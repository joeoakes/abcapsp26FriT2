# 🛰️ Maze Pupper 2: Mission Control
### AI-Powered Robotics Telemetry & Analysis System

## 📌 Project Overview
This system provides a real-time, high-visibility dashboard for the **Mini Pupper 2** robotics platform. It integrates live telemetry from a C-based maze application, persists data via **Redis**, and utilizes a local **Llama 3 (Ollama)** instance to provide formal mission analysis and status reports.

---

## 🏗️ System Architecture
The project is built on a "Bridge" architecture to facilitate communication between the robot's local environment and the university's server infrastructure:

1.  **Robot Client (C/SDL2):** Executes pathfinding (A*) and transmits coordinates to the Bridge.
2.  **Python Bridge:** Acts as the central nervous system, handling `POST` telemetry and serving `GET` requests to the dashboard while managing **CORS** for secure browser access.
3.  **Redis Database:** Stores the state of the maze and robot mission.
4.  **Ollama (Llama 3):** Serves as the Mission Control Analyst, processing natural language queries about live data.
5.  **Web Dashboard:** A high-contrast "Ocean Blue" interface designed for large-screen presentations.

---

## 🚀 Getting Started

### 1. Server-Side Setup (PSU Server)
Ensure **Redis** and **Ollama** are installed and running on your server (`10.170.8.109`).

```bash
# Start the AI service
ollama serve

# Run the Python Bridge
python3 bridge.py
