import random
import threading
import time
from pathlib import Path
from typing import Optional

from telemetry.packet import TelemetryPacket
from telemetry.schema import (
    FRAME_TYPE_TELEMETRY,
    PROTOCOL_VERSION_V1,
    STATE_ASCENT,
    STATE_COAST,
    STATE_DESCENT,
    PHASE_ASCENT,
    PHASE_COAST,
    PHASE_DESCENT,
)
from validation.checksum import compute_xor_checksum


class MissionSimulator:
    """Stateful mission simulator with integrated altitude and deterministic sequence.

    Produces packets by integrating velocity into altitude so descent actually
    reduces altitude. Sequence is maintained as simulator state so it won't
    inadvertently reset mid-run.
    """

    def __init__(
        self,
        output_path: str,
        command_path: Optional[str] = None,
        packet_count: int = 100,
        interval_seconds: float = 0.25,
        corruption_rate: float = 0.15,
        malformed_rate: float = 0.05,
        missing_field_rate: float = 0.05,
        invalid_checksum_rate: float = 0.05,
        seed: Optional[int] = None,
    ) -> None:
        self.output_path = output_path
        self.command_path = command_path
        self.packet_count = packet_count
        self.interval_seconds = interval_seconds
        self.corruption_rate = corruption_rate
        self.malformed_rate = malformed_rate
        self.missing_field_rate = missing_field_rate
        self.invalid_checksum_rate = invalid_checksum_rate
        self._stop_event = threading.Event()
        self._thread: Optional[threading.Thread] = None

        # Command handling state
        self._last_command_pos = 0
        self._system_armed = False
        self._launch_triggered = False
        self._launch_time = 0.0
        self._altitude_offset = 0.0
        self._telemetry_muted = False
        self._ping_flag = False
        self._force_packet = False

        # Stateful flight variables
        self._total_sequence = 0
        self._flight_sequence = 0
        self._altitude = 0.0
        self._vertical_velocity = 12.5
        self._vertical_acceleration = -0.2

        # Phase boundaries (deterministic profile)
        self._ascent_end = max(1, int(self.packet_count * 0.4))
        self._coast_end = self._ascent_end + max(1, int(self.packet_count * 0.2))

        if seed is not None:
            random.seed(seed)

    def start(self) -> None:
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=3.0)
            self._thread = None

    def is_running(self) -> bool:
        return self._thread is not None and self._thread.is_alive()

    def _read_commands(self) -> None:
        if not self.command_path:
            return
            
        p = Path(self.command_path)
        if not p.exists():
            return
            
        try:
            with p.open("r", encoding="utf-8") as f:
                f.seek(self._last_command_pos)
                lines = f.readlines()
                self._last_command_pos = f.tell()
                
            for line in lines:
                cmd = line.strip()
                if cmd == "CMD:ARM_FLIGHT":
                    self._system_armed = True
                    if not self._launch_triggered:
                        self._launch_triggered = True
                        self._launch_time = time.time() + 3.0  # 3s launch delay
                elif cmd == "CMD:DISARM_FLIGHT":
                    self._system_armed = False
                elif cmd == "CMD:PING":
                    self._ping_flag = True
                elif cmd == "CMD:REQUEST_STATUS":
                    self._force_packet = True
                elif cmd == "CMD:STOP_TELEMETRY":
                    self._telemetry_muted = True
                elif cmd == "CMD:START_TELEMETRY":
                    self._telemetry_muted = False
                elif cmd == "CMD:ZERO_SENSORS":
                    # Offset calibration
                    self._altitude_offset = self._altitude
        except Exception:
            # Ignore read errors in simulation loop
            pass

    def _run(self) -> None:
        output_file = Path(self.output_path)
        output_file.parent.mkdir(parents=True, exist_ok=True)
        if output_file.exists():
            output_file.unlink()

        if self.command_path:
            cmd_file = Path(self.command_path)
            if cmd_file.exists():
                cmd_file.unlink()

        # Run indefinitely until stopped, allowing pre-launch waiting
        while not self._stop_event.is_set():
            self._read_commands()

            # If a status packet is requested, we emit one immediately regardless of mute state
            if self._telemetry_muted and not self._force_packet:
                time.sleep(self.interval_seconds)
                continue

            self._force_packet = False

            packet = self._make_packet()
            raw_packet = self._serialize(packet)
            corrupted_packet = self._maybe_corrupt(raw_packet)
            with output_file.open("a", encoding="utf-8") as handle:
                handle.write(f"{corrupted_packet}\n")

            time.sleep(self.interval_seconds)

    def _make_packet(self) -> TelemetryPacket:
        # Determine if we are flying yet
        flying = self._launch_triggered and time.time() >= self._launch_time

        if not flying:
            state = "BOOT"
            phase = "BOOT"
            vertical_velocity = 0.0
            vertical_acceleration = 0.0
        else:
            seq = self._flight_sequence
            if seq < self._ascent_end:
                state = STATE_ASCENT
                phase = PHASE_ASCENT
                # slowly reducing climb as fuel burns
                vertical_velocity = max(1.0, self._vertical_velocity - seq * 0.05)
                vertical_acceleration = -0.05
            elif seq < self._coast_end:
                state = STATE_COAST
                phase = PHASE_COAST
                vertical_velocity = 0.2  # near-zero coast
                vertical_acceleration = 0.0
            else:
                state = STATE_DESCENT
                phase = PHASE_DESCENT
                # increase descent rate over time
                descent_index = seq - self._coast_end
                vertical_velocity = - (5.0 + descent_index * 0.3)
                vertical_acceleration = -0.2

            # Apply acceleration to velocity (simple Euler integration)
            self._vertical_velocity = vertical_velocity + vertical_acceleration * self.interval_seconds

            # Integrate altitude from velocity
            self._altitude = max(0.0, self._altitude + self._vertical_velocity * self.interval_seconds)

            # Advance flight sequence
            if self._flight_sequence < self.packet_count:
                self._flight_sequence += 1

        # Apply offset calibration to the final displayed altitude
        display_altitude = max(0.0, self._altitude - self._altitude_offset)

        # Telemetry fields
        mission_time = self._total_sequence * self.interval_seconds
        
        # Environmental and status fields (deterministic-ish)
        pressure = max(30000.0, 101325.0 - display_altitude * 0.12)
        temperature = 24.8 - display_altitude * 0.001
        voltage = 11.4 - self._flight_sequence * 0.005

        packet = TelemetryPacket(
            protocol_version=PROTOCOL_VERSION_V1,
            frame_type=FRAME_TYPE_TELEMETRY,
            sequence=self._total_sequence,
            mission_elapsed_seconds=mission_time,
            current_state=state,
            current_flight_phase=phase,
            altitude=display_altitude,
            vertical_velocity=vertical_velocity,
            vertical_acceleration=vertical_acceleration,
            system_armed=self._system_armed,
            fault_detected=False,
            descending=(state == STATE_DESCENT) if flying else False,
            payload_deployed=(self._flight_sequence >= int(self.packet_count * 0.85)),
            pressure=pressure,
            temperature=temperature,
            voltage=voltage,
            gnss_time=int(mission_time),
            gnss_latitude=35.3331 + self._flight_sequence * 1e-6,
            gnss_longitude=-117.803 - self._flight_sequence * 1e-6,
            gnss_altitude=display_altitude,
            gnss_sats=88 if self._ping_flag else 8,
            accelerometer_magnitude=9.82 + random.uniform(-0.1, 0.1),
            gyro_spin_rate=0.02 + random.uniform(-0.01, 0.01),
            state_name=state.title(),
            flight_phase_name=phase.title(),
        )

        self._ping_flag = False
        self._total_sequence += 1
        return packet

    def _serialize(self, packet: TelemetryPacket) -> str:
        payload = ",".join(packet.to_csv_row())
        checksum = compute_xor_checksum(payload)
        return f"{payload},{checksum}"

    def _maybe_corrupt(self, raw_packet: str) -> str:
        if random.random() < self.malformed_rate:
            return self._make_malformed(raw_packet)

        if random.random() < self.missing_field_rate:
            return self._remove_random_field(raw_packet)

        if random.random() < self.invalid_checksum_rate:
            return self._make_invalid_checksum(raw_packet)

        if random.random() < self.corruption_rate:
            return self._flip_random_character(raw_packet)

        return raw_packet

    def _make_malformed(self, raw_packet: str) -> str:
        return "!!!CORRUPTED_PAYLOAD!!!"

    def _remove_random_field(self, raw_packet: str) -> str:
        parts = raw_packet.split(",")
        if len(parts) <= 2:
            return raw_packet
        index = random.randrange(0, len(parts) - 1)
        parts.pop(index)
        return ",".join(parts)

    def _make_invalid_checksum(self, raw_packet: str) -> str:
        payload, _ = raw_packet.rsplit(",", 1)
        invalid_checksum = random.randint(0, 255)
        while invalid_checksum == compute_xor_checksum(payload):
            invalid_checksum = random.randint(0, 255)
        return f"{payload},{invalid_checksum}"

    def _flip_random_character(self, raw_packet: str) -> str:
        if not raw_packet:
            return raw_packet
        printable = [chr(i) for i in range(32, 127)]
        index = random.randrange(len(raw_packet))
        current = raw_packet[index]
        replacement = random.choice([c for c in printable if c != current])
        return raw_packet[:index] + replacement + raw_packet[index + 1 :]
