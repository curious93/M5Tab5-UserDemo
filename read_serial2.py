import serial
import time

try:
    s = serial.Serial('/dev/cu.usbmodem101', 460800, timeout=0.1)
    s.setDTR(False)
    s.setRTS(True)
    time.sleep(0.1)
    s.setRTS(False)
    time.sleep(0.1)
    print("Reading serial...")
    start_time = time.time()
    while time.time() - start_time < 4:
        line = s.readline()
        if line:
            print(line.decode(errors='ignore').strip())
except Exception as e:
    print(f"Error: {e}")
