import csv
import random
from datetime import datetime
from pathlib import Path
from typing import List

from telemetry.packet import TelemetryPacket
from telemetry.schema import FRAME_TYPE_TELEMETRY, PROTOCOL_VERSION_V1


def _make_sample_packet(sequence: int) -> TelemetryPacket:
    return TelemetryPacket(
        protocol_version=PROTOCOL_VERSION_V1,
        frame_type=FRAME_TYPE_TELEMETRY,
        sequence=sequence,
        mission_elapsed_seconds=sequence * 0.25,
        current_state="Ascent",
        current_flight_phase="Flight_Coast",
        altitude=sequence * 3.2,
        vertical_velocity=12.5,
        vertical_acceleration=-0.2,
        system_armed=True,
        fault_detected=False,
        descending=False,
        payload_deployed=False,
        pressure=101325.0,
        temperature=24.8,
        voltage=11.4,
        gnss_time=int(sequence * 0.25),
        gnss_latitude=35.3331 + sequence * 1e-6,
        gnss_longitude=-117.803 - sequence * 1e-6,
        gnss_altitude=sequence * 3.1,
        gnss_sats=8,
        accelerometer_magnitude=9.82 + random.uniform(-0.1, 0.1),
        gyro_spin_rate=0.02,
        state_name="Ascent",
        flight_phase_name="Coast",
    )


def generate_sample_file(path: str, count: int = 50) -> None:
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as handle:
        for sequence in range(count):
            packet = _make_sample_packet(sequence)
            handle.write(",".join(packet.to_csv_row()))
            checksum = 0
            for byte in ",".join(packet.to_csv_row()).encode("utf-8"):
                checksum ^= byte
            handle.write(f",{checksum}\n")


def sample_packets(count: int = 10) -> List[str]:
    packets = []
    for sequence in range(count):
        packet = _make_sample_packet(sequence)
        raw = ",".join(packet.to_csv_row())
        checksum = 0
        for byte in raw.encode("utf-8"):
            checksum ^= byte
        packets.append(f"{raw},{checksum}")
    return packets
