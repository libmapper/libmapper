#!/usr/bin/python3

import cv2
import os
import sys
import time
import mediapipe as mp
import libmapper as mpr

MODEL_PATH = "face_landmarker.task"

if not os.path.exists(MODEL_PATH):
    print(f"Model file '{MODEL_PATH}' not found.")
    response = input("Would you like to download it now? (y/n): ").strip().lower()

    if response == "y":
        os.system(
            f"wget -q --show-progress "
            f"https://storage.googleapis.com/mediapipe-models/"
            f"face_landmarker/face_landmarker/float16/1/"
            f"face_landmarker.task -O {MODEL_PATH}"
        )

        if not os.path.exists(MODEL_PATH):
            print("Download failed.")
            sys.exit(1)

        print("Download complete.")

    else:
        print("Cannot continue without the model.")
        sys.exit(1)

dev = mpr.Device("head_pose")

gaze_ratio_sig = dev.add_signal(
    mpr.Signal.Direction.OUTGOING,
    "gaze_ratio",
    1,
    mpr.Type.FLOAT,
)

mouth_size_sig = dev.add_signal(
    mpr.Signal.Direction.OUTGOING,
    "mouth_size",
    2,
    mpr.Type.FLOAT,
)


def gaze_ratio(left, right, pupil):
    return (pupil.x - left.x) / (right.x - left.x)

BaseOptions = mp.tasks.BaseOptions
FaceLandmarker = mp.tasks.vision.FaceLandmarker
FaceLandmarkerOptions = mp.tasks.vision.FaceLandmarkerOptions
RunningMode = mp.tasks.vision.RunningMode

options = FaceLandmarkerOptions(
    base_options=BaseOptions(model_asset_path=MODEL_PATH),
    running_mode=RunningMode.VIDEO,
    num_faces=1,
    min_face_detection_confidence=0.5,
    min_tracking_confidence=0.5,
)

DEBUG = False

cap = cv2.VideoCapture(0)

if not cap.isOpened():
    print("Could not open webcam.")
    sys.exit(1)

with FaceLandmarker.create_from_options(options) as landmarker:

    while cap.isOpened():

        success, image = cap.read()

        if not success:
            break

        dev.poll()

        image = cv2.flip(image, 1)

        image_rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)

        mp_image = mp.Image(
            image_format=mp.ImageFormat.SRGB,
            data=image_rgb,
        )

        timestamp_ms = int(time.time() * 1000)

        results = landmarker.detect_for_video(
            mp_image,
            timestamp_ms,
        )

        h, w = image.shape[:2]

        if results.face_landmarks:

            for face_idx, face_landmarks in enumerate(results.face_landmarks):

                if DEBUG:
                    print(f"\nFace {face_idx}")
                    
                for i, lm in enumerate(face_landmarks):

                    x = int(lm.x * w)
                    y = int(lm.y * h)

                    cv2.circle(image, (x, y), 1, (0, 255, 0), -1)

                    if DEBUG:
                        print(
                            f"{i:03d}: "
                            f"x={lm.x:.4f} "
                            f"y={lm.y:.4f} "
                            f"z={lm.z:.4f}"
                        )


                left_gr = gaze_ratio(
                    face_landmarks[33],
                    face_landmarks[133],
                    face_landmarks[468],
                )

                right_gr = gaze_ratio(
                    face_landmarks[363],
                    face_landmarks[263],
                    face_landmarks[473],
                )

                mean_gaze = (left_gr + right_gr) * 0.5

                gaze_ratio_sig.set_value(mean_gaze)

                if DEBUG:
                    print(f"Gaze ratio: {mean_gaze:.4f}")

                mouth_width = (
                    face_landmarks[291].x -
                    face_landmarks[61].x
                )

                mouth_height = (
                    face_landmarks[14].y -
                    face_landmarks[13].y
                )

                mouth_size_sig.set_value(
                    [
                        mouth_width,
                        mouth_height,
                    ]
                )

                if DEBUG:
                    print(
                        f"Mouth: "
                        f"w={mouth_width:.4f} "
                        f"h={mouth_height:.4f}"
                    )

        cv2.imshow("MediaPipe Face Landmarker", image)

        if cv2.waitKey(5) & 0xFF == 27:
            break
        
dev.free()
cap.release()
cv2.destroyAllWindows()