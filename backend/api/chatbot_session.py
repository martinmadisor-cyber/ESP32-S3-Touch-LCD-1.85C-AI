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
import audioop
import websockets
from core.config import settings
from services.storage_service import StorageService
from services.realtime_ai import RealtimeAIService

logger = logging.getLogger("chatbot")

class ChatbotSession:
    """Manages an OpenAI Realtime API connection for one ESP32 client."""

    def __init__(self, esp32_ws, client_id: str, storage_svc: StorageService, ai_svc: RealtimeAIService, udp_server=None):
        self.esp32_ws = esp32_ws
        self.client_id = client_id
        self.client_ip = esp32_ws.remote_address[0] if esp32_ws.remote_address else "0.0.0.0"
        self.udp_server = udp_server
        self.storage_svc = storage_svc
        self.ai_svc = ai_svc
        
        self.openai_ws = None
        self.relay_task = None
        self.idle_checker_task = None
        self.audio_task = None
        self.audio_queue = asyncio.Queue()
        self.active = False
        self.last_activity = time.time()
        self.up_state = None
        self.down_state = None

    def _is_openai_ws_open(self) -> bool:
        """Version-agnostic check if OpenAI websocket is open."""
        if not self.openai_ws:
            return False
        # Modern websockets (13.0+) uses .open property or .state
        if hasattr(self.openai_ws, "open"):
            return self.openai_ws.open
        # Fallback for some versions of websockets.asyncio
        if hasattr(self.openai_ws, "state"):
            return str(self.openai_ws.state).endswith("OPEN")
        return True

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
            self.audio_task = asyncio.create_task(self._send_audio_to_esp32())

            # Notify ESP32
            await self.esp32_ws.send(json.dumps({"type": "CHATBOT_READY"}))
            return True

        except Exception as e:
            logger.exception(f"[Chatbot:{self.client_id}] Failed to connect to OpenAI: {e}")
            self.active = False
            return False

    async def send_audio(self, pcm_data: bytes):
        """Forward binary PCM16 audio from ESP32 to OpenAI."""
        if not self.active or not self.openai_ws or not self._is_openai_ws_open():
            return
        
        self.last_activity = time.time()
        try:
            # Upsample 16kHz from ESP32 to 24kHz for OpenAI
            pcm_24k, self.up_state = audioop.ratecv(pcm_data, 2, 1, 16000, 24000, self.up_state)
            audio_b64 = base64.b64encode(pcm_24k).decode("utf-8")
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
        if self.audio_task:
            self.audio_task.cancel()
            
        if self.openai_ws and self._is_openai_ws_open():
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

    async def _send_audio_to_esp32(self):
        """Pulls audio from the queue and sends to ESP32 at a controlled bitrate (1.1x realtime)."""
        is_transmitting = False
        while self.active:
            try:
                # Wait for audio with a short timeout to detect end of speech
                try:
                    pcm_data = await asyncio.wait_for(self.audio_queue.get(), timeout=0.15)
                    
                    if not is_transmitting:
                        is_transmitting = True
                        await self.esp32_ws.send(json.dumps({"type": "CHATBOT_TURN_START"}))
                        logger.info(f"[Chatbot:{self.client_id}] Turn Start -> Muting ESP32 Mic")
                        
                    if not self.active: break
                    
                    CHUNK_SIZE = 1024
                    for i in range(0, len(pcm_data), CHUNK_SIZE):
                        if not self.active: break
                        chunk = pcm_data[i:i+CHUNK_SIZE]
                        
                        await self.esp32_ws.send(chunk)
                        
                        sleep_time = (len(chunk) / 48000.0) * 0.9
                        await asyncio.sleep(sleep_time)
                except asyncio.TimeoutError:
                    if is_transmitting:
                        is_transmitting = False
                        await self.esp32_ws.send(json.dumps({"type": "CHATBOT_TURN_STOP"}))
                        logger.info(f"[Chatbot:{self.client_id}] Turn Stop -> Unmuting ESP32 Mic")

            except websockets.exceptions.ConnectionClosed:
                break
            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error(f"[Chatbot:{self.client_id}] Audio send err: {e}")

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

                        # Trigger a brief introductory greeting from the AI immediately
                        greeting_event = {
                            "type": "conversation.item.create",
                            "item": {
                                "type": "message",
                                "role": "system",
                                "content": [
                                    {
                                        "type": "input_text",
                                        "text": "Introduce yourself warmly to the user in 1 short sentence."
                                    }
                                ]
                            }
                        }
                        await self.openai_ws.send(json.dumps(greeting_event))
                        await self.openai_ws.send(json.dumps({"type": "response.create"}))

                    elif event_type == "response.output_audio.delta":
                        audio_b64 = event.get("delta", "")
                        if audio_b64:
                            pcm_data = base64.b64decode(audio_b64)
                            # Downsample 24kHz to 16kHz for stable ESP32 DAC playback
                            pcm_data, self.down_state = audioop.ratecv(pcm_data, 2, 1, 24000, 16000, self.down_state)
                            self.audio_queue.put_nowait(pcm_data)

                    elif event_type == "response.output_audio.done":
                        try:
                            await self.esp32_ws.send(json.dumps({"type": "CHATBOT_AUDIO_DONE"}))
                        except websockets.exceptions.ConnectionClosed:
                            break

                    elif event_type == "input_audio_buffer.speech_started":
                        # Clear any stale outgoing AI audio so it immediately stops talking!
                        while not self.audio_queue.empty():
                            try:
                                self.audio_queue.get_nowait()
                            except asyncio.QueueEmpty:
                                break
                        try:
                            await self.esp32_ws.send(json.dumps({"type": "CHATBOT_SPEECH_STARTED"}))
                        except websockets.exceptions.ConnectionClosed:
                            break

                    elif event_type == "input_audio_buffer.speech_stopped":
                        try:
                            await self.esp32_ws.send(json.dumps({"type": "CHATBOT_SPEECH_STOPPED"}))
                        except websockets.exceptions.ConnectionClosed:
                            break

                    elif event_type == "response.output_audio_transcript.done":
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
