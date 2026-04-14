# Maze Pupper 2 – AI Telemetry Dashboard

An integrated system where a C‑based maze solver (A* algorithm) sends telemetry to Redis, which is then displayed on a live dashboard. A Python bridge connects Redis to a web UI, and an Ollama‑powered AI assistant (Llama 3) answers questions about the maze and system metrics.

## Architecture Overview

```

Maze Program (C + A* + SDL)  ──►  Redis  ◄──  Python Bridge (Flask)
│
▼
Web Dashboard (HTML/JS)
│
▼
Ollama (Llama 3 AI)

```

- **Maze Program**: Generates maze, moves robot, logs telemetry (position, moves, goal status).
- **Redis**: Stores current state and metrics (JSON format).
- **Python Bridge**: REST API to read/write Redis data and forward AI queries to Ollama.
- **Dashboard**: Real‑time telemetry display, maze visualisation, and AI chat.
- **Ollama**: Local LLM that answers questions based on live telemetry.

## Features

- ✅ A* pathfinding in the maze (press `p` to auto‑solve)
- ✅ Real‑time telemetry updates (position, move count, progress)
- ✅ Web dashboard with live maze visualisation
- ✅ AI assistant (Llama 3) – ask questions like:
  - *“What’s the current robot position?”*
  - *“How many moves have been made?”*
  - *“Is Redis connected?”*
  - *“Analyse the telemetry stability.”*
- ✅ Persistent Redis storage
- ✅ Responsive design (works on desktop & mobile)

## Requirements

| Dependency          | Version / Notes                                   |
|---------------------|---------------------------------------------------|
| **C compiler**      | gcc (with SDL2 and libcurl development headers)  |
| **Redis**           | 6.0+ (with RedisJSON, or use Redis Stack)        |
| **Python**          | 3.8+                                              |
| **Ollama**          | Latest (pull `llama3` model)                     |
| **Web server**      | Any static server (Python `http.server`, Apache) |

## Installation & Setup

### 1. Install system dependencies

**Ubuntu / Debian**
```bash
sudo apt update
sudo apt install libsdl2-dev libcurl4-openssl-dev
```

macOS (Homebrew)

```bash
brew install sdl2 curl
```

2. Install Redis (with RedisJSON)

Use Docker (easiest):

```bash
docker run -d --name redis-stack -p 6379:6379 redis/redis-stack-server:latest
```

Or install natively: redis.io/docs/latest/operate/oss_and_stack/install/

3. Install Python dependencies

```bash
pip install flask redis flask-cors
```

4. Install Ollama and pull the model

```bash
curl -fsSL https://ollama.com/install.sh | sh
ollama pull llama3
```

(Ollama usually runs as a background service; start it with ollama serve if needed.)

5. Place the files

Create a directory with these files:

```
project/
├── maze_program.c        # Your C maze code (with A*)
├── redis_bridge.py       # Python bridge (Flask app)
├── dashboard.html        # Web dashboard
└── README.md
```

6. Configure the C program

Make sure the Redis endpoint is correct in maze_program.c:

```c
#define REDIS_HOST "127.0.0.1"
#define REDIS_PORT 6379
```

Compile the maze:

```bash
gcc -o maze_program maze_program.c -lSDL2 -lcurl -lm
```

7. Run the services

Open three terminals:

Terminal 1 – Redis

```bash
redis-server
# or if using Docker: docker start redis-stack
```

Terminal 2 – Python Bridge

```bash
python redis_bridge.py
```

Terminal 3 – Ollama

```bash
ollama serve
```

8. Serve the dashboard

```bash
# From the project directory
python -m http.server 8080
```

Open http://localhost:8080/dashboard.html in your browser.

9. Launch the maze

```bash
./maze_program
```

Now use WASD or arrow keys to move the robot. Press p to run the A* solver. Watch the dashboard update in real time.

Using the AI Assistant

In the dashboard chat box, ask any question. The AI receives the current telemetry as context.

Example questions:

· “Where is the robot now?”
· “What is the mission progress?”
· “How many moves have been made?”
· “Is Redis connected?”
· “Analyse the telemetry stability.”
· “Give me a system summary.”

API Endpoints (Python Bridge)

Endpoint Method Description
/api/telemetry GET Returns all current telemetry (JSON)
/api/telemetry POST Accepts new telemetry from the maze
/api/chat POST Forwards a prompt to Ollama

Troubleshooting

Problem Solution
curl/curl.h: No such file Install libcurl4-openssl-dev (or libcurl-devel).
Maze cannot connect to Redis Check Redis is running: redis-cli ping.
Dashboard shows no data Open browser console (F12) for fetch/CORS errors.
Ollama not responding Run ollama serve in a terminal.
Python bridge fails to start Install missing modules: pip install flask redis flask-cors.
SDL2 compilation error Install libsdl2-dev and use pkg-config --cflags --libs sdl2.

Customisation

· Maze size: Edit MAZE_W and MAZE_H in the C code (update dashboard canvas too).
· AI model: Change OLLAMA_MODEL in redis_bridge.py (e.g., llama3.2, phi3).
· Telemetry fields: Extend the JSON structures in both the C program and the Python bridge.

License

MIT – free to use and modify.

---

Enjoy your AI‑powered maze explorer! 🧠🤖🗺️
