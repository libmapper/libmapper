#!/usr/bin/python3
import cv2
#import numpy as np
import mediapipe as mp
import libmapper as mpr

mp_drawing = mp.solutions.drawing_utils
mp_drawing_styles = mp.solutions.drawing_styles
mp_face_mesh = mp.solutions.face_mesh

dev = mpr.Device("head_pose")

gaze_ratio_sig = dev.add_signal(mpr.Direction.OUTGOING, "gaze_ratio", 1, mpr.Type.FLOAT)
mouth_size_sig = dev.add_signal(mpr.Direction.OUTGOING, "mouth_size", 2, mpr.Type.FLOAT)

# Begin webcam input for head tracking:
face_mesh = mp_face_mesh.FaceMesh(refine_landmarks=True)

cap = cv2.VideoCapture(0)

def gaze_ratio(left, right, pupil):
    return (pupil.x - left.x) / (right.x - left.x)

while cap.isOpened():
    success, image = cap.read()
    if not success:
        break

    # Poll the libmapper producer device to ensure that the signals are being updated properly
    dev.poll()

    image = cv2.cvtColor(cv2.flip(image, 1), cv2.COLOR_BGR2RGB)
    image.flags.writeable = False
    results = face_mesh.process(image)

    # Draw the hand annotations on the image.
    image.flags.writeable = True
    image = cv2.cvtColor(image, cv2.COLOR_RGB2BGR)

    if results.multi_face_landmarks:
        for face_landmarks in results.multi_face_landmarks:

            # ----------------------------------------
            # ESTIMATED FACE LANDMARKS ACCESSIBLE HERE
            # ----------------------------------------


            # Draw landmarks over video
            mp_drawing.draw_landmarks(
                image=image,
                landmark_list=face_landmarks,
                connections=mp_face_mesh.FACEMESH_TESSELATION,
                landmark_drawing_spec=None,
                connection_drawing_spec=mp_drawing_styles
                .get_default_face_mesh_tesselation_style())
            mp_drawing.draw_landmarks(
                image=image,
                landmark_list=face_landmarks,
                connections=mp_face_mesh.FACEMESH_CONTOURS,
                landmark_drawing_spec=None,
                connection_drawing_spec=mp_drawing_styles
                .get_default_face_mesh_contours_style())
            mp_drawing.draw_landmarks(
                image=image,
                landmark_list=face_landmarks,
                connections=mp_face_mesh.FACEMESH_IRISES,
                landmark_drawing_spec=None,
                connection_drawing_spec=mp_drawing_styles
                .get_default_face_mesh_iris_connections_style())

            left_gaze_ratio = gaze_ratio(face_landmarks.landmark[33],
                                         face_landmarks.landmark[133],
                                         face_landmarks.landmark[468])

            right_gaze_ratio = gaze_ratio(face_landmarks.landmark[363],
                                          face_landmarks.landmark[263],
                                          face_landmarks.landmark[473])

#            print('mean gaze ratio', (left_gaze_ratio + right_gaze_ratio) * 0.5)

            gaze_ratio_sig.set_value((left_gaze_ratio + right_gaze_ratio) * 0.5)

            mouth_size_sig.set_value([face_landmarks.landmark[291].x - face_landmarks.landmark[61].x,
                                      face_landmarks.landmark[14].y  - face_landmarks.landmark[13].y])

    cv2.imshow('MediaPipe Head Pose', image)

    if cv2.waitKey(5) & 0xFF == 27:
        break

dev.free()
face_mesh.close()
cap.release()
