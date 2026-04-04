"""
chatbot_session.py - OpenAI Realtime API session manager.

Provides ChatbotSession: one session per ESP32 client.
Audio path:
  ESP32 binary PCM16 → base64 → OpenAI input_audio_buffer.append
  OpenAI response.audio.delta → decode base64 → binary PCM16 → ESP32
"""

import json
import base64
import asyncio
import time
import datetime
import logging
import websockets
from core.config import settings
from services.storage_service import StorageService
from services.realtime_ai import RealtimeAIService

logger = logging.getLogger("chatbot")

class ChatbotSession:
    """Manages an OpenAI Realtime API connection for one ESP32 client."""

    def __init__(self, esp32_ws, client_id: str, storage_svc: StorageService, ai_svc: RealtimeAIService):
        self.esp32_ws = esp32_ws
        self.client_id = client_id
        self.storage_svc = storage_svc
        self.ai_svc = ai_svc
        
        self.openai_ws = None
        self.relay_task = None
        self.idle_checker_task = None
        self.active = False
        self.last_activity = time.time()

    async def start(self) -> bool:
        """Connect to OpenAI Realtime API and begin relaying."""
        if not settings.OPENAI_API_KEY:
            logger.error("[Chatbot] OPENAI_API_KEY not set!")
            await self.esp32_ws.send(json.dumps({"type": "ERROR", "message": "No API key configured"}))
            return False

        try:
            logger.info(f"[Chatbot:{self.client_id}] Connecting to OpenAI Realtime API...")
            extra_headers = {
                "Authorization": f"Bearer {settings.OPENAI_API_KEY}",
                "OpenAI-Beta": "realtime=v1",
            }
            self.openai_ws = await websockets.connect(
                settings.OPENAI_REALTIME_URL,
                additional_headers=extra_headers,
                max_size=2**24,
                ping_interval=20,
                ping_timeout=20,
            )
            self.active = True
            self.last_activity = time.time()
            logger.info(f"[Chatbot:{self.client_id}] Connected to OpenAI")

            # Initialize transcript file natively using service
            self.storage_svc.init_transcript(self.client_id)

            # Start background tasks
            self.relay_task = asyncio.create_task(self._relay_openai_to_esp32())
            self.idle_checker_task = asyncio.create_task(self._check_idle_timeout())

            # Notify ESP32
            await self.esp32_ws.send(json.dumps({"type": "CHATBOT_READY"}))
            return True

        except Exception as e:
            logger.exception(f"[Chatbot:{self.client_id}] Failed to connect to OpenAI: {e}")
            self.active = False
            return False

    async def send_audio(self, pcm_data: bytes):
        """Forward binary PCM16 audio from ESP32 to OpenAI."""
        if not self.active or not self.openai_ws or self.openai_ws.closed:
            return
        
        self.last_activity = time.time()
        try:
            audio_b64 = base64.b64encode(pcm_data).decode("utf-8")
            event = {
                "type": "input_audio_buffer.append",
                "audio": audio_b64,
            }
            await self.openai_ws.send(json.dumps(event))
        except websockets.exceptions.ConnectionClosed:
            logger.warning(f"[Chatbot:{self.client_id}] OpenAI disconnected during audio send")
            await self.stop()

    async def stop(self, idle_timeout=False):
        """Close the OpenAI session and clean up."""
        if not self.active:
            return
        self.active = False

        if idle_timeout:
            try:
                await self.esp32_ws.send(json.dumps({"type": "CHATBOT_STOP", "reason": "idle_timeout"}))
            except Exception:
                pass

        if self.relay_task:
            self.relay_task.cancel()
        if self.idle_checker_task:
            self.idle_checker_task.cancel()
            
        if self.openai_ws and not self.openai_ws.closed:
            await self.openai_ws.close()
            logger.info(f"[Chatbot:{self.client_id}] OpenAI connection closed")

        self.storage_svc.close_transcript(self.client_id)
        self.openai_ws = None

    async def _check_idle_timeout(self):
        """Check for session inactivity."""
        while self.active:
            await asyncio.sleep(10)
            if time.time() - self.last_activity > 300:
                logger.info(f"[Chatbot:{self.client_id}] Session idle timeout (>300s)")
                await self.stop(idle_timeout=True)
                break

    async def _relay_openai_to_esp32(self):
        """Read events from OpenAI and relay audio back to ESP32."""
        try:
            async for message in self.openai_ws:
                if not self.active:
                    break
                
                self.last_activity = time.time()
                try:
                    event = json.loads(message)
                    event_type = event.get("type", "")

                    if event_type == "session.created":
                        logger.info(f"[Chatbot:{self.client_id}] Session created")
                        session_update = self.ai_svc.build_session_config()
                        await self.openai_ws.send(json.dumps(session_update))

                    elif event_type == "response.audio.delta":
                        audio_b64 = event.get("delta", "")
                        if audio_b64:
                            pcm_data = base64.b64decode(audio_b64)
                            try:
                                await self.esp32_ws.send(pcm_data)
                            except websockets.exceptions.ConnectionClosed:
                                break

                    elif event_type == "response.audio.done":
                        try:
                            await self.esp32_ws.send(json.dumps({"type": "CHATBOT_AUDIO_DONE"}))
                        except websockets.exceptions.ConnectionClosed:
                            break

                    elif event_type == "input_audio_buffer.speech_started":
                        try:
                            await self.esp32_ws.send(json.dumps({"type": "CHATBOT_SPEECH_STARTED"}))
                        except websockets.exceptions.ConnectionClosed:
                            break

                    elif event_type == "input_audio_buffer.speech_stopped":
                        try:
                            await self.esp32_ws.send(json.dumps({"type": "CHATBOT_SPEECH_STOPPED"}))
                        except websockets.exceptions.ConnectionClosed:
                            break

                    elif event_type == "response.audio_transcript.done":
                        transcript = event.get('transcript', '')
                        logger.info(f"[Chatbot:{self.client_id}] Assistant: {transcript}")
                        self.storage_svc.write_transcript_event(self.client_id, "Assistant", transcript)

                    elif event_type == "conversation.item.input_audio_transcription.completed":
                        transcript = event.get('transcript', '')
                        logger.info(f"[Chatbot:{self.client_id}] User: {transcript}")
                        self.storage_svc.write_transcript_event(self.client_id, "User", transcript)

                    elif event_type == "response.function_call_arguments.done":
                        name = event.get("name")
                        call_id = event.get("call_id")
                        args = json.loads(event.get("arguments", "{}"))
                        
                        logger.info(f"[Chatbot:{self.client_id}] Function call: {name} => {args}")
                        
                        if name == "get_current_time":
                            now_str = datetime.datetime.now().strftime("%Y-%m-%d %I:%M %p")
                            tool_resp = {
                                "type": "conversation.item.create",
                                "item": {
                                    "type": "function_call_output",
                                    "call_id": call_id,
                                    "output": json.dumps({"current_time": now_str})
                                }
                            }
                            await self.openai_ws.send(json.dumps(tool_resp))
                            await self.openai_ws.send(json.dumps({"type": "response.create"}))
                            
                        elif name == "play_radio":
                            station_name = args.get("station_name", "unknown")
                            # First, send command to ESP32!
                            try:
                                await self.esp32_ws.send(json.dumps({"type": "CHATBOT_ACTION", "action": "play_radio", "station": station_name}))
                            except websockets.exceptions.ConnectionClosed:
                                pass
                            
                            # Let it speak acknowledging the command for a brief moment, then shut down
                            await asyncio.sleep(2)
                            await self.stop(idle_timeout=False)

                    elif event_type == "error":
                        logger.error(f"[Chatbot:{self.client_id}] Error: {event.get('error', {})}")

                    elif event_type in ("session.updated", "response.done"):
                        logger.info(f"[Chatbot:{self.client_id}] {event_type}")

                except json.JSONDecodeError:
                    pass

        except websockets.exceptions.ConnectionClosed:
            logger.info(f"[Chatbot:{self.client_id}] OpenAI connection closed")
        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.exception(f"[Chatbot:{self.client_id}] Relay error: {e}")
