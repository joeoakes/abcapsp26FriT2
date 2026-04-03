<?php
/**
 * Maze Bridge - Receives telemetry from your C maze program
 * Stores data in Redis for the dashboard
 * 
 * Your maze program should POST JSON to this endpoint
 */

header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST, GET, OPTIONS');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit();
}

// Redis connection
$redis = null;
$redis_connected = false;

try {
    $redis = new Redis();
    if ($redis->connect('127.0.0.1', 6379, 2)) {
        $redis_connected = true;
    }
} catch (Exception $e) {
    error_log("Redis connection failed: " . $e->getMessage());
}

// Handle POST from maze program
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $input = json_decode(file_get_contents('php://input'), true);
    
    if (!$input) {
        http_response_code(400);
        echo json_encode(['error' => 'Invalid JSON']);
        exit();
    }
    
    $event_type = $input['event_type'] ?? 'unknown';
    $timestamp = $input['timestamp'] ?? date('c');
    $player = $input['player'] ?? ['position' => ['x' => 0, 'y' => 0]];
    $goal_reached = $input['goal_reached'] ?? false;
    $level = $input['level'] ?? 0;
    $move_sequence = $input['input']['move_sequence'] ?? 0;
    
    // Prepare complete telemetry package
    $telemetry = [
        'maze' => [
            'time' => date('H:i:s'),
            'progress' => calculate_progress($player['position']['x'] ?? 0, $player['position']['y'] ?? 0),
            'position' => [
                'x' => $player['position']['x'] ?? 0,
                'y' => $player['position']['y'] ?? 0
            ],
            'moveset' => $move_sequence,
            'goal_reached' => $goal_reached,
            'level' => $level
        ],
        'timestamp' => $timestamp,
        'last_event' => $event_type,
        'last_move_sequence' => $move_sequence
    ];
    
    // Store in Redis
    if ($redis_connected) {
        $redis->set('maze:current', json_encode($telemetry));
        $redis->set('maze:last_update', time());
        
        // Store history (keep last 100 events)
        $redis->lPush('maze:history', json_encode($telemetry));
        $redis->lTrim('maze:history', 0, 99);
        
        echo json_encode(['status' => 'ok', 'stored' => true, 'redis' => true]);
    } else {
        // Fallback to file
        file_put_contents('telemetry_cache.json', json_encode($telemetry));
        echo json_encode(['status' => 'ok', 'stored' => true, 'redis' => false]);
    }
    exit();
}

// Helper function to calculate progress percentage
function calculate_progress($x, $y) {
    $total_cells = 21 * 15; // MAZE_W * MAZE_H
    $current_cell = $y * 21 + $x;
    return round(($current_cell / $total_cells) * 100);
}

// GET request - return current status
if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    if ($redis_connected) {
        $data = $redis->get('maze:current');
        if ($data) {
            echo $data;
        } else {
            echo json_encode(['status' => 'waiting', 'message' => 'No data yet']);
        }
    } else {
        $cached = @file_get_contents('telemetry_cache.json');
        echo $cached ? $cached : json_encode(['status' => 'waiting']);
    }
    exit();
}
?>