#!/usr/bin/env python3

# Copyright (c) 2018 Anki, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License in the file LICENSE.txt or at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Make Vector drive in a square.

This script combines the two previous examples (02_drive_and_turn.py and
03_count.py) to make Vector drive in a square by going forward and turning
left 4 times in a row.
"""

import os
import sys
import time
import vector
from vector.util import degrees, distance_mm, speed_mmps

def main():
    args = vector.util.parse_test_args()

    # The robot shall drive straight, stop and then turn around
    with vector.Robot(args.name, args.ip, str(args.cert), port=args.port) as robot:
        # Use a "for loop" to repeat the indented code 4 times
        # Note: the _ variable name can be used when you don't need the value
        for _ in range(4):
            robot.behavior.drive_straight(distance_mm(200), speed_mmps(50))
            robot.behavior.turn_in_place(degrees(90))

if __name__ == "__main__":
    main()
