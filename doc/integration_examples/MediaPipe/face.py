#!/usr/bin/python3

import cv2
import os
import sys
import time
import mediapipe as mp
from mediapipe.tasks import python
from mediapipe.tasks.python import vision
import libmapper as mpr

# Drawing utils are still usable for manual rendering
mp_drawing = mp.solutions.drawing_utils
mp_drawing_styles = mp.solutions.drawing_styles

# --- Model file check ---
MODEL_PATH = 'face_landmarker.task'
if not os.path.exists(MODEL_PATH):
    print(f"Model file '{MODEL_PATH}' not found.")
    response = input("Would you like to download it now? (y/n): ").strip().lower()
    if response == 'y':
        os.system(f'wget -q --show-progress https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task -O {MODEL_PATH}')
        if not os.path.exists(MODEL_PATH):
            print("Download failed. Please download manually and re-run.")
            sys.exit(1)
        print("Download complete.")
    else:
        print("Cannot continue without model file. Exiting.")
        sys.exit(1)

# --- libmapper setup ---
dev = mpr.Device("head_pose")
gaze_ratio_sig = dev.add_signal(mpr.Signal.Direction.OUTGOING, "gaze_ratio", 1, mpr.Type.FLOAT)
mouth_size_sig = dev.add_signal(mpr.Signal.Direction.OUTGOING, "mouth_size", 2, mpr.Type.FLOAT)

# --- Gaze ratio helper ---
def gaze_ratio(left, right, pupil):
    return (pupil.x - left.x) / (right.x - left.x)

# --- New Tasks API setup ---
BaseOptions = mp.tasks.BaseOptions
FaceLandmarker = mp.tasks.vision.FaceLandmarker
FaceLandmarkerOptions = mp.tasks.vision.FaceLandmarkerOptions
VisionRunningMode = mp.tasks.vision.RunningMode

options = FaceLandmarkerOptions(
    base_options=BaseOptions(model_asset_path=MODEL_PATH),
    running_mode=VisionRunningMode.VIDEO,
    num_faces=1,
    min_face_detection_confidence=0.5,
    min_tracking_confidence=0.5
)

# --- Debug flag ---
DEBUG = True

cap = cv2.VideoCapture(0)
width  = cap.get(cv2.CAP_PROP_FRAME_WIDTH)
height = cap.get(cv2.CAP_PROP_FRAME_HEIGHT)

with FaceLandmarker.create_from_options(options) as landmarker:
    while cap.isOpened():
        success, image = cap.read()
        if not success:
            break

        dev.poll()

        image = cv2.flip(image, 1)
        image_rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)

        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=image_rgb)
        timestamp_ms = int(time.time() * 1000)
        results = landmarker.detect_for_video(mp_image, timestamp_ms)

        if results.face_landmarks:
            for face_idx, face_landmarks in enumerate(results.face_landmarks):

                # ----------------------------------------
                # ESTIMATED FACE LANDMARKS ACCESSIBLE HERE
                # ----------------------------------------

                # --- DEBUG: print all 478 landmarks ---
                if DEBUG:
                    print(f"\n--- Face {face_idx} ({len(face_landmarks)} landmarks) ---")
                    for lm_id, lm in enumerate(face_landmarks):
                        print(f"  Landmark {lm_id:03d}: x={lm.x:.4f}  y={lm.y:.4f}  z={lm.z:.4f}")
                # ---------------------------------------

                # Draw landmarks manually (Tasks API returns plain lists, not protos)
                for lm in face_landmarks:
                    cx, cy = int(lm.x * width), int(lm.y * height)
                    cv2.circle(image, (cx, cy), 1, (0, 200, 100), -1)

                # Gaze: landmark indices are the same as before
                # Left eye: 33=inner corner, 133=outer corner, 468=left iris center
                # Right eye: 363=inner corner, 263=outer corner, 473=right iris center
                left_gr  = gaze_ratio(face_landmarks[33],  face_landmarks[133], face_landmarks[468])
                right_gr = gaze_ratio(face_landmarks[363], face_landmarks[263],  face_landmarks[473])

                mean_gaze = (left_gr + right_gr) * 0.5
                if DEBUG:
                    print(f"  gaze_ratio (mean): {mean_gaze:.4f}")

                gaze_ratio_sig.set_value(mean_gaze)

                # Mouth: 61=left corner, 291=right corner, 13=upper lip, 14=lower lip
                mouth_w = face_landmarks[291].x - face_landmarks[61].x
                mouth_h = face_landmarks[14].y  - face_landmarks[13].y
                if DEBUG:
                    print(f"  mouth_size: w={mouth_w:.4f}  h={mouth_h:.4f}")

                mouth_size_sig.set_value([mouth_w, mouth_h])

        cv2.imshow('MediaPipe Head Pose', image)

        if cv2.waitKey(5) & 0xFF == 27:
            break

dev.free()
cap.release()
cv2.destroyAllWindows()