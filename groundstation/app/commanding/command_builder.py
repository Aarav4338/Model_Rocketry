from typing import Optional


class CommandBuilder:
    @staticmethod
    def build_legacy(command_name: str) -> str:
        return command_name.strip().upper()

    @staticmethod
    def build_transport_neutral(command_name: str, argument: Optional[str] = None) -> str:
        canonical = command_name.strip().upper()
        if argument:
            return f"CMD:{canonical},{argument}"
        return f"CMD:{canonical}"
