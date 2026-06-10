"""GUI entrypoint for the Ground Station.

Reuses the same backend construction as main.py (IntegrationRunner,
TelemetryManager) but wraps the display in a PyQt6 window instead of
printing to the console.

Usage
-----
    python main_gui.py --mode simulate --simulate-count 200
    python main_gui.py --mode replay --input-file path/to/telemetry_raw.csv
"""

import argparse
import sys
from pathlib import Path

from PyQt6.QtWidgets import QApplication

from core.telemetry_manager import TelemetryManager
from integration_runner import IntegrationRunner
from logger.csv_logger import CsvLogger
from logger.diagnostics import DiagnosticsLogger
from logger.raw_logger import RawTelemetryLogger
from logger.rejected_logger import RejectedPacketLogger
from parser.telemetry_parser import TelemetryParser
from validation.packet_validator import PacketValidator

from ui.main_window import MainWindow
from ui.theme import apply_dark_theme


def parse_args():
    parser = argparse.ArgumentParser(
        description="Ground Station — Mission Control (GUI)"
    )
    parser.add_argument(
        "--mode",
        dest="mode",
        choices=["simulate", "replay"],
        default="simulate",
        help="Operation mode: simulate a mission or replay an existing telemetry file",
    )
    parser.add_argument(
        "--input-file",
        dest="input_file",
        default="telemetry_input.txt",
        help="Path to the telemetry input file or the replay source file",
    )
    parser.add_argument(
        "--output-dir",
        dest="output_dir",
        default="logs",
        help="Directory for logs, CSV output, and diagnostics",
    )
    parser.add_argument(
        "--command-file",
        dest="command_file",
        default="telecommands.txt",
        help="Command file used for uplink in file transport",
    )
    parser.add_argument(
        "--poll-interval",
        dest="poll_interval",
        type=float,
        default=0.25,
        help="Seconds between input file polls when no packet is available",
    )
    parser.add_argument(
        "--simulate-count",
        dest="simulate_count",
        type=int,
        default=200,
        help="Number of telemetry packets to generate in simulation mode",
    )
    parser.add_argument(
        "--simulate-interval",
        dest="simulate_interval",
        type=float,
        default=0.25,
        help="Seconds between simulator packet emissions",
    )
    parser.add_argument(
        "--corruption-rate",
        dest="corruption_rate",
        type=float,
        default=0.15,
        help="Probability of intentionally corrupting a packet in simulation mode",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    # --- Build backend (identical to main.py) ---
    runner = IntegrationRunner(
        mode=args.mode,
        telemetry_source=args.input_file,
        output_dir=str(output_dir),
        command_file=args.command_file,
        packet_count=args.simulate_count,
        interval_seconds=args.simulate_interval,
        corruption_rate=args.corruption_rate,
        poll_interval=args.poll_interval,
    )

    transport = runner.build_transport()
    validator = PacketValidator()
    parser = TelemetryParser()
    csv_logger = CsvLogger(str(output_dir / "telemetry_valid.csv"))
    diagnostics = DiagnosticsLogger(str(output_dir / "diagnostics.log"))
    raw_logger = RawTelemetryLogger(str(output_dir / "telemetry_raw.csv"))
    rejected_logger = RejectedPacketLogger(str(output_dir / "rejected_packets.log"))

    manager = TelemetryManager(
        transport=transport,
        validator=validator,
        parser=parser,
        logger=csv_logger,
        diagnostics=diagnostics,
        raw_logger=raw_logger,
        rejected_logger=rejected_logger,
    )

    # Clean up stale files for simulation mode
    if args.mode == "simulate":
        for p in [
            output_dir / "telemetry_valid.csv",
            output_dir / "telemetry_raw.csv",
            output_dir / "rejected_packets.log",
            output_dir / "diagnostics.log",
        ]:
            try:
                if p.exists():
                    p.unlink()
            except Exception:
                pass

    # Start the simulator thread if applicable
    if runner.simulator is not None:
        runner.simulator.start()

    # --- Build and launch the GUI ---
    app = QApplication(sys.argv)
    apply_dark_theme(app)

    window = MainWindow(
        manager=manager,
        transport=transport,
        poll_interval=args.poll_interval,
    )
    window.show()
    window.start()

    exit_code = app.exec()

    # Clean up the simulator if it was running
    if runner.simulator is not None:
        runner.simulator.stop()

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
