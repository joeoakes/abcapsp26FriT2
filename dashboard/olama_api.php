<?php
/**
 * Ollama API Bridge - Connects dashboard to local Ollama
 */

header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST, OPTIONS');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit();
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    echo json_encode(['error' => 'Method not allowed']);
    exit();
}

$input = json_decode(file_get_contents('php://input'), true);

if (!$input || !isset($input['message'])) {
    http_response_code(400);
    echo json_encode(['error' => 'Missing message field']);
    exit();
}

$user_message = $input['message'];
$model = $input['model'] ?? 'llama3';
$system_context = $input['system_context'] ?? null;
$chat_history = $input['history'] ?? [];

// Prepare messages for Ollama
$messages = [];

if ($system_context) {
    $messages[] = [
        'role' => 'system',
        'content' => "You are an AI assistant for the Maze Pupper 2 robotics system. 
You have access to real-time maze telemetry data. Answer questions based on this data.
Be concise, technical, and helpful.

Current system data:
$system_context"
    ];
}

foreach ($chat_history as $entry) {
    $messages[] = ['role' => $entry['role'], 'content' => $entry['content']];
}

$messages[] = ['role' => 'user', 'content' => $user_message];

// Send to Ollama
$ollama_request = [
    'model' => $model,
    'messages' => $messages,
    'stream' => false,
    'options' => ['temperature' => 0.7, 'num_predict' => 500]
];

$ch = curl_init('http://127.0.0.1:11434/api/chat');
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_setopt($ch, CURLOPT_POST, true);
curl_setopt($ch, CURLOPT_POSTFIELDS, json_encode($ollama_request));
curl_setopt($ch, CURLOPT_HTTPHEADER, ['Content-Type: application/json']);
curl_setopt($ch, CURLOPT_TIMEOUT, 30);

$response = curl_exec($ch);
$http_code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
curl_close($ch);

if ($http_code !== 200) {
    http_response_code(500);
    echo json_encode(['error' => 'Ollama server error']);
    exit();
}

$ollama_response = json_decode($response, true);
$assistant_reply = $ollama_response['message']['content'] ?? 'No response';

echo json_encode(['success' => true, 'reply' => $assistant_reply, 'model' => $model]);
?>