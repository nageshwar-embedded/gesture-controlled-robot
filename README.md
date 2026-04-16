# Gesture Controlled & Autonomous Robot (ESP32)

> Built a dual-ESP32 embedded system combining real-time control, wireless communication, and live video streaming.

---

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
- 🧠 Modular firmware design (control, sensors, communication separated)  

---

## 🧠 System Architecture
This project is divided into modular firmware components:

- **motor_control** → Movement, braking, direction logic  
- **sensor_ultrasonic** → Distance measurement  
- **espnow_comm** → Wireless communication (ESP-NOW)  
- **auto_mode** → Autonomous navigation logic  
- **camera** → ESP32-CAM live streaming system  

---

## 🛠 Hardware Components
- ESP32 (2 units)  
- ESP32-CAM  
- Ultrasonic Sensor (HC-SR04)  
- L298N Motor Driver  
- DC Motors + Chassis  
- Battery Power Supply  

---

## 💻 Technologies Used
- Embedded C / Arduino Framework  
- ESP-NOW Communication  
- PWM Motor Control  
- HTTP Server (Camera Streaming)  
- Sensor Interfacing  

---

## ⚙️ How It Works
1. Gesture input is captured using controller ESP32  
2. Data is transmitted via ESP-NOW  
3. Robot ESP32 processes commands and drives motors  
4. Ultrasonic sensor prevents collisions  
5. ESP32-CAM streams live video to user  

---

## 📂 Project Structure
gesture-controlled-robot/
│── src/           # Core firmware  
│── camera/        # ESP32-CAM code  
│── legacy/        # Full original code  
│── docs/          # Images  
│── demo/          # Demo links  

---

## 🎥 Demo Video
👉  https://drive.google.com/drive/folders/1poaicB8XXCkoUPI4Vt8J_8MWh8E9nd23?usp=drive_link

---

## 📜 Original Implementation
The complete integrated version of the firmware is available in the `legacy/` folder.

---

## 🧠 What I Learned
- Real-time embedded system design  
- ESP-NOW wireless communication  
- Hardware + firmware debugging  
- Motor control and power management  
- Writing modular and scalable firmware  

---

## 🚀 Future Improvements
- PID-based motion control  
- Mobile app control  
- AI-based object detection  
- Extended communication range  

---

## 👨‍💻 Author
Nageshwar Singh  
B.Tech ECE | Embedded Systems Enthusiast  
GitHub: https://github.com/nageshwar-embedded
