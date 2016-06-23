#!/usr/bin/env python3
from cozmoInterface import CozmoInterface
import numpy as np
import math
import time
import cv2

"""This is an example program to display cozmo's camera. While you are in the camera
window, you can control cozmo with the arrow keys (there is a lot of latency and acceleration)
and control his head and lift with s,x and a,z. you can increase his speed by holding + or - 
and python will show you his new speed. """

cozmo = CozmoInterface(False, 0)

cozmo.StartSim()
time.sleep(1)
cozmo.SetRobotImageSendMode()

keyLog = []
held = False
speed = 20
width = 320
height = 240
while True:

    state = cozmo.GetState()
    image = state.GetImage()
    # Numpy's non-empty check
    if (image != None) and image.any():
        res = cv2.resize(image,(2*width, 2*height), interpolation = cv2.INTER_CUBIC)
        for cube in state.lightCubes:
            if cube:
                x = int(cube.img_topLeft_x)*2
                y = int(cube.img_topLeft_y)*2
                w = int(cube.img_width)*2
                h = int(cube.img_height)*2
                cv2.rectangle(res, (x,y), (x+w, y+h), (0,0,255)) 
        cv2.imshow("CozmoView", res)
        k = cv2.waitKey(5);
        keyLog = [k] + keyLog
        if len(keyLog) < 5:
            continue
        else:
            # the amount of not -1 presses in the key log
            presses = [key for key in keyLog if (key != -1)]
            if not held and (len(presses) > 1):
                held = True
                if presses[0] == 63235:
                    cozmo.DriveWheels(speed,-speed,speed,-speed)
                # Left arrow key
                elif presses[0] == 63234:
                    cozmo.DriveWheels(-speed,speed,-speed,speed)
                # Up Arrow key:
                elif presses[0] == 63232:
                    cozmo.DriveWheels(speed,speed,speed,speed)
                # Down arrow key:
                elif presses[0] == 63233:
                    cozmo.DriveWheels(-speed,-speed,-speed,-speed)
                # a
                elif presses[0] == 97:
                    cozmo.MoveLift(1)
                # z
                elif presses[0] == 122:
                    cozmo.MoveLift(-1)
                #s
                elif presses[0] == 115:
                    cozmo.MoveHead(1)
                #x
                elif presses[0] == 120:
                    cozmo.MoveHead(-1)

                #+
                elif presses[0] == 61:
                    speed += 20
                    print("NEW SPEED = " + str(speed))
                elif presses[0] == 45:
                    speed -= 20
                    print("NEW SPEED = " + str(speed)) 
                continue

            elif held and (len(presses) < 1):
                cozmo.Stop()
                held = False
            else:
                pass

        # Escape to break
        if k == 27:
            break        
        keyLog = keyLog[:5]



cozmo.Stop()
time.sleep(1)
cozmo.Shutdown()

