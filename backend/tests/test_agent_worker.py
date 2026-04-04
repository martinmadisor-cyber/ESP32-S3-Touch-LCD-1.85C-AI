import pytest
import threading
import queue
from unittest.mock import Mock, patch
from workers.agent_worker import AgentWorker
from services.llm_service import LLMService
from openai import RateLimitError


@pytest.fixture
def agent_in_queue():
    return queue.Queue()

@pytest.fixture
def agent_out_queue():
    return queue.Queue()

@pytest.fixture
def shutdown_event():
    return threading.Event()

@pytest.fixture
def mock_llm_service():
    llm = Mock(spec=LLMService)
    llm.transcribe_audio.return_value = "Test transcript"
    llm.get_assistant_reply.return_value = {"reply": "Test reply", "lang": "en", "type": "text"}
    return llm

@pytest.fixture
def worker(agent_in_queue, agent_out_queue, mock_llm_service, shutdown_event):
    return AgentWorker(agent_in_queue, agent_out_queue, mock_llm_service, shutdown_event)

def test_process_hello(worker, agent_out_queue):
    task = {"type": "HELLO", "sender": "client123"}
    worker._process_task(task)
    
    assert not agent_out_queue.empty()
    response = agent_out_queue.get()
    assert response["type"] == "HELLO"
    assert response["sender"] == "client123"

def test_process_assistant_wave(worker, agent_out_queue, mock_llm_service):
    task = {"type": "ASSISTANT_PROCESS_WAVE", "fname": "/tmp/test.wav", "sender": "client123", "mode": "chat"}
    worker._process_task(task)
    
    mock_llm_service.transcribe_audio.assert_called_once_with("/tmp/test.wav")
    mock_llm_service.get_assistant_reply.assert_called_once_with("Test transcript")
    
    assert not agent_out_queue.empty()
    response = agent_out_queue.get()
    assert response["type"] == "ASSISTANT_TEXT_RESPONSE"
    assert response["content"] == "Test reply"
    assert response["sender"] == "client123"

def test_process_assistant_wave_rate_limit(worker, agent_out_queue, mock_llm_service):
    # Setup mock to simulate RateLimitError wrapping a fake response
    rate_error = RateLimitError("Rate limit", response=Mock(), body={"error": {}})
    mock_llm_service.transcribe_audio.side_effect = rate_error
    
    task = {"type": "ASSISTANT_PROCESS_WAVE", "fname": "/tmp/test.wav", "sender": "client123", "mode": "chat"}
    worker._process_task(task)
    
    assert not agent_out_queue.empty()
    response = agent_out_queue.get()
    assert "Rate limit exceeded" in response["content"]
    assert response["sender"] == "client123"

def test_process_assistant_wave_generic_exception(worker, agent_out_queue, mock_llm_service):
    mock_llm_service.transcribe_audio.side_effect = Exception("General Error")
    
    task = {"type": "ASSISTANT_PROCESS_WAVE", "fname": "/tmp/test.wav", "sender": "client123", "mode": "chat"}
    worker._process_task(task)
    
    assert not agent_out_queue.empty()
    response = agent_out_queue.get()
    assert "Failed to process audio" in response["content"]
    assert response["sender"] == "client123"

def test_run_loop(worker, agent_in_queue, shutdown_event):
    # Queue a HELLO task, then signal shutdown so it breaks the loop
    task = {"type": "HELLO", "sender": "client123"}
    agent_in_queue.put(task)
    
    # We must patch _process_task to also set shutdown_event after processing one item 
    # to avoid an infinite loop in tests if the queue gets empty or hangs
    original_process = worker._process_task
    def hooked_process(t):
        original_process(t)
        shutdown_event.set()
        
    with patch.object(worker, '_process_task', side_effect=hooked_process):
        worker.run()
        
    # If we reached here, the loop terminated properly
    assert shutdown_event.is_set()
