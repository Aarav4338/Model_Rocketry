from transport.transport import Transport


class CommandDispatcher:
    def __init__(self, transport: Transport) -> None:
        self.transport = transport

    def send(self, command: str) -> None:
        self.transport.write_packet(command)
