import serial
import time

try:
    s = serial.Serial('/dev/cu.usbmodem101', 460800, timeout=1)
    s.setDTR(False)
    s.setRTS(True)
    time.sleep(0.1)
    s.setRTS(False)
    time.sleep(0.1)
    print("Reading serial...")
    for _ in range(50):
        line = s.readline()
        if line:
            print(line.decode(errors='ignore').strip())
except Exception as e:
    print(f"Error: {e}")
