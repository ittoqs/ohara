from fastapi import FastAPI, HTTPException, UploadFile, File
from fastapi.responses import StreamingResponse
from pydantic import BaseModel
import os
import uvicorn
import weaviate
import sqlite3
import fitz  # PyMuPDF
from llama_cpp import Llama
from llama_cpp.llama_chat_format import Llava15ChatHandler
from huggingface_hub import hf_hub_download
import json
from typing import Optional

app = FastAPI(title="Ohara GPT Core Daemon", version="0.1.0")

class ChatRequest(BaseModel):
    message: str
    model_name: str
    filename: str
    system_prompt: str = ""
    session_id: int = 0
    image_base64: str = ""

class DownloadRequest(BaseModel):
    repo_id: str
    filename: str
    mmproj_filename: str = ""

class SessionRequest(BaseModel):
    title: str

# Global state
active_model_name = None
llm_instance = None
weaviate_client = None
DB_PATH = "history.db"

def init_db():
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS sessions (id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP)''')
    c.execute('''CREATE TABLE IF NOT EXISTS messages (id INTEGER PRIMARY KEY AUTOINCREMENT, session_id INTEGER, sender TEXT, text TEXT, image_base64 TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP)''')
    conn.commit()
    conn.close()

def init_weaviate():
    global weaviate_client
    try:
        print("Starting Embedded Weaviate...")
        weaviate_client = weaviate.connect_to_embedded()
        print("Embedded Weaviate connected.")
        if not weaviate_client.collections.exists("Document"):
            weaviate_client.collections.create(name="Document")
    except Exception as e:
        print(f"Failed to start Embedded Weaviate: {e}")

@app.on_event("startup")
def on_startup():
    init_db()
    init_weaviate()

@app.on_event("shutdown")
def on_shutdown():
    if weaviate_client:
        weaviate_client.close()

def load_model(filename: str):
    global active_model_name, llm_instance
    model_path = os.path.join("models", filename)
    if not os.path.exists(model_path):
        raise HTTPException(status_code=404, detail="Model file not found. Please download it first.")
    
    if active_model_name != filename or llm_instance is None:
        print(f"Loading model {filename}...")
        try:
            chat_handler = None
            if "llava" in filename.lower() or "ggml-model-q4_k" in filename.lower():
                mmproj_path = None
                for f in os.listdir("models"):
                    if f.startswith("mmproj") and f.endswith(".gguf"):
                        mmproj_path = os.path.join("models", f)
                        break
                if mmproj_path:
                    chat_handler = Llava15ChatHandler(clip_model_path=mmproj_path)
                    print(f"Loaded Llava Chat Handler with {mmproj_path}")

            llm_instance = Llama(
                model_path=model_path, 
                n_ctx=2048, 
                n_gpu_layers=-1, 
                chat_handler=chat_handler,
                verbose=False
            )
            active_model_name = filename
        except Exception as e:
            raise HTTPException(status_code=500, detail=f"Failed to load model: {e}")

@app.get("/")
def read_root():
    return {"status": "Ohara GPT Core Daemon is running"}

@app.get("/sessions")
def get_sessions():
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("SELECT id, title FROM sessions ORDER BY id DESC")
    rows = c.fetchall()
    conn.close()
    return {"sessions": [{"id": row[0], "title": row[1]} for row in rows]}

@app.post("/session")
def create_session(req: SessionRequest):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("INSERT INTO sessions (title) VALUES (?)", (req.title,))
    session_id = c.lastrowid
    conn.commit()
    conn.close()
    return {"id": session_id, "title": req.title}

@app.get("/session/{session_id}")
def get_session_history(session_id: int):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("SELECT sender, text, image_base64 FROM messages WHERE session_id = ? ORDER BY id ASC", (session_id,))
    rows = c.fetchall()
    conn.close()
    return {"messages": [{"sender": row[0], "text": row[1], "image_base64": row[2] or ""} for row in rows]}

@app.post("/download_model")
def download_model(request: DownloadRequest):
    os.makedirs("models", exist_ok=True)
    try:
        print(f"Downloading {request.filename} from {request.repo_id}...")
        path = hf_hub_download(repo_id=request.repo_id, filename=request.filename, local_dir="models")
        
        if request.mmproj_filename:
            print(f"Downloading {request.mmproj_filename} from {request.repo_id}...")
            hf_hub_download(repo_id=request.repo_id, filename=request.mmproj_filename, local_dir="models")
            
        return {"status": "success", "path": path}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/upload_doc")
async def upload_doc(file: UploadFile = File(...)):
    if not weaviate_client:
        raise HTTPException(status_code=500, detail="Weaviate database is not initialized.")
    
    text = ""
    contents = await file.read()
    if file.filename.endswith(".pdf"):
        doc = fitz.open(stream=contents, filetype="pdf")
        for page in doc:
            text += page.get_text() + "\n"
    elif file.filename.endswith(".txt"):
        text = contents.decode("utf-8")
    else:
        raise HTTPException(status_code=400, detail="Unsupported file format. Please upload PDF or TXT.")

    chunk_size = 1000
    chunks = [text[i:i+chunk_size] for i in range(0, len(text), chunk_size)]
    
    try:
        collection = weaviate_client.collections.get("Document")
        for chunk in chunks:
            if chunk.strip():
                collection.data.insert({"text": chunk, "source": file.filename})
        return {"status": "success", "message": f"Ingested {len(chunks)} chunks from {file.filename}."}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to insert to Weaviate: {e}")

@app.post("/chat")
def chat_endpoint(request: ChatRequest):
    if not request.filename:
        raise HTTPException(status_code=400, detail="Filename must be provided.")
    
    load_model(request.filename)
    
    if request.session_id > 0:
        conn = sqlite3.connect(DB_PATH)
        c = conn.cursor()
        c.execute("INSERT INTO messages (session_id, sender, text, image_base64) VALUES (?, ?, ?, ?)", 
                  (request.session_id, "User", request.message, request.image_base64))
        conn.commit()
        conn.close()

    context = ""
    if weaviate_client and not request.image_base64:
        try:
            collection = weaviate_client.collections.get("Document")
            response = collection.query.bm25(query=request.message, limit=2)
            for obj in response.objects:
                context += obj.properties.get("text", "") + "\n"
        except Exception as e:
            print(f"Weaviate query error: {e}")

    messages = []
    
    if request.system_prompt:
        sys_msg = request.system_prompt
        if context:
            sys_msg += f"\nRelevant context:\n{context}"
        messages.append({"role": "system", "content": sys_msg})
    elif context:
        messages.append({"role": "system", "content": f"Relevant context:\n{context}"})
    
    if request.session_id > 0:
        conn = sqlite3.connect(DB_PATH)
        c = conn.cursor()
        c.execute("SELECT sender, text, image_base64 FROM messages WHERE session_id = ? ORDER BY id DESC LIMIT 6", (request.session_id,))
        rows = c.fetchall()
        conn.close()
        for row in reversed(rows):
            if row[0] == "User":
                if row[1] == request.message and row[2] == request.image_base64:
                    continue
                messages.append({"role": "user", "content": row[1]})
            elif row[0] == "Ohara":
                messages.append({"role": "assistant", "content": row[1]})
                
    if request.image_base64:
        messages.append({
            "role": "user",
            "content": [
                {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{request.image_base64}"}},
                {"type": "text", "text": request.message}
            ]
        })
    else:
        messages.append({"role": "user", "content": request.message})

    def event_stream():
        full_reply = ""
        try:
            output = llm_instance.create_chat_completion(messages=messages, max_tokens=512, stream=True)
            for chunk in output:
                delta = chunk["choices"][0]["delta"]
                if "content" in delta:
                    text_chunk = delta["content"]
                    full_reply += text_chunk
                    yield f"data: {json.dumps({'text': text_chunk})}\n\n"
            
            if request.session_id > 0:
                conn = sqlite3.connect(DB_PATH)
                c = conn.cursor()
                c.execute("INSERT INTO messages (session_id, sender, text, image_base64) VALUES (?, ?, ?, ?)", 
                          (request.session_id, "Ohara", full_reply, ""))
                conn.commit()
                conn.close()
                
            yield "data: [DONE]\n\n"
        except Exception as e:
            yield f"data: {json.dumps({'error': str(e)})}\n\n"

    return StreamingResponse(event_stream(), media_type="text/event-stream")
