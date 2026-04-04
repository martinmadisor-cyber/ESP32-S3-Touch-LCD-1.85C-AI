import os
import shutil
import logging
from core.config import settings

logger = logging.getLogger(__name__)

class StorageService:
    def __init__(self):
        os.makedirs(settings.UPLOAD_DIR, exist_ok=True)

    def save_upload(self, upload_file, filename: str) -> tuple[str, int]:
        path = os.path.join(settings.UPLOAD_DIR, filename)
        with open(path, "wb") as f:
            shutil.copyfileobj(upload_file.file, f)
        size = os.path.getsize(path)
        return path, size

    def list_recordings(self) -> list[str]:
        return [f for f in os.listdir(settings.UPLOAD_DIR) if f.endswith(".wav")]

    def get_filepath(self, filename: str) -> str:
        return os.path.join(settings.UPLOAD_DIR, filename)

    def write_transcript_event(self, client_id: str, label: str, text: str):
        path = os.path.join(settings.UPLOAD_DIR, f"chatbot_transcript_{client_id}.txt")
        with open(path, "a", encoding="utf-8") as f:
            f.write(f"{label}: {text}\n")
            
    def init_transcript(self, client_id: str):
        path = os.path.join(settings.UPLOAD_DIR, f"chatbot_transcript_{client_id}.txt")
        with open(path, "a", encoding="utf-8") as f:
            f.write(f"--- Chatbot Session Started: {client_id} ---\n")
            
    def close_transcript(self, client_id: str):
        path = os.path.join(settings.UPLOAD_DIR, f"chatbot_transcript_{client_id}.txt")
        with open(path, "a", encoding="utf-8") as f:
            f.write("--- Chatbot Session Ended ---\n\n")

class AudioStreamSession:
    def __init__(self, filepath: str, sample_rate=16000, num_channels=1, bits_per_sample=16):
        self.filepath = filepath
        self.data_length = 0
        try:
            self.wave_file = open(filepath, "wb")
        except Exception as e:
            logger.exception(f"[AudioStreamSession] Failed to initialize WAV stream: {e}")
            raise

    def write_chunk(self, data: bytes):
        try:
            self.wave_file.write(data)
            self.wave_file.flush()
            os.fsync(self.wave_file.fileno())
            self.data_length += len(data)
        except Exception as e:
            logger.exception(f"[AudioStreamSession] Failed to write audio chunk: {e}")

    def close(self):
        try:
            if not self.wave_file.closed:
                self.wave_file.close()
                logger.info(f"[AudioStreamSession] WAV file closed: {self.filepath} ({self.data_length} bytes data)")
        except Exception as e:
            logger.exception(f"[AudioStreamSession] Failed to close WAV file: {e}")
