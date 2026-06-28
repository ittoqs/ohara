# Ohara GPT

<div align="center">

*Offline · Privat · Embedded C++ Inference*

</div>

---

## ✨ Features

### 🧠 Fully Embedded C++ Inference
Ohara GPT runs Large Language Models (LLMs) **directly in the application** using [llama.cpp](https://github.com/ggerganov/llama.cpp)

### 🔍 Smart Hardware Detection
Automatically detects your system's **RAM, CPU, GPU (NVIDIA/AMD/Apple Metal)**, and available disk space. Recommends the optimal model and context size for your hardware to prevent crashes and maximize performance.

### 🎮 Multi-GPU Support
- **NVIDIA CUDA** — Automatic GPU offloading for maximum speed
- **AMD ROCm / Vulkan** — Cross-vendor GPU acceleration
- **Apple Metal** — Native Apple Silicon acceleration
- **CPU-only** — Runs great on any machine with sufficient RAM

### 🤖 Built-in Model Catalog
Pre-configured models optimized for different tasks, downloadable directly from the GUI:

| Model | Type | Min RAM | Description |
|-------|------|---------|-------------|
| TinyLlama 1.1B | Testing | 2 GB | Ultra-fast testing model |
| Phi-3 Mini | Text (Light) | 4 GB | Efficient for low-spec devices |
| Llama 3 8B | General | 8 GB | Advanced conversational AI |
| Llava 1.5 7B | Vision+Text | 8 GB | Image + text understanding |
| Qwen 2.5 Coder 7B | Coding | 8 GB | Expert programming assistant |
| DeepSeek R1 7B | Reasoning | 8 GB | Chain-of-thought reasoning |
| Qwen 2.5 Coder 14B | Coding (Large) | 16 GB | Advanced software development |

### 📄 Local RAG (Document Chat)
Upload TXT, MD, CSV, or JSON files — they're automatically chunked and indexed using **SQLite FTS5**.

### 🔒 Privacy First (Offline-First)
All inference and data processing happen entirely on your local machine. **Zero telemetry**, zero cloud dependencies. Your data never leaves your device.

### 🎨 Dark GUI
Built with **Qt6/QML**, featuring:
- Deep navy glassmorphism-inspired design
- Smooth animations and transitions
- Responsive layout (sidebar auto-collapses on small screens)
- Settings drawer with visual sliders
- Live status bar (model, RAM, tokens/sec)

---

## 🚀 Getting Started

### Prerequisites
- **CMake** 3.16+
- **Qt6** (Core, Gui, Qml, Quick, Network, Sql, Widgets)
- **C++17** compiler (GCC 9+, Clang 10+, MSVC 2019+)
- **Git** (for FetchContent)

> **Note**: llama.cpp is automatically downloaded and built by CMake. No manual setup needed.

### Build

```bash
# Clone
[git clone https://github.com/ittoqs/ohara.git)
cd ohara

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run
./build/ohara-gui/ohara_gpt
```

### GPU Acceleration

GPU support is auto-detected during build:
- **NVIDIA**: Install CUDA Toolkit → CMake enables CUDA automatically
- **AMD/Intel**: Install Vulkan SDK → CMake enables Vulkan automatically
- **macOS**: Metal is enabled by default on Apple Silicon

---

## 🏗️ Architecture

```
ohara/
├── CMakeLists.txt              # Root build
├── ohara-gui/
│   ├── CMakeLists.txt          # GUI build + llama.cpp FetchContent
│   ├── src/
│   │   ├── main.cpp            # Application entry + component wiring
│   │   ├── inference_engine.*  # llama.cpp wrapper (model loading, generation)
│   │   ├── database_manager.*  # SQLite WAL + FTS5 (sessions, messages, RAG)
│   │   ├── model_manager.*     # HuggingFace model download + management
│   │   ├── settings_manager.*  # User prefs, i18n, config persistence
│   │   ├── hardware_detector.* # CPU/RAM/GPU/Disk detection
│   │   └── document_processor.*# Document → FTS5 indexing for RAG
│   └── qml/
│       └── main.qml            # Complete enterprise GUI
└── ohara-backend/              # [Deprecated] Python backend (reference only)
```

**Key Design Decisions:**
- **Fully embedded** — No separate Python process. All inference runs in-process via llama.cpp C API
- **SQLite FTS5** — Replaced Weaviate (~300MB RAM) with lightweight full-text search
- **Qt Signals/Slots** — Thread-safe communication between inference thread and GUI
- **Cross-platform** — Windows, macOS, Linux from a single codebase

---
