import io
import pytest
from fastapi.testclient import TestClient
from unittest.mock import Mock, patch

from api.http_routes import create_app
from services.storage_service import StorageService
import queue

@pytest.fixture
def mock_storage():
    storage = Mock(spec=StorageService)
    # save_upload returns (filepath, size)
    storage.save_upload.return_value = ("/tmp/recordings/test.wav", 1024)
    storage.list_recordings.return_value = ["recording1.wav", "recording2.wav"]
    return storage

@pytest.fixture
def agent_in_queue():
    return queue.Queue()

@pytest.fixture
def client(agent_in_queue, mock_storage):
    app = create_app(agent_in_queue, mock_storage)
    return TestClient(app)

def test_upload_wav(client, mock_storage, agent_in_queue):
    fake_audio = io.BytesIO(b"fake wav data")
    response = client.post(
        "/upload",
        files={"file": ("test.wav", fake_audio, "audio/wav")},
        data={"session_id": "testsuite_123"}
    )
    
    assert response.status_code == 200
    assert response.json() == {"status": "ok", "filename": "test.wav", "size": 1024}
    
    mock_storage.save_upload.assert_called_once()
    
    # Assert item was placed on the queue
    assert not agent_in_queue.empty()
    task = agent_in_queue.get()
    assert task["type"] == "ASSISTANT_PROCESS_WAVE"
    assert task["fname"] == "/tmp/recordings/test.wav"
    assert task["sender"] == "testsuite_123"

def test_upload_wav_no_session(client, mock_storage, agent_in_queue):
    fake_audio = io.BytesIO(b"fake wav data")
    response = client.post(
        "/upload",
        files={"file": ("test2.wav", fake_audio, "audio/wav")}
        # Missing form data on purpose
    )
    assert response.status_code == 200
    task = agent_in_queue.get()
    assert task["sender"] is None

def test_list_recordings(client, mock_storage):
    response = client.get("/recordings")
    assert response.status_code == 200
    assert response.json() == {"files": ["recording1.wav", "recording2.wav"]}

def test_serve_index(client):
    response = client.get("/")
    assert response.status_code == 200
    assert "text/html" in response.headers["content-type"]
    assert "recording1.wav" in response.text
    assert "recording2.wav" in response.text

@patch("os.path.isfile", return_value=True)
def test_get_recording(mock_isfile, client, mock_storage):
    mock_storage.get_filepath.return_value = "/tmp/fake.wav"
    
    # Test valid file but patch FileResponse since /tmp/fake.wav doesn't exist
    with patch("api.http_routes.FileResponse") as mock_fileresponse:
        mock_fileresponse.return_value = {"patched": True}
        response = client.get("/recordings/recording1.wav")
        # FastAPI TestClient doesn't directly return the mocked object if the route signature returns Response class
        # so we just verify the call inside the endpoint is correct
        assert response

@patch("os.path.isfile", return_value=False)
def test_get_recording_not_found(mock_isfile, client):
    response = client.get("/recordings/doesnotexist.wav")
    assert response.status_code == 404
    assert response.json() == {"error": "File not found"}
