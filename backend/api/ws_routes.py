import os
import json
import logging
import asyncio
import websockets
from services.storage_service import StorageService, AudioStreamSession
from api.chatbot_session import ChatbotSession
from services.realtime_ai import RealtimeAIService

class WSRoutes:
    def __init__(self, agent_in_queue, agent_out_queue, storage_svc: StorageService, ai_svc: RealtimeAIService):
        self.agent_in_queue = agent_in_queue
        self.agent_out_queue = agent_out_queue
        self.storage_svc = storage_svc
        self.ai_svc = ai_svc
        
        self.connected_clients = {}  # {websocket: last_hello_timestamp}
        self.stream_sessions = {}    # {client_id: AudioStreamSession}
        self.chatbot_sessions = {}   # {client_id: ChatbotSession}
        self.udp_server = None       # Attached by main.py

    async def handle_client(self, websocket):
        client_id = str(id(websocket))
        logging.info(f"[WS] Client connected: {client_id}")
        self.connected_clients[websocket] = asyncio.get_event_loop().time()

        try:
            async for message in websocket:
                if isinstance(message, bytes):
                    chatbot = self.chatbot_sessions.get(client_id)
                    if chatbot and chatbot.active:
                        await chatbot.send_audio(message)
                        continue

                    session = self.stream_sessions.get(client_id)
                    if session:
                        session.write_chunk(message)
                    else:
                        logging.warning(f"[WS] Binary from {client_id} but no active session")
                    continue

                if isinstance(message, str) and message.strip().startswith("{"):
                    try:
                        parsed = json.loads(message)
                        parsed["sender"] = client_id
                        msg_type = parsed.get("type")

                        if msg_type == "CHATBOT_START":
                            logging.info(f"[WS] CHATBOT_START from {client_id}")
                            if client_id in self.chatbot_sessions:
                                await self.chatbot_sessions[client_id].stop()
                            
                            downsample_to = parsed.get("downsample_to")
                            session = ChatbotSession(websocket, client_id, self.storage_svc, self.ai_svc, self.udp_server, downsample_to=downsample_to)
                            ok = await session.start()
                            if ok:
                                self.chatbot_sessions[client_id] = session
                            continue

                        elif msg_type == "CHATBOT_STOP":
                            logging.info(f"[WS] CHATBOT_STOP from {client_id}")
                            chatbot = self.chatbot_sessions.pop(client_id, None)
                            if chatbot:
                                await chatbot.stop()
                            continue

                        elif msg_type == "START_STREAM":
                            logging.info(f"[WS] START_STREAM for {client_id}")
                            filepath = self.storage_svc.get_filepath(f"stream_{client_id}.wav")
                            if os.path.exists(filepath):
                                os.remove(filepath)
                            self.stream_sessions[client_id] = AudioStreamSession(filepath)
                            parsed["filepath"] = filepath

                        elif msg_type == "STOP_STREAM":
                            logging.info(f"[WS] STOP_STREAM for {client_id}")
                            session = self.stream_sessions.pop(client_id, None)
                            if session:
                                session.close()
                                self.agent_in_queue.put({
                                    "type": "ASSISTANT_PROCESS_WAVE",
                                    "fname": session.filepath,
                                    "sender": client_id
                                })

                        else:
                            self.agent_in_queue.put(parsed)

                    except json.JSONDecodeError as e:
                        logging.warning(f"[WS] Invalid JSON from {client_id}: {e}")
                    continue

                # Raw text
                self.agent_in_queue.put({"sender": client_id, "type": message})

        except websockets.exceptions.ConnectionClosed:
            logging.info(f"[WS] Client disconnected: {client_id}")
        finally:
            self.connected_clients.pop(websocket, None)
            session = self.stream_sessions.pop(client_id, None)
            if session:
                session.close()
            chatbot = self.chatbot_sessions.pop(client_id, None)
            if chatbot:
                await chatbot.stop()

    async def ping_clients(self, shutdown_event):
        while not shutdown_event.is_set():
            try:
                await asyncio.sleep(60)
            except asyncio.CancelledError:
                break
            for ws in list(self.connected_clients.keys()):
                pass

    async def dispatch_agent_responses(self, shutdown_event):
        import queue
        while not shutdown_event.is_set():
            try:
                msg = self.agent_out_queue.get_nowait()
                sender = msg.get("sender")
                payload = json.dumps(msg)

                if not sender or sender == "None":
                    for ws in list(self.connected_clients.keys()):
                        try:
                            await ws.send(payload)
                        except Exception:
                            pass
                else:
                    for ws in list(self.connected_clients.keys()):
                        if str(id(ws)) == str(sender):
                            await ws.send(payload)
                            break
            except queue.Empty:
                await asyncio.sleep(0.1)

    async def cleanup(self):
        for ws in list(self.connected_clients.keys()):
            await ws.close()
