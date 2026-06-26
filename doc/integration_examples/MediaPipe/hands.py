#!/usr/bin/python3

import cv2
import mediapipe as mp
from mediapipe.tasks import python
from mediapipe.tasks.python import vision
import libmapper as mpr
import signal
import time
import os
import sys

done = False

def handler_done(signum, frame):
    global done
    done = True

signal.signal(signal.SIGINT, handler_done)
signal.signal(signal.SIGTERM, handler_done)

# --- Model file check ---
MODEL_PATH = 'hand_landmarker.task'
if not os.path.exists(MODEL_PATH):
    print(f"Model file '{MODEL_PATH}' not found.")
    response = input("Would you like to download it now? (y/n): ").strip().lower()
    if response == 'y':
        os.system(f'wget -q --show-progress https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task -O {MODEL_PATH}')
        if not os.path.exists(MODEL_PATH):
            print("Download failed. Please download manually and re-run.")
            sys.exit(1)
        print("Download complete.")
    else:
        print("Cannot continue without model file. Exiting.")
        sys.exit(1)

# --- libmapper setup ---
dev = mpr.Device("mediapipe.hands")
index_tip_pos = dev.add_signal(mpr.Signal.Direction.OUTGOING, "finger/index/tip/position",
                               2, mpr.Type.FLOAT, num_inst=2)
thumb_tip_pos = dev.add_signal(mpr.Signal.Direction.OUTGOING, "thumb/tip/position",
                               2, mpr.Type.FLOAT, num_inst=2)
is_left = dev.add_signal(mpr.Signal.Direction.OUTGOING, "is_left", 1, mpr.Type.INT32, num_inst=2)

print(index_tip_pos.properties)

# --- Hand connections for drawing (21 landmark skeleton) ---
HAND_CONNECTIONS = [
    (0,1),(1,2),(2,3),(3,4),         # thumb
    (0,5),(5,6),(6,7),(7,8),         # index
    (0,9),(9,10),(10,11),(11,12),    # middle
    (0,13),(13,14),(14,15),(15,16),  # ring
    (0,17),(17,18),(18,19),(19,20),  # pinky
    (5,9),(9,13),(13,17)             # palm
]

def draw_hand_landmarks(image, landmarks, width, height):
    for start, end in HAND_CONNECTIONS:
        x0, y0 = int(landmarks[start].x * width), int(landmarks[start].y * height)
        x1, y1 = int(landmarks[end].x * width),   int(landmarks[end].y * height)
        cv2.line(image, (x0, y0), (x1, y1), (201, 107, 62), 2)

    for lm in landmarks:
        cx, cy = int(lm.x * width), int(lm.y * height)
        cv2.circle(image, (cx, cy), 4, (92, 49, 29), -1)

# --- Debug flag ---
DEBUG = True

# --- Tasks API setup ---
BaseOptions = mp.tasks.BaseOptions
HandLandmarker = mp.tasks.vision.HandLandmarker
HandLandmarkerOptions = mp.tasks.vision.HandLandmarkerOptions
VisionRunningMode = mp.tasks.vision.RunningMode

options = HandLandmarkerOptions(
    base_options=BaseOptions(model_asset_path=MODEL_PATH),
    running_mode=VisionRunningMode.VIDEO,
    num_hands=2,
    min_hand_detection_confidence=0.8,
    min_tracking_confidence=0.5
)

cap = cv2.VideoCapture(0)
width  = cap.get(cv2.CAP_PROP_FRAME_WIDTH)
height = cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
max_inst_id = -1

with HandLandmarker.create_from_options(options) as landmarker:
    while cap.isOpened() and not done:
        success, image = cap.read()
        if not success:
            break

        dev.poll()

        image = cv2.flip(image, 1)
        image_rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)

        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=image_rgb)
        timestamp_ms = int(time.time() * 1000)
        results = landmarker.detect_for_video(mp_image, timestamp_ms)

        idx = -1
        if results.hand_landmarks:
            for i, hand_landmarks in enumerate(results.hand_landmarks):
                idx += 1
                print('updating instance', idx)

                hand_label = results.handedness[i][0].category_name
                is_left.Instance(idx).set_value(1 if hand_label == "Left" else 0)

                index_tip_pos.Instance(idx).set_value([hand_landmarks[8].x, hand_landmarks[8].y])
                thumb_tip_pos.Instance(idx).set_value([hand_landmarks[4].x, hand_landmarks[4].y])

                # --- DEBUG: print all 21 landmarks ---
                if DEBUG:
                    print(f"\n--- Hand {idx} ({hand_label}) ---")
                    for lm_id, lm in enumerate(hand_landmarks):
                        print(f"  Landmark {lm_id:02d}: x={lm.x:.4f}  y={lm.y:.4f}  z={lm.z:.4f}")
                # --------------------------------------

                draw_hand_landmarks(image, hand_landmarks, width, height)

        # Release any instances beyond current detected hands
        to_release = idx + 1
        while to_release <= max_inst_id:
            is_left.Instance(to_release).release()
            index_tip_pos.Instance(to_release).release()
            thumb_tip_pos.Instance(to_release).release()
            to_release += 1
        max_inst_id = idx

        dev.update_maps()

        cv2.imshow('MediaPipe Hands', image)

        if cv2.waitKey(5) & 0xFF == 27:
            break

dev.free()
cap.release()
cv2.destroyAllWindows()