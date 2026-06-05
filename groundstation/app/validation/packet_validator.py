from dataclasses import dataclass, field
from csv import reader
from io import StringIO
from typing import List

from telemetry.schema import (
    EXPECTED_FIELD_COUNT_LEGACY,
    EXPECTED_FIELD_COUNT_V1,
    FIELD_NAMES_V1,
    LEGACY_FIELD_NAMES,
    PROTOCOL_VERSION_V1,
    FRAME_TYPE_TELEMETRY,
)
from validation.checksum import checksum_to_int, compute_xor_checksum


@dataclass
class PacketValidationResult:
    valid: bool
    fields: List[str] = field(default_factory=list)
    version: int = 0
    errors: List[str] = field(default_factory=list)


class PacketValidator:
    def validate(self, raw_line: str) -> PacketValidationResult:
        source = raw_line.strip()
        if len(source) == 0:
            return PacketValidationResult(valid=False, errors=["Empty packet"])

        try:
            parsed = next(reader(StringIO(source)))
        except Exception as exc:
            return PacketValidationResult(valid=False, errors=[f"CSV parse failed: {exc}"])

        fields = [field.strip() for field in parsed if field is not None]
        if len(fields) == EXPECTED_FIELD_COUNT_V1 and self._looks_like_v1(fields):
            return self._validate_v1(fields)
        if len(fields) == EXPECTED_FIELD_COUNT_LEGACY:
            return self._validate_legacy(fields)

        return PacketValidationResult(
            valid=False,
            errors=[
                f"Unexpected field count: {len(fields)} (expected {EXPECTED_FIELD_COUNT_V1} for v1 or {EXPECTED_FIELD_COUNT_LEGACY} for legacy)"
            ],
        )

    def _looks_like_v1(self, fields: List[str]) -> bool:
        try:
            version = int(fields[0])
        except ValueError:
            return False
        return version == PROTOCOL_VERSION_V1 and fields[1].upper() == FRAME_TYPE_TELEMETRY

    def _validate_v1(self, fields: List[str]) -> PacketValidationResult:
        payload = ",".join(fields[:-1])
        checksum_value = self._parse_checksum(fields[-1])
        if checksum_value is None:
            return PacketValidationResult(valid=False, errors=["Invalid checksum format"])

        if compute_xor_checksum(payload) != checksum_value:
            return PacketValidationResult(valid=False, errors=["Checksum mismatch"])

        return PacketValidationResult(valid=True, fields=fields[:-1], version=PROTOCOL_VERSION_V1)

    def _validate_legacy(self, fields: List[str]) -> PacketValidationResult:
        payload = ",".join(fields[:-1])
        checksum_value = self._parse_checksum(fields[-1])
        if checksum_value is None:
            return PacketValidationResult(valid=False, errors=["Invalid checksum format"])

        if compute_xor_checksum(payload) != checksum_value:
            return PacketValidationResult(valid=False, errors=["Checksum mismatch"])

        return PacketValidationResult(valid=True, fields=fields[:-1], version=0)

    @staticmethod
    def _parse_checksum(raw: str) -> int:
        try:
            return checksum_to_int(raw)
        except ValueError:
            return None
