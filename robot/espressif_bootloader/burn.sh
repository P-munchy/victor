#!/usr/bin/env bash

SDK_BASE=/Volumes/devel/GitHub/products-cozmo/robot/espressif

python2 $SDK_BASE/tools/esptool.py --port $1 --baud 115200 write_flash --flash_size 16m --flash_freq 80m \
        0x000000 firmware/rboot.bin \
        0x001000 firmware/sn.bin \
        0x002000 ../espressif/bin/blank.bin \
        0x003000 ../espressif/bin/upgrade/user1.2048.new.3.bin \
        0x1fc000 ../espressif/bin/esp_init_data_default.bin \
        0x1fe000 ../espressif/bin/blank.bin
