import argparse
import os
from pathlib import Path

from validation.packet_validator import PacketValidator
from parser.telemetry_parser import TelemetryParser
from core.telemetry_manager import TelemetryManager
from logger.csv_logger import CsvLogger
from logger.diagnostics import DiagnosticsLogger
from logger.raw_logger import RawTelemetryLogger
from logger.rejected_logger import RejectedPacketLogger
from integration_runner import IntegrationRunner


def parse_args():
    parser = argparse.ArgumentParser(
        description="Headless competition ground station prototype"
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
        help="Path to the telemetry input file used by the file-based transport or the replay source file",
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
        help="Seconds to wait between input file polls when no packet is available",
    )
    parser.add_argument(
        "--simulate-count",
        dest="simulate_count",
        type=int,
        default=100,
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
    parser.add_argument(
        "--duration",
        dest="duration",
        type=float,
        default=None,
        help="Optional duration in seconds to run the integration test before stopping",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    runner = IntegrationRunner(
        mode=args.mode,
        telemetry_source=args.input_file,
        output_dir=str(output_dir),
        command_file=args.command_file,
        packet_count=args.simulate_count,
        interval_seconds=args.simulate_interval,
        corruption_rate=args.corruption_rate,
        duration_seconds=args.duration,
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

    print("Starting ground station headless prototype")
    print(f"Mode: {args.mode}")
    print(f"Telemetry source: {args.input_file}")
    print(f"Output directory: {output_dir}")
    print(f"Valid telemetry log: {output_dir / 'telemetry_valid.csv'}")
    print(f"Raw telemetry log: {output_dir / 'telemetry_raw.csv'}")
    print(f"Rejected packet log: {output_dir / 'rejected_packets.log'}")
    print(f"Diagnostics log: {output_dir / 'diagnostics.log'}")

    # For simulate mode start fresh so sequence numbers don't mix across runs
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

    try:
        runner.run(manager)
    except KeyboardInterrupt:
        print("Shutting down ground station")


if __name__ == "__main__":
    main()
