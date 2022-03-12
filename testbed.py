#!/usr/bin/env python3
from RPi import GPIO

receiver = 5
GPIO.setmode(GPIO.BCM)
GPIO.setup(receiver, GPIO.IN) # Receiver is on GPIO5/pin 29?
GPIO.setup(13, GPIO.OUT) # Transmitter is on GPIO13/pin33

try:
    while True:
        print(GPIO.input(receiver))
except KeyboardInterrupt:
    GPIO.cleanup()

