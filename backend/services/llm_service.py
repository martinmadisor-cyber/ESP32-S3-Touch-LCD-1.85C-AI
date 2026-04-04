import json
import logging
from openai import OpenAI
from core.config import settings

logger = logging.getLogger(__name__)

class LLMService:
    def __init__(self):
        self.openai_client = OpenAI(api_key=settings.OPENAI_API_KEY)
        
    def transcribe_audio(self, filepath: str) -> str:
        with open(filepath, "rb") as f:
            whisper_result = self.openai_client.audio.transcriptions.create(
                model="whisper-1",
                file=f
            )
            return whisper_result.text

    def get_assistant_reply(self, transcript: str) -> dict:
        prompt = (
              "You are a smart voice assistant running on a low-power ESP32-S3 device with a Raspberry Pi server. "
              "Your user just said the following. Understand and interpret the intent clearly. "
              "If the user asks to play a radio station (e.g., RMF FM, BBC Oxford), respond with a command to do so. "
              "If the user asks for the time, answer it. "
              "Respond in **this strict JSON format**:\n\n"
              "{ \"reply\": \"Your spoken reply here\", \"lang\": \"en\", \"type\": \"command|text|error\", \"action\": \"none|play_radio\", \"action_data\": \"station_name\" }\n\n"
              "Set 'type' to 'command' if you are requesting an action like 'play_radio', else 'text'.\n"
              "Use ISO 639-1 codes for 'lang'.\n"
              f"User said: \"{transcript}\""
        )
        response = self.openai_client.chat.completions.create(
            model="gpt-4",
            messages=[{"role": "user", "content": prompt}]
        )
        
        reply_content = response.choices[0].message.content
        try:
            reply_json = json.loads(reply_content)
            return {
                "reply": reply_json.get("reply", "").strip(),
                "lang": reply_json.get("lang", "en").strip(),
                "type": reply_json.get("type", "text").strip(),
                "action": reply_json.get("action", "none").strip(),
                "action_data": reply_json.get("action_data", "").strip()
            }
        except (json.JSONDecodeError, AttributeError) as e:
            logger.warning(f"[LLMService] Failed to parse response JSON: {e}")
            return {
                "reply": reply_content.strip(),
                "lang": "en",
                "type": "error",
                "action": "none",
                "action_data": ""
            }
