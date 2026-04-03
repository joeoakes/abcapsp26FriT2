# Maze Pupper 2 · Mission Control Dashboard

A real-time, AI-powered web dashboard for monitoring an autonomous Mini Pupper robot running an A* pathfinding algorithm. This system visualizes live maze telemetry, hardware status, and database metrics, and features an integrated Llama 3 assistant to analyze mission data on the fly.

## 🌟 Features
* **Real-Time Telemetry:** Live updates of the robot's (X,Y) coordinates, move sequence, and mission progress.
* **Hardware Monitoring:** Displays Mini Pupper battery life, signal strength, and system CPU/RAM usage.
* **Database Tracking:** Monitors latency and connection status for the dual-database backend (Redis & MongoDB).
* **AI Mission Assistant:** An integrated chat interface powered by local Llama 3 models (via Ollama) that uses the live telemetry as context to answer questions about the current run.

## 🏗️ Architecture
The project is decoupled into three main layers:

1. **The Data Generator (C Program):** Runs the A* algorithm, controls the Mini Pupper, and sends POST requests containing JSON telemetry.
2. **The PHP Bridge (Backend):** * `maze_bridge.php`: Catches the POST requests from the C program and writes the data to the Redis server.
    * `redis_api.php`: Pulls the latest telemetry from Redis to feed the dashboard.
    * `olama_api.php`: Injects the live telemetry into a system prompt and bridges the dashboard chat with a local Ollama instance.
3. **The Dashboard (Frontend):** `maze-pupper-dashboard.html` dynamically fetches data every 2 seconds to update the UI and provides the AI chat interface.

## 📋 Prerequisites
* **Web Server:** A server with PHP enabled (e.g., PSU webfiles, Apache, Nginx).
* **Redis Server:** Hosted on a reachable IP (e.g., WSL Ubuntu) with port `6379` open.
* **Ollama:** Installed locally or on a network server, with the `llama3` or `llama3.2` models pulled.
* **C Compiler:** For compiling the maze logic (`gcc` with SDL2 and libcurl).

## 🚀 Setup & Installation

### 1. Database & AI Setup
1. **Configure Redis:** Ensure your Redis server accepts external connections.
   * Open `/etc/redis/redis.conf`
   * Set `bind 0.0.0.0`
   * Set `protected-mode no`
   * Restart the service: `sudo systemctl restart redis-server`
2. **Start Ollama:** Ensure your Ollama server is running and accessible.

### 2. Configure the Backend (PHP)
Upload the `.php` files and the `.html` dashboard to your web server. Update the IP addresses in the PHP scripts to match your network topology:
* In `redis_api.php` and `maze_bridge.php`: Update the Redis connection IP:
  ```php
  $redis->connect('YOUR_REDIS_SERVER_IP', 6379, 2)
