import os
from fastapi import FastAPI, UploadFile, File, Form
from fastapi.responses import JSONResponse, FileResponse, HTMLResponse
from services.storage_service import StorageService

def create_app(in_queue, storage_service: StorageService):
    app = FastAPI()

    @app.post("/upload")
    async def upload_wav(
        file: UploadFile = File(...),
        session_id: str = Form(None)
    ):
        try:
            filename = os.path.basename(file.filename)
            path, size = storage_service.save_upload(file, filename)

            in_queue.put({
                "type": "ASSISTANT_PROCESS_WAVE",
                "fname": path,
                "sender": session_id if session_id else None,
            })
            return JSONResponse(content={"status": "ok", "filename": filename, "size": size})
        except Exception as e:
            return JSONResponse(status_code=500, content={"status": "error", "message": str(e)})

    @app.get("/", response_class=HTMLResponse)
    async def serve_index():
        files = storage_service.list_recordings()
        html = f"""
    <html>
    <head><style>body {{ font-family: sans-serif; padding: 20px; }}</style></head>
    <body>
        <h1>Recorded WAV Files</h1>
        <div style="display:flex; flex-direction:column; gap:10px;">
        {"".join(f'<div style="padding:10px; border:1px solid #ccc; border-radius:5px;"><strong>{f}</strong><br/><audio controls style="margin-top:5px;"><source src="/recordings/{f}" type="audio/wav"></audio></div>' for f in files)}
        </div>
    </body>
    </html>
        """
        return HTMLResponse(content=html)

    @app.get("/recordings")
    async def list_recordings():
        return {"files": storage_service.list_recordings()}

    @app.get("/recordings/{filename}")
    async def get_recording(filename: str):
        filepath = storage_service.get_filepath(filename)
        if not os.path.isfile(filepath):
            return JSONResponse(status_code=404, content={"error": "File not found"})
        return FileResponse(filepath, media_type="audio/wav")

    return app
