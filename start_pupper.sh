#!/bin/bash

echo "Starting Mini Pupper bringup + calibration + HTTPS listener..."

cd ~/ros2_ws || exit 1

source /opt/ros/humble/setup.bash
source ~/ros2_ws/install/setup.bash

# Build listener if needed
if [ ! -d "install/maze_https_fast" ]; then
    echo "Building maze_https_fast..."
    colcon build --packages-select maze_https_fast
    source ~/ros2_ws/install/setup.bash
fi

# Build bringup if needed
if [ ! -d "install/mini_pupper_bringup" ]; then
    echo "Building Mini Pupper bringup..."
    colcon build --symlink-install --packages-select mini_pupper_bringup mini_>
    source ~/ros2_ws/install/setup.bash
fi

# Kill old processes
pkill -f mini_pupper_bringup 2>/dev/null
pkill -f maze_https_fast 2>/dev/null

# Start bringup in background
nohup ros2 launch mini_pupper_bringup bringup.launch.py > ~/pupper_bringup.log>
echo "Bringup started in background"
echo "Log: ~/pupper_bringup.log"

# Give bringup time to fully come up
sleep 2

echo "Starting HTTPS listener (foreground)..."
ros2 run maze_https_fast maze_https_fast
~/start_pupper.sh
