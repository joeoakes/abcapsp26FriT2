# Maze Pupper 2 - Live Network Dashboard

A real-time monitoring dashboard for the Maze Pupper 2 robotics system, featuring live network status, service monitoring, and interactive maze visualization.

![Dashboard Preview](https://via.placeholder.com/800x400/0a1a25/0ff?text=Maze+Pupper+Dashboard)

## Overview

This dashboard provides a unified interface for monitoring all components of the Maze Pupper 2 system, including:
- Database connections (MongoDB, Redis)
- Mini Pupper 2 robot telemetry
- Network and VPN status
- Mission tracking with maze path visualization
- AprilTag computer vision tracking
- System resource monitoring

## Features

### 🗺️ **Maze Visualization**
- Interactive 12x12 grid maze
- Real-time path animation
- Step-by-step movement control
- Random path generation
- Visual representation of robot position (S=Start, E=End, cyan dot=current position)

### 🌐 **Network Monitoring**
- Automatic VPN detection
- Real-time IP address tracking
- Connection status for all services
- Latency monitoring
- Signal strength visualization

### 🤖 **Service Status**
- **MongoDB**: Connection status, latency, storage usage
- **Redis**: Cache status, hit rate, memory usage
- **Mini Pupper 2**: Battery, temperature, LIDAR status, signal strength
- **AprilTag**: Vision tracking PC, tag positions, confidence levels

### 📊 **Telemetry**
- Movement file size tracking
- Connection stability metrics
- Data rate monitoring
- Packet count

### 💻 **System Resources**
- CPU load with visual progress bar
- RAM usage
- System uptime
- Local IP address

## Technology Stack

- **Frontend**: HTML5, CSS3, JavaScript (ES6)
- **Styling**: Glassmorphism design with gradient effects
- **Icons**: Font Awesome 6
- **Fonts**: Inter, JetBrains Mono
- **Visualization**: HTML5 Canvas API
- **Network Detection**: WebRTC API

## Installation

### Prerequisites
- Modern web browser (Chrome, Firefox, Safari, Edge)
- No server required - runs entirely in the browser

### Quick Start

1. **Download the Dashboard**
   ```bash
   git clone https://github.com/yourusername/maze-pupper-dashboard.git
   cd maze-pupper-dashboard
