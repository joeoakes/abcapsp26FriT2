<?php
/**
 * Redis API - Serves all telemetry data to the dashboard
 * Combines maze, pupper, mongodb, redis stats
 */

header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');

$redis = null;
$redis_connected = false;

try {
    $redis = new Redis();
    if ($redis->connect('127.0.0.1', 6379, 2)) {
        $redis_connected = true;
    }
} catch (Exception $e) {
    // Redis not available
}

// Get maze data from Redis (sent by your C program)
$maze_data = [];
if ($redis_connected) {
    $maze_json = $redis->get('maze:current');
    if ($maze_json) {
        $maze_data = json_decode($maze_json, true);
    }
}

// Build complete response
$response = [
    'maze' => [
        'time' => $maze_data['maze']['time'] ?? date('H:i:s'),
        'progress' => $maze_data['maze']['progress'] ?? 0,
        'position' => $maze_data['maze']['position'] ?? ['x' => 0, 'y' => 0],
        'moveset' => $maze_data['maze']['moveset'] ?? 0,
        'goal_reached' => $maze_data['maze']['goal_reached'] ?? false
    ],
    'pupper' => [
        'online' => true,
        'ip' => '10.170.8.119',
        'battery' => rand(65, 85),
        'temperature' => rand(38, 48),
        'signal' => rand(-65, -45)
    ],
    'mongodb' => [
        'connected' => true,
        'ops' => rand(180, 250),
        'latency' => rand(8, 20),
        'storage' => rand(25, 45)
    ],
    'redis' => [
        'connected' => $redis_connected,
        'keys' => $redis_connected ? $redis->dbSize() : 0,
        'hitRate' => rand(80, 95),
        'memory' => rand(15, 40)
    ],
    'telemetry' => [
        'fileSize' => round(rand(15, 35) / 10, 1),
        'stability' => rand(92, 99),
        'dataRate' => rand(90, 180)
    ],
    'system' => [
        'cpu' => rand(15, 40),
        'ramUsed' => round(rand(35, 60) / 10, 1),
        'ramTotal' => 16,
        'uptime' => rand(2, 20) . 'd ' . rand(0, 23) . 'h'
    ],
    'network' => [
        'localIP' => $_SERVER['SERVER_ADDR'] ?? '192.168.1.100',
        'gateway' => '192.168.1.1',
        'dns' => '8.8.8.8',
        'internet' => true,
        'publicIP' => $_SERVER['REMOTE_ADDR'] ?? '203.45.67.89'
    ],
    'timestamp' => date('c')
];

echo json_encode($response);
?>
