#!/usr/bin/python3

import cv2
import mediapipe as mp
import libmapper as mpr
import signal
mp_drawing = mp.solutions.drawing_utils
mp_hands = mp.solutions.hands

done = False

def handler_done(signum, frame):
    global done
    done = True

signal.signal(signal.SIGINT, handler_done)
signal.signal(signal.SIGTERM, handler_done)

# declare libmapper device and signals
dev = mpr.Device("mediapipe.hands")
index_tip_pos = dev.add_signal(mpr.Signal.Direction.OUTGOING, "finger/index/tip/position",
                               2, mpr.Type.FLOAT, num_inst=2)
thumb_tip_pos = dev.add_signal(mpr.Signal.Direction.OUTGOING, "thumb/tip/position",
                               2, mpr.Type.FLOAT, num_inst=2)
is_left = dev.add_signal(mpr.Signal.Direction.OUTGOING, "is_left", 1, mpr.Type.INT32, num_inst=2)

print(index_tip_pos.properties)

# Begin webcam input for hand tracking:
hands = mp_hands.Hands(min_detection_confidence=0.8,
                       min_tracking_confidence=0.5)

cap = cv2.VideoCapture(0)

width = cap.get(cv2.CAP_PROP_FRAME_WIDTH)
height = cap.get(cv2.CAP_PROP_FRAME_HEIGHT)

max_inst_id = -1

while cap.isOpened() and not done:
    success, image = cap.read()
    if not success:
        break

    # Poll the MediaPipe devices to ensure that the signals are being updated properly
    dev.poll()

    # Flip the image horizontally for a later selfie-view display, and convert
    # the BGR image to RGB.
    image = cv2.cvtColor(cv2.flip(image, 1), cv2.COLOR_BGR2RGB)

    # To improve performance, optionally mark the image as not writeable to
    # pass by reference.
    image.flags.writeable = False
    results = hands.process(image)

    # Draw the hand annotations on the image.
    image.flags.writeable = True
    image = cv2.cvtColor(image, cv2.COLOR_RGB2BGR)

    idx = -1
    if results.multi_hand_landmarks:
        for hand in results.multi_hand_landmarks:

            # ----------------------------------------
            # ESTIMATED HAND LANDMARKS ACCESSIBLE HERE
            # ----------------------------------------

            idx += 1
            print('updating instance', idx)

#            if results.multi_handedness[results.multi_hand_landmarks.index(hand)].classification[0].label == "Left":
#                is_left.Instance(idx).set_value(1)
#            else:
#                is_left.Instance(idx).set_value(0)
            index_tip_pos.Instance(idx).set_value([hand.landmark[8].x, hand.landmark[8].y])
#            thumb_tip_pos.Instance(idx).set_value([hand.landmark[4].x, hand.landmark[4].y])

            mp_drawing.draw_landmarks(
                image, hand, mp_hands.HAND_CONNECTIONS,
                mp_drawing.DrawingSpec(color=(92, 49, 29), thickness=2, circle_radius=4),
                mp_drawing.DrawingSpec(color=(201, 107, 62), thickness=2, circle_radius=2))

    to_release = idx + 1
    while to_release <= max_inst_id:
#        print('releasing instance', to_release)
        is_left.Instance(to_release).release()
        index_tip_pos.Instance(to_release).release()
        thumb_tip_pos.Instance(to_release).release()
        to_release += 1
    max_inst_id = idx

#    print('num instances:', max_inst_id + 1)
    dev.update_maps()

    cv2.imshow('MediaPipe Hands', image)

    if cv2.waitKey(5) & 0xFF == 27:
        break

dev.free()
hands.close()
cap.release()
