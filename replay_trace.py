#!/usr/bin/env python3
from RPi import GPIO
import sys
import time

transmitter = 13 # Transmitter is on GPIO13/pin 33
GPIO.setmode(GPIO.BCM)
GPIO.setup(transmitter, GPIO.OUT)

with open(sys.argv[1], 'r') as f:
    lines = f.readlines()

t, b = [], []
for line in lines:
    elapsed, bit = line.split()
    elapsed = float(elapsed)
    bit = int(bit)
    t.append(elapsed)
    b.append(bit)

t[0] = 0.0 # Fixing the initial offset being very large

for elapsed, bit in zip(t, b):
    time.sleep(elapsed)
    GPIO.output(transmitter, bit)

GPIO.cleanup()
