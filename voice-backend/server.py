"""Voice assistant backend for the ESP32-S3 round display.

The board streams raw 16 kHz mono PCM over a WebSocket and expects a text
answer back; it does the speech synthesis itself. So this only has to
transcribe and reply.

Protocol, as implemented by src/AIAssistant.cpp on the device:
    device -> {"type":"HELLO"} / {"type":"START_STREAM"} / <binary PCM> / {"type":"STOP_STREAM"}
    server -> {"type":"ASSISTANT_TEXT_RESPONSE", "content": "...", "language": "es"}

Transcription runs locally with faster-whisper and the reply is spoken with
Kokoro, also local, so neither needs an API key. Only the answer itself goes
out to DeepSeek, whose key is read from the environment and never hardcoded.

The board used to synthesise speech itself through a free telephone-quality
service. Instead the reply is rendered here and served over HTTP, and the board
just streams that URL, which is the same path the internet radio uses and the
one that sounds clean on this hardware.
"""

import asyncio
import io
import time
import json
import logging
import os
import struct
import subprocess
import wave

import numpy as np
import requests
import soundfile as sf
import websockets
from aiohttp import web
from faster_whisper import WhisperModel
from kokoro import KPipeline

LOG = logging.getLogger("esp32-voice")
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

HOST = os.getenv("VOICE_HOST", "0.0.0.0")
PORT = int(os.getenv("VOICE_PORT", "8765"))
SAMPLE_RATE = 16000
STT_MODEL = os.getenv("VOICE_STT_MODEL", "base")     # local whisper size
CHAT_MODEL = os.getenv("VOICE_CHAT_MODEL", "deepseek-chat")
CHAT_URL = "https://api.deepseek.com/chat/completions"
TTS_VOICE = os.getenv("VOICE_TTS_VOICE", "ef_dora")
HTTP_PORT = int(os.getenv("VOICE_HTTP_PORT", "8772"))
SERVER_IP = os.getenv("VOICE_SERVER_IP", "192.168.1.104")
SPEECH_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "speech")

SYSTEM_PROMPT = (
    "Eres el asistente de voz de un pequeño dispositivo con pantalla redonda. "
    "Respondes en español de Chile, en tono natural y cercano, tratando de tú. "
    "Tus respuestas se leen en voz alta y cada palabra suma espera, así que "
    "contestas con UNA sola frase corta, de menos de 120 caracteres. Sin "
    "listas, sin markdown y sin emojis. Si el tema da para más, terminas "
    "ofreciendo contarlo."
)

CHAT_KEY = os.environ["DEEPSEEK_API_KEY"]

LOG.info("loading whisper model '%s' (first run downloads it)...", STT_MODEL)
whisper = WhisperModel(STT_MODEL, device="cpu", compute_type="int8")
LOG.info("whisper ready")

os.makedirs(SPEECH_DIR, exist_ok=True)
LOG.info("loading kokoro voice '%s'...", TTS_VOICE)
kokoro = KPipeline(lang_code="e")
LOG.info("kokoro ready")


def synthesize(text: str) -> str:
    """Render the reply and return the name the board can fetch.

    Delivered as 44.1 kHz stereo MP3 on purpose: this board plays MP3 cleanly
    but mangles WAV, and Kokoro's native 24 kHz came out slow and muffled.
    """
    t0 = time.time()
    chunks = [audio for _, _, audio in kokoro(text, voice=TTS_VOICE)]
    if not chunks:
        return ""
    audio = np.concatenate(chunks)
    t_synth = time.time()

    # Straight through a pipe: writing a temporary WAV and reading it back cost
    # about a second on its own.
    wav = io.BytesIO()
    sf.write(wav, audio, 24000, format="WAV", subtype="PCM_16")

    name = "reply-%d.mp3" % (int(time.time() * 1000) % 1000000)
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-i", "pipe:0",
         # Kokoro renders quietly and the board's speaker is small, so bring the
         # level up to broadcast loudness with a limiter to avoid clipping.
         "-af", "loudnorm=I=-9:TP=-0.5:LRA=5",
         "-ar", "44100", "-ac", "2", "-b:a", "128k",
         os.path.join(SPEECH_DIR, name)],
        input=wav.getvalue(),
        check=True,
    )
    LOG.info("voice: kokoro %.1fs + encode %.1fs", t_synth - t0, time.time() - t_synth)

    # Keep only the last few files so the folder does not grow forever.
    # Sort by age, not by name: the name is a truncated timestamp that does not
    # grow, so a low number sorted ahead of the older files and this deleted the
    # reply it had just rendered. The board then fetched it and got a 404.
    files = sorted(os.listdir(SPEECH_DIR),
                   key=lambda f: os.path.getmtime(os.path.join(SPEECH_DIR, f)))
    for old in files[:-5]:
        try:
            os.remove(os.path.join(SPEECH_DIR, old))
        except OSError:
            pass
    return name


