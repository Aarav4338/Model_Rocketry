

def compute_xor_checksum(payload: str) -> int:
    checksum = 0
    for byte in payload.encode("utf-8"):
        checksum ^= byte
    return checksum


def checksum_to_int(raw_value: str) -> int:
    raw = raw_value.strip()
    if raw.lower().startswith("0x"):
        return int(raw, 16)
    return int(raw)
