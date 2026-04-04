import os
import json
import asyncio
import pytest
from unittest.mock import Mock, AsyncMock, patch

from api.ws_routes import WSRoutes
from services.storage_service import StorageService
from services.realtime_ai import RealtimeAIService
import queue

class MockWebsocket:
    def __init__(self, messages):
        self.messages = messages
        self.sent_messages = []
        self.closed = False
        
    async def __aiter__(self):
        for msg in self.messages:
            yield msg

    async def send(self, data):
        self.sent_messages.append(data)

    async def close(self):
        self.closed = True

@pytest.fixture
def agent_in_queue():
    return queue.Queue()

@pytest.fixture
def agent_out_queue():
    return queue.Queue()

@pytest.fixture
def mock_storage():
    storage = Mock(spec=StorageService)
    storage.get_filepath.side_effect = lambda f: f"/tmp/{f}"
    return storage

@pytest.fixture
def mock_ai():
    ai = Mock(spec=RealtimeAIService)
    ai.build_session_config.return_value = {"fake": "config"}
    return ai

@pytest.fixture
def ws_router(agent_in_queue, agent_out_queue, mock_storage, mock_ai):
    return WSRoutes(agent_in_queue, agent_out_queue, mock_storage, mock_ai)


@pytest.mark.asyncio
async def test_hello_text_message(ws_router, agent_in_queue):
    ws = MockWebsocket(["HELLO"])
    await ws_router.handle_client(ws)

    assert not agent_in_queue.empty()
    item = agent_in_queue.get()
    assert item["type"] == "HELLO"
    assert "sender" in item

@pytest.mark.asyncio
async def test_start_and_stop_stream(ws_router, agent_in_queue, mock_storage):
    # Simulate JSON start -> binary chunk -> JSON stop
    ws = MockWebsocket([
        json.dumps({"type": "START_STREAM"}),
        b'\x00\x01\x02',  # fake wav binary chunk
        json.dumps({"type": "STOP_STREAM"}),
    ])
    
    # We must patch AudioStreamSession since it tries to open real files
    with patch("api.ws_routes.AudioStreamSession") as mock_audio_session_class:
        mock_session_inst = Mock()
        mock_audio_session_class.return_value = mock_session_inst
        mock_session_inst.filepath = "/tmp/fake.wav"

        await ws_router.handle_client(ws)
        
        # Verify a session was created and chunk was written
        mock_audio_session_class.assert_called_once()
        mock_session_inst.write_chunk.assert_called_with(b'\x00\x01\x02')
        mock_session_inst.close.assert_called_once()
        
        # Verify the file was posted to agent_queue
        assert not agent_in_queue.empty()
        item = agent_in_queue.get()
        assert item["type"] == "ASSISTANT_PROCESS_WAVE"
        assert item["fname"] == "/tmp/fake.wav"
        
        # Ensure client was removed from connected state
        assert ws not in ws_router.connected_clients

@pytest.mark.asyncio
async def test_chatbot_start_and_stop(ws_router):
    ws = MockWebsocket([
        json.dumps({"type": "CHATBOT_START"}),
        b'\x01\x02\x03', # PCM audio chunk
        json.dumps({"type": "CHATBOT_STOP"}),
    ])
    
    with patch("api.ws_routes.ChatbotSession") as mock_chatbot_class:
        mock_chatbot_inst = AsyncMock()
        mock_chatbot_inst.active = True
        mock_chatbot_inst.start.return_value = True
        mock_chatbot_class.return_value = mock_chatbot_inst
        
        await ws_router.handle_client(ws)
        
        mock_chatbot_inst.start.assert_awaited_once()
        mock_chatbot_inst.send_audio.assert_awaited_with(b'\x01\x02\x03')
        mock_chatbot_inst.stop.assert_awaited_once()

@pytest.mark.asyncio
async def test_generic_json(ws_router, agent_in_queue):
    ws = MockWebsocket([
        json.dumps({"type": "SOME_CUSTOM_COMMAND", "payload": "data"}),
    ])
    await ws_router.handle_client(ws)

    assert not agent_in_queue.empty()
    item = agent_in_queue.get()
    assert item["type"] == "SOME_CUSTOM_COMMAND"
    assert item["payload"] == "data"
