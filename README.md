# I2C Client Device Driver & ADAS System

This repository contains a Linux-based Advanced Driver Assistance System (ADAS) implementation for a Raspberry Pi-based autonomous vehicle. It combines custom Linux kernel drivers for hardware interfacing (Sensors, Motors) with a Python-based high-level application for lane detection, object avoidance, and autonomous control.

## 📌 Project Overview

The system operates by interfacing directly with hardware via custom Kernel Modules (written in C) and processing high-level logic in User Space (written in Python).

**Key Features:**
*   **Lane Detection:** Computer vision-based lane tracking using OpenCV.
*   **Obstacle Avoidance:** Real-time distance measurement using an ultrasonic sensor.
*   **Speed Estimation:** Inertial measurement using an MPU6050 accelerometer/gyroscope.
*   **Autonomous Motor Control:** Logic to adjust steering and speed based on sensor inputs.

## 🏗️ Architecture

The project is divided into two main layers:
1.  **Kernel Space (C Drivers):** Handles direct hardware register access, interrupts, and protocol communication (I2C, GPIO).
2.  **User Space (Python Application):** Handles computer vision, sensor data aggregation, and control logic.

![Code Hierarchy](https://github.com/arslanamjad19/i2c_client_device_driver_ADAS/blob/342c77297592a3da122b6ee935ebe9a98de9c5b4/Code_Heirarchy.png)
<!-- Placeholder: Insert a diagram showing the flow from Python App -> /dev/ nodes -> Kernel Drivers -> Hardware -->

## 📂 File Structure

```text
├── IMU_I2C.c               # Linux Kernel Module for MPU6050 (I2C)
├── Ultrasonic_Sensor.c     # Linux Kernel Module for HC-SR04 (GPIO/HR Timer)
├── LED_GPIO.c              # Linux Kernel Module for GPIO control
├── README.md               # Project Documentation
└── adas_features/          # Python ADAS Application
    ├── autonomous_vehicle_main.py  # Main entry point for autonomous driving
    ├── Lane_Detection.py           # OpenCV lane detection pipeline
    ├── IMU_MPU6050_Sensor.py       # Interface for MPU6050 data
    ├── Ultrasonic_Sensor.py        # Interface for Ultrasonic data
```

🛠️ Hardware Requirements
Compute: Raspberry Pi 4 (BCM2711 based)
Sensors:
MPU6050 (Accelerometer/Gyroscope) connected via I2C.
HC-SR04 (Ultrasonic Sensor) connected via GPIO.
Camera Module (Pi Camera or USB Webcam).
Actuators:
DC Motors with Driver (e.g., L298N).
⚙️ Installation & Build
1. Build Kernel Modules
You must have the Linux kernel headers installed for your running kernel version.

```bash
# Example Makefile commands (Create a Makefile if not present)
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```
2. Load Drivers
Insert the compiled kernel modules (.ko files) into the kernel.
```bash
# Load MPU6050 Driver
sudo insmod IMU_I2C.ko

# Load Ultrasonic Driver (Configuration parameters available)
sudo insmod Ultrasonic_Sensor.ko trig_pin=17 echo_pin=27

# Load GPIO/LED Driver
sudo insmod LED_GPIO.ko
```
Note: Ensure device tree overlays are configured correctly for I2C if necessary.

🚀 Usage
Running the Autonomous Loop
The main Python script integrates all features. It captures video, calculates lane curvature, checks for obstacles, and sends commands to the motor driver.

```bash
cd adas_features
sudo python3 autonomous_vehicle_main.py
```
🧩 Module Details
IMU Driver (```IMU_I2C.c```)
Device: ```/dev/mpu6050```
Functionality: Initializes the MPU6050, configures power management, and exposes raw accelerometer and gyroscope data.
Interface: Read from the device file to get formatted sensor data.
Ultrasonic Driver (```Ultrasonic_Sensor.c```)
Interface: ```/proc/hcsr04/text``` (Human readable) and ```/proc/hcsr04/raw``` (Raw integer).
Functionality: Uses ```ktime``` and GPIO interrupts to measure the time-of-flight of the ultrasonic pulse with high precision in kernel space, avoiding user-space scheduling jitter.
Lane Detection (```Lane_Detection.py```)
Pipeline:
Thresholding: Filters for white lane markings using HSV color space.
Warping: Applies a perspective transform (Bird's Eye View).
Histogram: Calculates the pixel density to find the lane center.
Curve Calculation: Determines the steering offset required to center the vehicle.
📝 Configuration
Pin assignments and calibration values can be adjusted in the respective files:

Ultrasonic Pins: Modify ```module_param``` defaults in ```Ultrasonic_Sensor.c``` or pass as insmod arguments.
Camera Calibration: Adjust ```valTrackbars``` defaults in ```Lane_Detection.py```.
