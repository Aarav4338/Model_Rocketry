import os
import time
from pathlib import Path
from threading import Event
from typing import Optional

from core.telemetry_manager import TelemetryManager
from simulation.mission_simulator import MissionSimulator
from transport.file_transport import FileTransport
from transport.replay_transport import ReplayTransport


class IntegrationRunner:
    def __init__(
        self,
        mode: str,
        telemetry_source: str,
        output_dir: str,
        command_file: str,
        packet_count: int = 100,
        interval_seconds: float = 0.25,
        corruption_rate: float = 0.15,
        duration_seconds: Optional[float] = None,
        poll_interval: float = 0.25,
    ) -> None:
        self.mode = mode
        self.telemetry_source = telemetry_source
        self.output_dir = output_dir
        self.command_file = command_file
        self.packet_count = packet_count
        self.interval_seconds = interval_seconds
        self.corruption_rate = corruption_rate
        self.duration_seconds = duration_seconds
        self.poll_interval = poll_interval
        self._stop_event = Event()
        self.simulator: Optional[MissionSimulator] = None

        os.makedirs(self.output_dir, exist_ok=True)

    def build_transport(self):
        return self._build_transport()

    def _build_transport(self):
        if self.mode == "replay":
            return ReplayTransport(self.telemetry_source)
        
        # FIX 1: Make corruption_rate the single source of truth
        # When corruption_rate=0.0, all corruption mechanisms must be 0.0 for clean runs
        # Otherwise use standard sub-corruption rates
        sub_corruption_rate = 0.0 if self.corruption_rate == 0.0 else 0.05
        
        self.simulator = MissionSimulator(
            output_path=self.telemetry_source,
            command_path=self.command_file,
            packet_count=self.packet_count,
            interval_seconds=self.interval_seconds,
            corruption_rate=self.corruption_rate,
            malformed_rate=sub_corruption_rate,
            missing_field_rate=sub_corruption_rate,
            invalid_checksum_rate=sub_corruption_rate,
        )
        return FileTransport(self.telemetry_source, self.command_file)

    def run(self, manager: TelemetryManager) -> None:
        if self.simulator is not None:
            self.simulator.start()

        manager.start()
        try:
            start_time = time.time()
            idle_cycles = 0
            max_idle_cycles = int(5.0 / self.poll_interval)
            while not self._stop_event.is_set():
                handled = manager.run_once()
                if handled:
                    idle_cycles = 0
                    manager.pretty_print()
                else:
                    idle_cycles += 1
                    if self.mode == "replay" and idle_cycles > max_idle_cycles:
                        break
                    if self.simulator is not None and not self.simulator.is_running() and idle_cycles > max_idle_cycles:
                        break
                    time.sleep(self.poll_interval)

                if self.duration_seconds is not None and time.time() - start_time >= self.duration_seconds:
                    break

        except KeyboardInterrupt:
            print("Integration runner interrupted by user.")
        finally:
            manager.stop()
            if self.simulator is not None:
                self.simulator.stop()

    def stop(self) -> None:
        self._stop_event.set()
