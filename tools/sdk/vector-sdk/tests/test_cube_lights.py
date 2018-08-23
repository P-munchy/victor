#!/usr/bin/env python3

'''
Test the cube lights

Attempts to connect to a cube.
If successful sets its lights all to yellow for 2.5 seconds, then red, green, blue & white for 2.5 seconds
'''

import os
import sys
import time

sys.path.append(os.path.join(os.path.dirname(__file__), '..'))
import anki_vector  # pylint: disable=wrong-import-position


def main():
    '''main execution'''
    args = anki_vector.util.parse_test_args()

    print("------ begin cube light interactions ------")

    # The robot connects to a cube, and performs a number of manipulations on its lights
    with anki_vector.Robot(args.name, args.ip, str(args.cert), port=args.port) as robot:

        # ensure we are connected to a cube
        robot.world.connect_cube()

        connected_cubes = robot.world.connected_light_cubes
        if connected_cubes:
            cube = connected_cubes[0]

            # Set cube lights to yellow
            cube.set_lights(anki_vector.lights.yellow_light)
            time.sleep(2.5)

            # Set cube lights to red, green, blue, and white
            cube.set_light_corners(anki_vector.lights.blue_light,
                                   anki_vector.lights.green_light,
                                   anki_vector.lights.red_light,
                                   anki_vector.lights.white_light)
            time.sleep(2.5)

            # Turn off cube lights
            cube.set_lights_off()

            print("------ finish cube light interactions ------")
        else:
            print("------ FAILURE: No connected cubes found ------")


if __name__ == "__main__":
    main()
