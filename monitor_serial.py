import serial
import time
import sys

port = "/dev/cu.usbmodem101"
baud = 2000000
duration = 10

try:
    with serial.Serial(port, baud, timeout=1) as ser:
        print(f"Capturing serial output from {port} for {duration} seconds...")
        start_time = time.time()
        while (time.time() - start_time) < duration:
            line = ser.readline()
            if line:
                sys.stdout.buffer.write(line)
                sys.stdout.buffer.flush()
except Exception as e:
    print(f"Error: {e}")
