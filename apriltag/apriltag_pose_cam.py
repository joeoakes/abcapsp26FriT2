import cv2
from pupil_apriltags import Detector
import math
import numpy as np

def rpy_from_R(R):
    """Convert 3x3 rotation matrix to roll, pitch, yaw in degrees."""
    yaw = math.atan2(R[1,0], R[0,0])
    pitch = math.atan2(-R[2,0], math.sqrt(R[2,1]**2 + R[2,2]**2))
    roll = math.atan2(R[2,1], R[2,2])
    return map(math.degrees, (roll, pitch, yaw))

def main():
    # -----------------------------
    # Camera Setup
    # -----------------------------
    camera_device = 1  # try 1 or 2 if needed
    cap = cv2.VideoCapture(camera_device, cv2.CAP_DSHOW)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

    if not cap.isOpened():
        print("❌ Could not open camera")
        return

    # -----------------------------
    # Camera intrinsics & tag size
    # -----------------------------
    w, h = 1280, 720
    fx = fy = 900.0
    cx = w / 2.0
    cy = h / 2.0
    tag_size_m = 0.05

    # -----------------------------
    # AprilTag Detector
    # -----------------------------
    detector = Detector(
        families="tag36h11",
        nthreads=4,
        quad_decimate=2.0,
        quad_sigma=0.0,
        refine_edges=True
    )

    print("✅ Camera started. Press 'q' to quit.")

    # -----------------------------
    # Main Loop
    # -----------------------------
    while True:
        ret, frame = cap.read()
        if not ret:
            print("❌ Failed to grab frame")
            break

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

        # Detect tags WITH pose estimation
        results = detector.detect(
            gray,
            estimate_tag_pose=True,
            camera_params=(fx, fy, cx, cy),
            tag_size=tag_size_m
        )

        best_tag = None
        if results:
            # Pick tag with highest decision margin
            best_tag = max(results, key=lambda t: t.decision_margin)

        if best_tag:
            # Draw bounding box
            pts = best_tag.corners.astype(int)
            for i in range(4):
                pt1 = tuple(pts[i])
                pt2 = tuple(pts[(i+1)%4])
                cv2.line(frame, pt1, pt2, (0,255,0), 2)

            # Draw center
            center = tuple(best_tag.center.astype(int))
            cv2.circle(frame, center, 5, (0,0,255), -1)

            # Tag ID
            cv2.putText(frame, f"ID: {best_tag.tag_id}", (center[0]+10, center[1]), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255,0,0), 2)

            # ---- Pose ----
            tx, ty, tz = best_tag.pose_t.flatten().tolist()
            R = best_tag.pose_R
            roll, pitch, yaw = rpy_from_R(R)

            # Display position and orientation
            cv2.putText(frame, f"X:{tx:.2f} Y:{ty:.2f} Z:{tz:.2f} m", (10, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0,255,255),2)
            cv2.putText(frame, f"roll:{roll:.1f} pitch:{pitch:.1f} yaw:{yaw:.1f}", (10, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0,255,255),2)

            # Print in console
            print(f"ID:{best_tag.tag_id}  X:{tx:.2f} Y:{ty:.2f} Z:{tz:.2f}  roll:{roll:.1f} pitch:{pitch:.1f} yaw:{yaw:.1f}")

        cv2.imshow("AprilTag Pose", frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    # -----------------------------
    # Cleanup
    # -----------------------------
    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
