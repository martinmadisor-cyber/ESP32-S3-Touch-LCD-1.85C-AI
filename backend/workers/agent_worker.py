import logging
import openai
from queue import Empty

logger = logging.getLogger(__name__)

class AgentWorker:
    def __init__(self, in_queue, out_queue, llm_service, shutdown_event):
        self.in_queue = in_queue
        self.out_queue = out_queue
        self.llm_service = llm_service
        self.shutdown_event = shutdown_event

    def _process_task(self, task):
        msg_type = task.get("type")
        sender = task.get("sender")
        lang = task.get("lang", "en")
        mode = task.get("mode", "chat")

        if msg_type == "HELLO":
            logger.info(f"[Agent] Processing HELLO from sender: {sender}")
            self.out_queue.put({
                "type": "HELLO",
                "content": "Hello back from server.",
                "language": "en",
                "mode": "",
                "sender": sender
            })
            return

        if msg_type == "ASSISTANT_PROCESS_WAVE":
            filename = task.get("fname")
            logger.info(f"[Agent] Processing {filename} from sender: {sender}")
            try:
                # Transcribe
                transcript = self.llm_service.transcribe_audio(filename)
                logger.info(f"[Whisper] Transcript: {transcript}")

                # Ask GPT
                result = self.llm_service.get_assistant_reply(transcript)
                logger.info(f"[GPT-4] Response: {result['reply']}")

                self.out_queue.put({
                    "type": "ASSISTANT_TEXT_RESPONSE",
                    "content": result["reply"],
                    "language": result["lang"],
                    "mode": mode,
                    "sender": str(sender),
                    "action": result.get("action", "none"),
                    "action_data": result.get("action_data", "")
                })

            except openai.RateLimitError:
                logger.error("[Agent] Rate limit or quota exceeded.")
                if sender:
                    self.out_queue.put({
                        "sender": sender,
                        "type": "ASSISTANT_TEXT_RESPONSE",
                        "content": "[ERROR] Rate limit exceeded."
                    })
            except Exception as e:
                logger.exception("[Agent] Failed to process audio task")
                if sender:
                    self.out_queue.put({
                        "sender": sender,
                        "type": "ASSISTANT_TEXT_RESPONSE",
                        "content": "[ERROR] Failed to process audio."
                    })

    def run(self):
        logger.info("[AgentWorker] Agent thread started")
        while not self.shutdown_event.is_set():
            try:
                task = self.in_queue.get(timeout=1)
                if task:
                    self._process_task(task)
            except Empty:
                continue
            except Exception:
                logger.exception("[AgentWorker] Fatal error")
        logger.info("[AgentWorker] Agent thread finished")
