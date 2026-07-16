import json
import math
import os
import ssl
import struct
import threading
import time
import uuid
import wave

import websocket

from volc_protocol import (
    EVENT_CONNECTION_STARTED,
    EVENT_END_ASR,
    EVENT_FINISH_SESSION,
    EVENT_SESSION_FAILED,
    EVENT_SESSION_FINISHED,
    EVENT_SESSION_STARTED,
    EVENT_START_CONNECTION,
    EVENT_START_SESSION,
    MESSAGE_AUDIO_SERVER,
    MESSAGE_ERROR,
    MESSAGE_FULL_CLIENT,
    SERIALIZATION_JSON,
    SERIALIZATION_NONE,
    pack_audio_frame,
    pack_event_frame,
    parse_event_frame,
)

APP_ID = os.environ["VOLCENGINE_APP_ID"]
ACCESS_KEY = os.environ["VOLCENGINE_API_KEY"]
APP_KEY = os.environ["VOLCENGINE_APP_KEY"]
MODEL_NAME = os.getenv("MODEL_NAME", "O")
VOICE_TYPE = os.getenv("VOICE_TYPE", "zh_female_vv_jupiter_bigtts")

URL = "wss://openspeech.bytedance.com/api/v3/realtime/dialogue"
SESSION_ID = str(uuid.uuid4())
AUDIO_SERIALIZATION = (
    SERIALIZATION_JSON if os.getenv("AUDIO_SERIALIZATION") == "json" else SERIALIZATION_NONE
)


def send_start_session(ws):
    config = {
        "dialog": {"extra": {"model": MODEL_NAME, "input_mod": "audio"}},
        "asr": {
            "audio_info": {"format": "pcm", "sample_rate": 16000, "channel": 1},
            "extra": {"end_smooth_window_ms": 800},
        },
        "tts": {
            "speaker": VOICE_TYPE,
            "audio_config": {"format": "pcm_s16le", "sample_rate": 16000, "channel": 1},
        },
    }
    payload = json.dumps(config, separators=(",", ":")).encode()
    ws.send(
        pack_event_frame(
            MESSAGE_FULL_CLIENT,
            EVENT_START_SESSION,
            payload,
            session_id=SESSION_ID,
        ),
        opcode=websocket.ABNF.OPCODE_BINARY,
    )
    print(f"[SEND] StartSession session_id={SESSION_ID}")


def send_audio_and_finish(ws):
    time.sleep(0.5)
    voice_file = os.getenv("VOICE_FILE")
    if voice_file:
        with wave.open(voice_file, "rb") as source:
            audio = source.readframes(source.getnframes())
        chunks = [audio[offset : offset + 640] for offset in range(0, len(audio), 640)]
        print(f"[PTT] Sending speech WAV, serialization={AUDIO_SERIALIZATION}...")
    else:
        samples = [int(7000 * math.sin(2 * math.pi * 440 * index / 16000)) for index in range(320)]
        chunk = struct.pack("<320h", *samples)
        chunks = [chunk] * 100
        print(f"[PTT] Sending 2 seconds of tone, serialization={AUDIO_SERIALIZATION}...")
    for chunk in chunks:
        ws.send(
            pack_audio_frame(chunk, SESSION_ID, AUDIO_SERIALIZATION),
            opcode=websocket.ABNF.OPCODE_BINARY,
        )
        time.sleep(0.02)

    ws.send(
        pack_event_frame(
            MESSAGE_FULL_CLIENT,
            EVENT_END_ASR,
            b"{}",
            session_id=SESSION_ID,
        ),
        opcode=websocket.ABNF.OPCODE_BINARY,
    )
    print("[SEND] EndASR")


def on_message(ws, message):
    if not isinstance(message, bytes):
        print(f"[RECV TEXT] {message}")
        return

    try:
        frame = parse_event_frame(message)
    except ValueError as error:
        print(f"[PARSE ERROR] {error}; frame={message.hex()}")
        return

    payload_text = frame.payload.decode("utf-8", errors="replace")
    if frame.message_type == MESSAGE_ERROR:
        print(f"[SERVER ERROR] code={frame.error_code} payload={payload_text}")
        return
    if frame.message_type == MESSAGE_AUDIO_SERVER:
        print(f"[RECV AUDIO] event={frame.event} bytes={len(frame.payload)}")
        return

    print(
        f"[RECV] event={frame.event} session={frame.session_id} "
        f"connect={frame.connect_id} payload={payload_text}"
    )
    if frame.event == EVENT_CONNECTION_STARTED:
        send_start_session(ws)
    elif frame.event == EVENT_SESSION_STARTED:
        threading.Thread(target=send_audio_and_finish, args=(ws,), daemon=True).start()
    elif frame.event in (EVENT_SESSION_FINISHED, EVENT_SESSION_FAILED):
        ws.close()


def on_error(ws, error):
    print(f"[ERROR] {error}")


def on_close(ws, close_status_code, close_msg):
    print(f"[CLOSED] status={close_status_code}, msg={close_msg}")


def on_open(ws):
    ws.send(
        pack_event_frame(MESSAGE_FULL_CLIENT, EVENT_START_CONNECTION, b"{}"),
        opcode=websocket.ABNF.OPCODE_BINARY,
    )
    print("[SEND] StartConnection")


if __name__ == "__main__":
    headers = {
        "X-Api-App-ID": APP_ID,
        "X-Api-Access-Key": ACCESS_KEY,
        "X-Api-Resource-Id": "volc.speech.dialog",
        "X-Api-App-Key": APP_KEY,
        "X-Api-Request-Id": str(uuid.uuid4()),
    }
    ws = websocket.WebSocketApp(
        URL,
        header=headers,
        on_open=on_open,
        on_message=on_message,
        on_error=on_error,
        on_close=on_close,
    )
    print("Starting protocol validation client...")
    ws.run_forever(sslopt={"cert_reqs": ssl.CERT_REQUIRED})
