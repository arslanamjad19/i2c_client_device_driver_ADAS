import time

def read_mpu6050(device_path="/dev/mpu6050"):
    """
    Reads raw sensor data from the MPU6050 character device and returns a dict.
    """
    with open(device_path, "r") as f:
        lines = f.readlines()
    
    data = {}
    for line in lines:
        key, value = line.split(":")
        value = value.strip()
        # Temperature comes as "xx.yy C"
        if key.strip() == "temp":
            # strip ' C' and convert to float
            temp_str = value.replace(" C", "")
            data["temp_c"] = float(temp_str)
        else:
            data[key.strip()] = int(value)
    return data

def main(poll_interval=1.0):
    """
    Continuously reads from the sensor every poll_interval seconds
    and prints values.
    """
    try:
        while True:
            sensor = read_mpu6050()
            print(f"Accel X: {sensor['accel_x']}")
            print(f"Accel Y: {sensor['accel_y']}")
            print(f"Accel Z: {sensor['accel_z']}")
            print(f"Temp   : {sensor['temp_c']:.2f} °C")
            print(f"Gyro X : {sensor['gyro_x']}")
            print(f"Gyro Y : {sensor['gyro_y']}")
            print(f"Gyro Z : {sensor['gyro_z']}")
            print("-" * 40)
            time.sleep(poll_interval)
    except KeyboardInterrupt:
        print("Exiting.")

if __name__ == "__main__":
    main()