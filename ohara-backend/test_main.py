from fastapi.testclient import TestClient
from main import app

client = TestClient(app)

def test_read_root():
    response = client.get("/")
    assert response.status_code == 200
    assert response.json() == {"status": "Ohara GPT Core Daemon is running"}

def test_chat_endpoint():
    request_data = {
        "message": "Halo",
        "model_name": "Llama-3-8B-Instruct",
        "system_prompt": "Test"
    }
    response = client.post("/chat", json=request_data)
    assert response.status_code == 200
    json_resp = response.json()
    assert json_resp["status"] == "success"
    assert "[ECHO from Backend]" in json_resp["reply"]
    assert "Llama-3-8B-Instruct" in json_resp["reply"]
