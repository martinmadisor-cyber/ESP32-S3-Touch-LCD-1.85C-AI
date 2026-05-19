from core.config import settings

class RealtimeAIService:
    
    SYSTEM_INSTRUCTIONS = (
        "You are a helpful, friendly voice assistant running on an ESP32 IoT device. "
        "Keep your answers concise and conversational. "
        "The user is speaking to you through a microphone and hearing your responses through a speaker. "
        "Be natural, warm, and helpful. Speak at a fast, energetic pace."
    )
    
    TOOL_INSTRUCTIONS = (
        "\n\nIMPORTANT: You have access to tools that control the ESP32 device. "
        "1. If the user asks to play a radio station (e.g., RMF FM, BBC Oxford, etc.), "
        "you MUST call 'play_radio' with the name of the station. Doing so will transition the device to radio mode and STOP the chatbot session immediately. "
        "Say a quick acknowledgment (e.g., 'Tuning in now!') before calling the tool. "
        "2. If the user asks for the current time or date, call 'get_current_time' to fetch it, "
        "and then speak the time out loud to the user clearly."
    )

    CHATBOT_TOOLS = [
        {
            "name": "play_radio",
            "type": "function",
            "description": "Start playing an internet radio station on the ESP32 device. Use this if user says 'play RMF FM', 'play radio', etc. This stops the AI session.",
            "parameters": {
                "type": "object",
                "properties": {
                    "station_name": {
                        "type": "string", 
                        "description": "Name of the radio station to play (e.g., 'RMF FM', 'BBC Oxford')."
                    }
                },
                "required": ["station_name"]
            }
        },
        {
            "name": "get_current_time",
            "type": "function",
            "description": "Fetch the current local date and time. Use this when the user asks 'what time is it?' or 'what is today's date?'.",
            "parameters": {
                "type": "object",
                "properties": {},
            }
        }
    ]

    def build_session_config(self) -> dict:
        return {
            "type": "session.update",
            "session": {
                "type": "realtime",
                "model": settings.VOICE_CHAT_MODEL,
                "output_modalities": ["audio"],
                "audio": {
                    "input": {
                        "format": {
                            "type": "audio/pcm",
                            "rate": 24000,
                        },
                        "turn_detection": {
                            "type": "server_vad",
                            "threshold": 0.35,
                            "prefix_padding_ms": 500,
                            "silence_duration_ms": 800,
                        }
                    },
                    "output": {
                        "format": {
                            "type": "audio/pcm",
                        },
                        "voice": settings.VOICE,
                    }
                },
                "instructions": self.SYSTEM_INSTRUCTIONS + self.TOOL_INSTRUCTIONS,
                "input_audio_transcription": {
                    "model": "whisper-1",
                },
                "max_response_output_tokens": 400,
                "tools": self.CHATBOT_TOOLS,
            },
        }
