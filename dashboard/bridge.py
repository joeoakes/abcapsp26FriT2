import json
import subprocess
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer

class TelemetryHandler(BaseHTTPRequestHandler):
    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def do_GET(self):
        if self.path == '/telemetry_latest':
            result = subprocess.run(['redis-cli', 'GET', 'team2f:latest_telemetry'], capture_output=True, text=True)
            data = result.stdout.strip() or "{}"
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(data.encode())

    def do_POST(self):
        content_length = int(self.headers['Content-Length'])
        post_data = self.rfile.read(content_length)
        
        if self.path == '/telemetry':
            data = json.loads(post_data.decode('utf-8'))
            subprocess.run(['redis-cli', 'SET', 'team2f:latest_telemetry', json.dumps(data)])
            self.send_response(200)
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()

        elif self.path == '/ask_ai':
            user_query = json.loads(post_data.decode('utf-8'))['question']
            result = subprocess.run(['redis-cli', 'GET', 'team2f:latest_telemetry'], capture_output=True, text=True)
            latest_data = result.stdout.strip() or "No active telemetry."

            # FORMAL MISSION CONTROL PROMPT
            prompt = (
                "System: You are a Mission Control Analyst for the Team 2F Robotics Project. "
                "Provide a formal technical status report based on the provided JSON data. "
                "Use professional terminology, bullet points for metrics, and no conversational filler. "
                f"Data: {latest_data}. Inquiry: {user_query}"
            )
            
            payload = json.dumps({"model": "llama3", "prompt": prompt, "stream": False}).encode()
            try:
                req = urllib.request.Request("http://localhost:11434/api/generate", data=payload, headers={'Content-Type': 'application/json'})
                with urllib.request.urlopen(req) as res:
                    ai_response = json.loads(res.read().decode())['response']
            except Exception as e:
                ai_response = f"AI Error: System Offline. {str(e)}"

            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(json.dumps({"answer": ai_response}).encode())

if __name__ == '__main__':
    server_address = ('', 5000)
    httpd = HTTPServer(server_address, TelemetryHandler)
    print("Bridge + Formal AI Analyst running on port 5000...")
    httpd.serve_forever()