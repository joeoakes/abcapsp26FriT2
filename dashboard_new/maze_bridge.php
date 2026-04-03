<?php
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST, GET, OPTIONS');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit();
}

$telemetry_file = __DIR__ . '/telemetry_cache.json';
$history_file = __DIR__ . '/telemetry_history.json';
$commands_file = __DIR__ . '/commands_cache.json';

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
    $move_dir = $input['input']['move_dir'] ?? '';
    $robot = $input['robot'] ?? [];

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
        'robot' => [
            'heading' => $robot['heading'] ?? 0,
            'astar_running' => $robot['astar_running'] ?? false,
            'path_length' => $robot['path_length'] ?? 0,
            'path_index' => $robot['path_index'] ?? 0
        ],
        'command' => [
            'event_type' => $event_type,
            'move_dir' => $move_dir,
            'timestamp' => $timestamp
        ],
        'timestamp' => $timestamp,
        'last_event' => $event_type,
        'last_move_sequence' => $move_sequence
    ];

    file_put_contents($telemetry_file, json_encode($telemetry, JSON_PRETTY_PRINT));

    $history = [];
    if (file_exists($history_file)) {
        $history = json_decode(file_get_contents($history_file), true) ?: [];
    }
    array_unshift($history, $telemetry);
    $history = array_slice($history, 0, 100);
    file_put_contents($history_file, json_encode($history, JSON_PRETTY_PRINT));

    $commands = [];
    if (file_exists($commands_file)) {
        $commands = json_decode(file_get_contents($commands_file), true) ?: [];
    }
    array_unshift($commands, $telemetry['command']);
    $commands = array_slice($commands, 0, 50);
    file_put_contents($commands_file, json_encode($commands, JSON_PRETTY_PRINT));

    echo json_encode([
        'status' => 'ok',
        'stored' => true,
        'method' => 'file'
    ]);
    exit();
}

function calculate_progress($x, $y) {
    $total_cells = 21 * 15;
    $current_cell = $y * 21 + $x;
    return round(($current_cell / $total_cells) * 100);
}

if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    if (file_exists($telemetry_file)) {
        echo file_get_contents($telemetry_file);
    } else {
        echo json_encode(['status' => 'waiting', 'message' => 'No data yet']);
    }
    exit();
}
?>