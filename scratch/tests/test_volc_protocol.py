import struct
import unittest

from volc_protocol import (
    EVENT_CONNECTION_STARTED,
    EVENT_START_CONNECTION,
    EVENT_START_SESSION,
    MESSAGE_FULL_CLIENT,
    MESSAGE_FULL_SERVER,
    pack_event_frame,
    parse_event_frame,
)


class VolcProtocolTest(unittest.TestCase):
    def test_start_connection_contains_event_flag_and_no_session_id(self):
        frame = pack_event_frame(
            MESSAGE_FULL_CLIENT,
            EVENT_START_CONNECTION,
            b"{}",
        )
        self.assertEqual(frame[:4], bytes.fromhex("11141000"))
        self.assertEqual(struct.unpack(">i", frame[4:8])[0], EVENT_START_CONNECTION)
        self.assertEqual(struct.unpack(">I", frame[8:12])[0], 2)
        self.assertEqual(frame[12:], b"{}")

    def test_start_session_contains_length_prefixed_session_id(self):
        frame = pack_event_frame(
            MESSAGE_FULL_CLIENT,
            EVENT_START_SESSION,
            b'{"dialog":{}}',
            session_id="session-1",
        )
        self.assertEqual(frame[:4], bytes.fromhex("11141000"))
        self.assertEqual(struct.unpack(">i", frame[4:8])[0], EVENT_START_SESSION)
        self.assertEqual(struct.unpack(">I", frame[8:12])[0], 9)
        self.assertEqual(frame[12:21], b"session-1")
        self.assertEqual(struct.unpack(">I", frame[21:25])[0], 13)
        self.assertEqual(frame[25:], b'{"dialog":{}}')

    def test_connection_started_response_parses_event_and_connect_id(self):
        connect_id = b"connect-1"
        payload = b"{}"
        frame = (
            bytes.fromhex("11941000")
            + struct.pack(">i", EVENT_CONNECTION_STARTED)
            + struct.pack(">I", len(connect_id))
            + connect_id
            + struct.pack(">I", len(payload))
            + payload
        )
        parsed = parse_event_frame(frame)
        self.assertEqual(parsed.message_type, MESSAGE_FULL_SERVER)
        self.assertEqual(parsed.event, EVENT_CONNECTION_STARTED)
        self.assertEqual(parsed.connect_id, "connect-1")
        self.assertEqual(parsed.payload, payload)


if __name__ == "__main__":
    unittest.main()