def pcm_to_wav(pcm: bytes) -> bytes:
    """Wrap raw PCM in a WAV container, which is what the API expects."""
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm)
    return buf.getvalue()


def transcribe(pcm: bytes) -> str:
    segments, _ = whisper.transcribe(io.BytesIO(pcm_to_wav(pcm)), language="es", beam_size=1)
    return " ".join(seg.text for seg in segments).strip()


def answer(question: str, history: list) -> str:
    messages = [{"role": "system", "content": SYSTEM_PROMPT}] + history[-6:]
    messages.append({"role": "user", "content": question})
    r = requests.post(
        CHAT_URL,
        headers={"Authorization": "Bearer " + CHAT_KEY, "Content-Type": "application/json"},
        json={"model": CHAT_MODEL, "messages": messages, "max_tokens": 60, "temperature": 0.6},
        timeout=60,
    )
    r.raise_for_status()
    return r.json()["choices"][0]["message"]["content"].strip()


async def handle(ws):
    peer = getattr(ws, "remote_address", ("?", 0))[0]
    LOG.info("device connected: %s", peer)
    audio = bytearray()
    recording = False
    history = []

    try:
        async for msg in ws:
            if isinstance(msg, bytes):
                if recording:
                    audio.extend(msg)
                continue

            try:
                kind = json.loads(msg).get("type", "")
            except json.JSONDecodeError:
                LOG.warning("unparsable message: %s", msg[:80])
                continue

            if kind == "HELLO":
                await ws.send(json.dumps({"type": "HELLO"}))

            elif kind == "START_STREAM":
                audio = bytearray()
                recording = True
                LOG.info("recording started")

            elif kind == "STOP_STREAM":
                recording = False
                seconds = len(audio) / (SAMPLE_RATE * 2)
                LOG.info("recording stopped: %.1f s", seconds)
                if seconds < 0.3:
                    LOG.info("too short, ignoring")
                    continue

                text = await asyncio.to_thread(transcribe, bytes(audio))
                LOG.info("heard: %s", text or "(nothing)")
                if not text:
                    continue

                reply = await asyncio.to_thread(answer, text, history)
                LOG.info("reply: %s", reply)
                history.append({"role": "user", "content": text})
                history.append({"role": "assistant", "content": reply})

                name = await asyncio.to_thread(synthesize, reply)
                if name:
                    url = "http://%s:%d/speech/%s" % (SERVER_IP, HTTP_PORT, name)
                    LOG.info("speaking: %s", url)
                    await ws.send(json.dumps({
                        "type": "ASSISTANT_AUDIO_URL",
                        "content": url,
                        "text": reply,
                    }))
                else:
                    # Fall back to the board's own speech if rendering failed.
                    await ws.send(json.dumps({
                        "type": "ASSISTANT_TEXT_RESPONSE",
                        "content": reply,
                        "language": "es",
                        "mode": "voice",
                        "sender": "assistant",
                    }))
    except websockets.ConnectionClosed:
        pass
    finally:
        LOG.info("device disconnected: %s", peer)


async def serve_speech():
    """Plain file server so the board can stream the rendered reply."""
    app = web.Application()
    app.router.add_static("/speech/", SPEECH_DIR)
    runner = web.AppRunner(app)   # log every fetch: the board reported NOT FOUND
    await runner.setup()
    await web.TCPSite(runner, HOST, HTTP_PORT).start()
    LOG.info("speech files on http://%s:%d/speech/", SERVER_IP, HTTP_PORT)


async def main():
    LOG.info("listening on ws://%s:%d  (stt=%s, chat=%s, voice=%s)",
             HOST, PORT, STT_MODEL, CHAT_MODEL, TTS_VOICE)
    await serve_speech()
    async with websockets.serve(handle, HOST, PORT, max_size=8 * 1024 * 1024):
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
