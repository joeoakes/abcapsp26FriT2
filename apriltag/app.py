from flask import Flask, Response, jsonify
from apriltag_grid_map import CameraTracker, find_cameras
import time, cv2, numpy as np

app = Flask(__name__)

# -----------------------------
# Camera Selection (runs once at startup)
# -----------------------------
def select_camera():
    cams = find_cameras()

    if not cams:
        raise RuntimeError("No cameras detected.")

    print("\nAvailable cameras:")
    for cam in cams:
        print(f" - Camera {cam}")

    # Auto-pick if only one camera
    if len(cams) == 1:
        print(f"[INFO] Only one camera found. Using {cams[0]}")
        return cams[0]

    # Ask user
    while True:
        try:
            cam_index = int(input("Select camera index: "))
            if cam_index in cams:
                return cam_index
            else:
                print("Invalid selection. Try again.")
        except ValueError:
            print("Please enter a valid number.")

# -----------------------------
# Initialize Tracker
# -----------------------------
try:
    cam_index = select_camera()
    tracker = CameraTracker(cam_index=cam_index)
    print(f"[INFO] CameraTracker started on index {cam_index}")
except RuntimeError as e:
    print(f"[WARN] {e}")
    tracker = None

# -----------------------------
# Fallback Blanks
# -----------------------------
def _blank(h, w, ch, msg):
    f = np.zeros((h, w, ch) if ch > 1 else (h, w), dtype=np.uint8)
    cv2.putText(f, msg, (20, h//2), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (60,60,60), 2)
    _, jpg = cv2.imencode('.jpg', f)
    return jpg.tobytes()

_BV = _blank(480, 640, 3, "camera not connected")
_BM = _blank(600, 600, 1, "no map data")

# -----------------------------
# Generators
# -----------------------------
def gen_video():
    while True:
        data = tracker.get_frame_stream() if tracker else None
        yield b'--frame\r\nContent-Type: image/jpeg\r\n\r\n' + (data or _BV) + b'\r\n'
        time.sleep(0.033)

def gen_map():
    while True:
        data = tracker.get_map_bytes() if tracker else None
        yield b'--frame\r\nContent-Type: image/jpeg\r\n\r\n' + (data or _BM) + b'\r\n'
        time.sleep(0.033)

# -----------------------------
# Routes
# -----------------------------
@app.route('/')
def dashboard():
    html = """
    <!DOCTYPE html>
    <html>
    <head>
        <title>Robot Vision Dashboard</title>
        <style>
            body { font-family: Arial, sans-serif; background: #222; color: #fff; text-align: center; }
            .container { display: flex; justify-content: center; gap: 30px; margin-top: 20px; flex-wrap: wrap; }
            .feed { background: #333; padding: 10px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.5); }
            img { border: 2px solid #555; border-radius: 4px; max-width: 100%; height: auto; }
        </style>
    </head>
    <body>
        <h2>Multi-Tag Mapping Dashboard</h2>
        <div class="container">
            <div class="feed">
                <h3>Live Camera</h3>
                <img src="/video_feed" width="640">
            </div>
            <div class="feed">
                <h3>2D Grid Map</h3>
                <img src="/map_feed" width="600">
            </div>
        </div>
    </body>
    </html>
    """
    return html

@app.route('/video_feed')
def video_feed():
    return Response(gen_video(), mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/map_feed')
def map_feed():
    return Response(gen_map(), mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/detection')
def detection():
    if tracker:
        return jsonify(tracker.get_latest_detection())
    return jsonify({"tag_id":None,"x":None,"y":None,"z":None,"roll":None,"pitch":None,"yaw":None})

@app.route('/health')
def health():
    det = tracker.get_latest_detection()['tag_id'] is not None if tracker else False
    return jsonify({'online':True,'camera':tracker is not None,'detecting':det})

if __name__ == '__main__':
    print("\n[SUCCESS] Flask server running.")
    print("--> Open your browser at: http://localhost:5001\n")
    try:
        app.run(host='0.0.0.0', port=5001, debug=False, threaded=True)
    finally:
        if tracker: tracker.release()
