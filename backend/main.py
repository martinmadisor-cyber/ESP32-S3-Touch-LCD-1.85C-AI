import sys
import queue
import signal
import logging
import asyncio
import threading
import websockets
from uvicorn import Config, Server

from core.config import settings
from services.storage_service import StorageService
from services.llm_service import LLMService
from services.realtime_ai import RealtimeAIService
from workers.agent_worker import AgentWorker
from api.http_routes import create_app
from api.ws_routes import WSRoutes


def run_fastapi_server(app, shutdown_event):
    """Run FastAPI/uvicorn server in a secondary thread."""
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    config = Config(app=app, host="0.0.0.0", port=8766, log_level="info")
    server = Server(config)
    
    async def serve_with_shutdown():
        serve_task = asyncio.create_task(server.serve())
        while not shutdown_event.is_set():
            await asyncio.sleep(1)
        logging.info("[HTTP] Stopping FastAPI server...")
        server.should_exit = True
        await serve_task

    logging.info("[HTTP] Starting FastAPI server at http://0.0.0.0:8766")
    loop.run_until_complete(serve_with_shutdown())
    loop.close()


async def main():
    # 1. Initialize Threading & Queues
    shutdown_event = threading.Event()
    agent_in_queue = queue.Queue()
    agent_out_queue = queue.Queue()

    # 2. Initialize Services (Dependency Injection)
    storage_svc = StorageService()
    llm_svc = LLMService()
    ai_svc = RealtimeAIService()

    # 3. Setup Workers
    worker = AgentWorker(
        in_queue=agent_in_queue,
        out_queue=agent_out_queue,
        llm_service=llm_svc,
        shutdown_event=shutdown_event
    )
    agent_thread = threading.Thread(target=worker.run, daemon=False)

    # 4. Setup FastAPI
    app = create_app(agent_in_queue, storage_svc)
    fastapi_thread = threading.Thread(
        target=run_fastapi_server, 
        args=(app, shutdown_event),
        daemon=True
    )

    # 5. Setup WebSockets
    ws_router = WSRoutes(agent_in_queue, agent_out_queue, storage_svc, ai_svc)

    # Signal handling inside async loop
    def handle_signal():
        logging.info("[SYS] Shutdown signal detected.")
        shutdown_event.set()
        agent_in_queue.put(None)  # unblock worker

    loop = asyncio.get_running_loop()
    if sys.platform != "win32":
        loop.add_signal_handler(signal.SIGINT, handle_signal)
        loop.add_signal_handler(signal.SIGTERM, handle_signal)

    # Start Threads
    fastapi_thread.start()
    agent_thread.start()

    # Start WS Server
    ws_server = await websockets.serve(ws_router.handle_client, "0.0.0.0", 8765, max_size=2**20)
    logging.info("[WS] Server started at ws://0.0.0.0:8765")

    pinger = asyncio.create_task(ws_router.ping_clients(shutdown_event))
    dispatcher = asyncio.create_task(ws_router.dispatch_agent_responses(shutdown_event))

    # Wait for shutdown signal
    while not shutdown_event.is_set():
        await asyncio.sleep(0.5)

    logging.info("[WS] Shutting down WebSocket server...")
    ws_server.close()
    await ws_server.wait_closed()
    
    pinger.cancel()
    dispatcher.cancel()
    
    await ws_router.cleanup()
    
    try:
        await asyncio.gather(pinger, dispatcher, return_exceptions=True)
    except Exception:
        pass
    
    logging.info("[SYS] Waiting for agent to stop...")
    agent_thread.join()
    fastapi_thread.join()
    logging.info("[SYS] Server shutdown complete.")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
