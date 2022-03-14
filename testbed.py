#!/usr/bin/python3 -u
from RPi import GPIO
import time

receiver = 5
GPIO.setmode(GPIO.BCM)
GPIO.setup(receiver, GPIO.IN) # Receiver is on GPIO5/pin 29?

# Setup
prev_time = time.perf_counter()
prev_pin_state = GPIO.input(receiver)
# If we just want to record the waveform, don't output intitial state?
# This could miss if the waveform starts with the pullup state but thats very unlikely
# print(0.0, prev_pin_state)

# Loop
try:
    while True:
        current_pin_state = GPIO.input(receiver)
        # Only record toggles!
        if current_pin_state == prev_pin_state:
            prev_pin_state = current_pin_state
            continue
        
        current_time = time.perf_counter()
        elapsed_sec = (current_time - prev_time)

        print(elapsed_sec, current_pin_state)
        prev_pin_state = current_pin_state
        # Let's do time between, not time overall
        prev_time = current_time
except KeyboardInterrupt:
    GPIO.cleanup()

