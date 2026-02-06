import cv2
import time
from Lane_Detection import getLaneCurve
from Ultrasonic_Sensor import measure_distance
from IMU_MPU6050_Sensor import read_mpu6050
from Motor_Driver import Motor

def main():
    # Initialize camera and motor
    cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        raise RuntimeError("Cannot open camera")
    # Create Motor instance and open device (note: Motor._init_ is used to open)
    motor = Motor.__new__(Motor)
    motor._init_("/dev/motor_driver")

    BASE_SPEED = 0.5  # base forward speed
    velocity = 0.0
    last_time = time.time()

    try:
        while True:
            ret, frame = cap.read()
            if not ret:
                print("Failed to capture frame")
                break

            # Compute lane curve from the frame
            curve = getLaneCurve(frame)

            # Read IMU data and estimate speed
            sensor = read_mpu6050()
            accel_x = sensor.get('accel_x', 0)
            # Convert raw accel to m/s^2 (±2g range, 16384 LSB = 1g)
            accel_x_m_s2 = accel_x * 9.8 / 16384.0
            now = time.time()
            dt = now - last_time if now - last_time > 0 else 1e-3
            last_time = now
            velocity += accel_x_m_s2 * dt
            print(f"Speed: {velocity:.2f} m/s")

            # Read ultrasonic distance (cm)
            dist = measure_distance()
            if dist < 20.0:
                print("Obstacle detected! Stopping vehicle.")
                motor.stop()
                break

            # Calculate turning command from lane curve (0 = straight)
            turn = -curve / 100.0
            turn = max(min(turn, 1.0), -1.0)
            motor.move(speed=BASE_SPEED, turn=turn, t=0)

            # Display camera frame
            cv2.imshow("Camera", frame)
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

    except KeyboardInterrupt:
        print("Interrupted by user. Exiting.")
    finally:
        motor.stop()
        cap.release()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    main()