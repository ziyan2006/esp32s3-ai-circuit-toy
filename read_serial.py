import serial
import time
import sys

port = 'COM14'
baudrate = 115200
duration = 15.0 # Read logs for 15 seconds

print(f"Opening {port} at {baudrate} baud rate (no reset)...")
try:
    ser = serial.Serial(port, baudrate, timeout=0.1, rtscts=False, dsrdtr=False)
except Exception as e:
    print(f"Error opening port: {e}")
    sys.exit(1)

# Ensure DTR/RTS are set to allow USB CDC output
ser.dtr = True
ser.rts = True
time.sleep(0.1)

print(f"Connected. Streaming logs for {duration} seconds...\n")
start_time = time.time()
try:
    while time.time() - start_time < duration:
        line = ser.readline()
        if line:
            print(line.decode('utf-8', errors='ignore'), end='')
        else:
            time.sleep(0.01)
except KeyboardInterrupt:
    print("\nInterrupted.")
finally:
    ser.close()
    print("\nPort closed.")
