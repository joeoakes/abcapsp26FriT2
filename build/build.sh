#!/bin/bash

# Build Script for Team 2 Friday
# This script looks into the /https and /maze/missions folders to compile the apps.

echo "---------------------------------------"
echo "🛠️  Starting Build from /build folder..."
echo "---------------------------------------"

# Define the source paths relative to the build folder
HTTPS_DIR="../https"
MAZE_DIR="../maze/missions"

# Create binaries in the current (build) directory
# 1. Maze Game Client
if [ -f "$MAZE_DIR/maze_sdl2.c" ]; then
    echo "📦 Compiling Maze SDL2 Client..."
    gcc "$MAZE_DIR/maze_sdl2.c" -o maze_mac -lSDL2 -lcurl
    [ $? -eq 0 ] && echo "✅ Success: maze_mac created"
fi

# 2. Telemetry Server (MongoDB)
if [ -f "$HTTPS_DIR/maze_https_mongo.c" ]; then
    echo "📦 Compiling Telemetry Server (Mongo)..."
    gcc "$HTTPS_DIR/maze_https_mongo.c" -o telemetry_server -lmicrohttpd $(pkg-config --cflags --libs libmongoc-1.0)
    [ $? -eq 0 ] && echo "✅ Success: telemetry_server created"
fi

# 3. Mission Server (Redis)
if [ -f "$HTTPS_DIR/maze_https_redis.c" ]; then
    echo "📦 Compiling Mission Server (Redis)..."
    gcc "$HTTPS_DIR/maze_https_redis.c" -o mission_server -lmicrohttpd -lhiredis
    [ $? -eq 0 ] && echo "✅ Success: mission_server created"
fi

# 4. Pupper Bridge (ROS)
# Note: Your screenshot shows the file is named maze_https_ros.c
if [ -f "$HTTPS_DIR/maze_https_ros.c" ]; then
    if [ -n "$ROS_DISTRO" ]; then
        echo "📦 Compiling Pupper ROS Bridge..."
        gcc "$HTTPS_DIR/maze_https_ros.c" -o pupper_bridge -lmicrohttpd $(pkg-config --cflags --libs rclc rcl geometry_msgs)
        [ $? -eq 0 ] && echo "✅ Success: pupper_bridge created"
    else
        echo "⚠️  Skipping ROS Bridge: ROS 2 environment not sourced."
    fi
fi

# 5. Copy Certs (Crucial for HTTPS to work)
if [ -d "$HTTPS_DIR/certs" ]; then
    echo "🔐 Copying certificates to build folder..."
    cp -r "$HTTPS_DIR/certs" .
fi

echo "---------------------------------------"
echo "🎉 Build Finished."
