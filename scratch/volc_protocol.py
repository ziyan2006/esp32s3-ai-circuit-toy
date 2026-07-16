from dataclasses import dataclass
import struct

MESSAGE_FULL_CLIENT = 0x1
MESSAGE_AUDIO_CLIENT = 0x2
MESSAGE_FULL_SERVER = 0x9
MESSAGE_AUDIO_SERVER = 0xB
MESSAGE_ERROR = 0xF

FLAG_WITH_EVENT = 0x4
SERIALIZATION_NONE = 0x0
SERIALIZATION_JSON = 0x1

EVENT_START_CONNECTION = 1
EVENT_FINISH_CONNECTION = 2
EVENT_CONNECTION_STARTED = 50
EVENT_CONNECTION_FAILED = 51
EVENT_CONNECTION_FINISHED = 52
EVENT_START_SESSION = 100
EVENT_FINISH_SESSION = 102
EVENT_SESSION_STARTED = 150
EVENT_SESSION_FINISHED = 152
EVENT_SESSION_FAILED = 153
EVENT_TASK_REQUEST = 200
EVENT_END_ASR = 400

_CONNECTION_EVENTS = {
    EVENT_START_CONNECTION,
    EVENT_FINISH_CONNECTION,
    EVENT_CONNECTION_STARTED,
    EVENT_CONNECTION_FAILED,
    EVENT_CONNECTION_FINISHED,
}


@dataclass(frozen=True)
class EventFrame:
    message_type: int
    flags: int
    serialization: int
    event: int | None
    session_id: str | None
    connect_id: str | None
    error_code: int | None
    payload: bytes


def _u32(value: int) -> bytes:
    return struct.pack(">I", value)


def pack_event_frame(message_type: int, event: int, payload: bytes, session_id: str | None = None) -> bytes:
    header = bytes((0x11, (message_type << 4) | FLAG_WITH_EVENT, SERIALIZATION_JSON << 4, 0x00))
    frame = bytearray(header)
    frame.extend(struct.pack(">i", event))
    if event not in (EVENT_START_CONNECTION, EVENT_FINISH_CONNECTION):
        encoded_session_id = (session_id or "").encode("utf-8")
        frame.extend(_u32(len(encoded_session_id)))
        frame.extend(encoded_session_id)
    frame.extend(_u32(len(payload)))
    frame.extend(payload)
    return bytes(frame)


def pack_audio_frame(payload: bytes, session_id: str, serialization: int = SERIALIZATION_NONE) -> bytes:
    header = bytes((
        0x11,
        (MESSAGE_AUDIO_CLIENT << 4) | FLAG_WITH_EVENT,
        serialization << 4,
        0x00,
    ))
    encoded_session_id = session_id.encode("utf-8")
    return (
        header
        + struct.pack(">i", EVENT_TASK_REQUEST)
        + _u32(len(encoded_session_id))
        + encoded_session_id
        + _u32(len(payload))
        + payload
    )


def parse_event_frame(data: bytes) -> EventFrame:
    if len(data) < 4:
        raise ValueError("frame is shorter than the protocol header")

    header_size = (data[0] & 0x0F) * 4
    if header_size < 4 or len(data) < header_size:
        raise ValueError("invalid protocol header size")

    message_type = data[1] >> 4
    flags = data[1] & 0x0F
    serialization = data[2] >> 4
    offset = header_size
    event = None
    session_id = None
    connect_id = None
    error_code = None

    if message_type == MESSAGE_ERROR:
        if len(data) < offset + 4:
            raise ValueError("missing error code")
        error_code = struct.unpack_from(">I", data, offset)[0]
        offset += 4

    if flags == FLAG_WITH_EVENT:
        if len(data) < offset + 4:
            raise ValueError("missing event")
        event = struct.unpack_from(">i", data, offset)[0]
        offset += 4

        if event not in _CONNECTION_EVENTS:
            session_id, offset = _read_string(data, offset)
        if event in (EVENT_CONNECTION_STARTED, EVENT_CONNECTION_FAILED, EVENT_CONNECTION_FINISHED):
            connect_id, offset = _read_string(data, offset)

    if len(data) < offset + 4:
        raise ValueError("missing payload size")
    payload_size = struct.unpack_from(">I", data, offset)[0]
    offset += 4
    if len(data) < offset + payload_size:
        raise ValueError("truncated payload")

    return EventFrame(
        message_type=message_type,
        flags=flags,
        serialization=serialization,
        event=event,
        session_id=session_id,
        connect_id=connect_id,
        error_code=error_code,
        payload=data[offset : offset + payload_size],
    )


def _read_string(data: bytes, offset: int) -> tuple[str, int]:
    if len(data) < offset + 4:
        raise ValueError("missing string size")
    size = struct.unpack_from(">I", data, offset)[0]
    offset += 4
    if len(data) < offset + size:
        raise ValueError("truncated string")
    return data[offset : offset + size].decode("utf-8"), offset + size
