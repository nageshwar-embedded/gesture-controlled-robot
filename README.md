# Gesture Controlled & Autonomous Robot (ESP32)

## 🚀 Overview
This project is a dual-ESP32 based robotic system capable of gesture-controlled navigation, autonomous obstacle avoidance, and real-time camera streaming.

The system uses ESP-NOW for low-latency wireless communication between a controller (glove-based input) and the robot, along with sensor-based decision making for autonomous movement.

---

## ✨ Key Features
- 🤖 Gesture-based control using ESP-NOW communication
- 🚗 Autonomous obstacle avoidance using ultrasonic sensor
- 🎥 Real-time video streaming using ESP32-CAM
- ⚡ PWM-based motor control with L298N driver
- 🛑 Emergency braking and smooth stopping logic
- 📡 Wireless dual-ESP32 architecture
- 🧠 Modular firmware design (separated control, sensors, communication)

---

## 🧠 System Architecture
This project is divided into modular firmware components:

- **motor_control** → Handles movement, braking, direction logic  
- **sensor_ultrasonic** → Measures distance for obstacle detection  
- **espnow_comm** → Wireless communication between ESP32 devices  
- **auto_mode** → Autonomous navigation logic  
- **camera** → ESP32-CAM streaming system  

---

## 🛠 Hardware Components
- ESP32 (2 units)
- ESP32-CAM
- Ultrasonic Sensor (HC-SR04)
- L298N Motor Driver
- DC Motors + Chassis
- Power Supply (Battery)

---

## 💻 Technologies Used
- Embedded C / Arduino Framework  
- ESP-NOW (low-latency wireless communication)  
- PWM Motor Control  
- HTTP Server (for camera streaming)  
- Sensor interfacing  

---

## 📂 Project Structure
