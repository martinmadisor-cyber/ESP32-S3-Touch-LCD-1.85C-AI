import asyncio
import logging

class ChatbotUdpServer(asyncio.DatagramProtocol):
    def __init__(self, ws_routes):
        self.ws_routes = ws_routes
        self.transport = None
        self.active_endpoints = {}

    def connection_made(self, transport):
        self.transport = transport
        logging.info("[UDP] Server listening on port 8767")

    def datagram_received(self, data, addr):
        ip, port = addr
        self.active_endpoints[ip] = addr

        for client_id, session in self.ws_routes.chatbot_sessions.items():
            if session.client_ip == ip and session.active:
                asyncio.create_task(session.send_audio(data))
                break

    def send_to_esp32(self, ip, data):
        addr = self.active_endpoints.get(ip)
        if addr and self.transport:
            self.transport.sendto(data, addr)
