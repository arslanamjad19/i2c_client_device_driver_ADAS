import os
from time import sleep

class Motor:
    def _init_(self, device_path="/dev/motor_driver"):
        self.device_path = device_path
        try:
            self.device = os.open(self.device_path, os.O_WRONLY)
        except OSError as e:
            raise RuntimeError(f"Cannot open motor driver: {e}")

    def move(self, speed=0.5, turn=0, t=0):
        # Convert to percent duty cycle
        speed *= 100
        turn *= 100
        leftSpeed = speed - turn
        rightSpeed = speed + turn

        # Clamp the speeds
        leftSpeed = max(min(leftSpeed, 100), -100)
        rightSpeed = max(min(rightSpeed, 100), -100)

        # Send command to device driver
        cmd = f"MOVE {int(leftSpeed)} {int(rightSpeed)}\n"
        os.write(self.device, cmd.encode())

        sleep(t)

    def stop(self, t=0):
        os.write(self.device, b"STOP\n")
        sleep(t)

    def _del_(self):
        if hasattr(self, 'device'):
            os.close(self.device)


def main():
    motor.move(0.6, 0, 2)
    motor.stop(1)
    motor.move(-0.5, 0.2, 2)
    motor.stop(1)

if _name_ == '_main_':
    motor = Motor("/dev/motor_driver")
    main()