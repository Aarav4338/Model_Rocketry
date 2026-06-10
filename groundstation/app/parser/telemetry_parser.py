from typing import List

from telemetry.packet import TelemetryPacket
from telemetry.schema import (
    FIELD_NAMES_V1,
    LEGACY_FIELD_NAMES,
    FRAME_TYPE_TELEMETRY,
    PROTOCOL_VERSION_V1,
)


class TelemetryParser:
    def parse(self, fields: List[str], version: int) -> TelemetryPacket:
        if version == PROTOCOL_VERSION_V1:
            return self._parse_v1(fields)
        return self._parse_legacy(fields)

    def _parse_v1(self, fields: List[str]) -> TelemetryPacket:
        if len(fields) != len(FIELD_NAMES_V1):
            raise ValueError("Invalid field count for v1 telemetry packet")

        if fields[1].upper() != FRAME_TYPE_TELEMETRY:
            raise ValueError(f"Unsupported frame type: {fields[1]}")

        return TelemetryPacket(
            protocol_version=int(fields[0]),
            frame_type=fields[1],
            sequence=int(fields[2]),
            mission_elapsed_seconds=float(fields[3]),
            current_state=fields[4],
            current_flight_phase=fields[5],
            altitude=float(fields[6]),
            vertical_velocity=float(fields[7]),
            vertical_acceleration=float(fields[8]),
            system_armed=self._truthy(fields[9]),
            fault_detected=self._truthy(fields[10]),
            descending=self._truthy(fields[11]),
            payload_deployed=self._truthy(fields[12]),
            pressure=float(fields[13]),
            temperature=float(fields[14]),
            voltage=float(fields[15]),
            gnss_time=int(fields[16]),
            gnss_latitude=float(fields[17]),
            gnss_longitude=float(fields[18]),
            gnss_altitude=float(fields[19]),
            gnss_sats=int(fields[20]),
            accelerometer_magnitude=float(fields[21]),
            gyro_spin_rate=float(fields[22]),
            state_name=fields[23],
            flight_phase_name=fields[24],
        )

    def _parse_legacy(self, fields: List[str]) -> TelemetryPacket:
        if len(fields) != len(LEGACY_FIELD_NAMES):
            raise ValueError("Invalid field count for legacy telemetry packet")

        return TelemetryPacket(
            protocol_version=0,
            frame_type=FRAME_TYPE_TELEMETRY,
            sequence=int(fields[0]),
            mission_elapsed_seconds=float(fields[1]),
            current_state=fields[2],
            current_flight_phase=fields[3],
            altitude=float(fields[4]),
            vertical_velocity=float(fields[5]),
            vertical_acceleration=float(fields[6]),
            system_armed=self._truthy(fields[7]),
            fault_detected=self._truthy(fields[8]),
            descending=self._truthy(fields[9]),
            payload_deployed=self._truthy(fields[10]),
            pressure=float(fields[11]),
            temperature=float(fields[12]),
            voltage=float(fields[13]),
            gnss_time=int(fields[14]),
            gnss_latitude=float(fields[15]),
            gnss_longitude=float(fields[16]),
            gnss_altitude=float(fields[17]),
            gnss_sats=int(fields[18]),
            accelerometer_magnitude=float(fields[19]),
            gyro_spin_rate=float(fields[20]),
            state_name=fields[21],
            flight_phase_name=fields[22],
        )

    @staticmethod
    def _truthy(value: str) -> bool:
        return value.strip() in {"1", "true", "True", "TRUE"}
