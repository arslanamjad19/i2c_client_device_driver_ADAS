import time

# Path to your procfs file exposed by the ultrasonic driver
PROCFS_PATH = "/proc/ultrasonic_data"

def measure_distance():
    """
    Reads raw pulse duration (in seconds) from the procfs file,
    then computes and returns the distance in cm.
    """
    try:
        with open(PROCFS_PATH, 'r') as f:
            # driver writes a single float value: pulse duration in seconds
            raw = f.readline().strip()
    except IOError as e:
        raise RuntimeError(f"Could not read {PROCFS_PATH}: {e}")

    try:
        pulse_duration = float(raw)
    except ValueError:
        raise RuntimeError(f"Invalid data from driver: '{raw}'")

    # speed of sound = 34300 cm/s, so distance = (duration × speed)/2?
    # If driver measures round-trip time, divide by 2.
    distance = (pulse_duration * 34300) / 2
    distance = round(distance, 2)
    return distance

if _name_ == "_main_":
    print("Starting distance measurement (via procfs). Press Ctrl+C to stop.")
    try:
        while True:
            dist = measure_distance()
            print(f"Distance: {dist} cm")
            time.sleep(1)  # 1 second between readings
    except KeyboardInterrupt:
        print("\nMeasurement stopped by user.")