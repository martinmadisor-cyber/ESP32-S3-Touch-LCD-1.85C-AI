import os
import logging

class Settings:
    OPENAI_API_KEY: str = os.getenv("OPENAI_API_KEY", "")
    OPENAI_REALTIME_URL: str = "wss://api.openai.com/v1/realtime?model=gpt-realtime-2"
    VOICE_CHAT_MODEL: str = "gpt-realtime"
    TRANSCRIPTION_MODEL: str = "gpt-4o-transcribe"
    VOICE: str = "verse"
    UPLOAD_DIR: str = "./recordings"

settings = Settings()
logging.basicConfig(level=logging.INFO)
