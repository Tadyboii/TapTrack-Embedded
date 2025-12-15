# TapTrack Embedded Attendance System

## Project Title & Team Members
**Project Title**: TapTrack Embedded Attendance System  
**Team Members**: Tristan Jadman & Thaddeus Rosales
## Abstract
The TapTrack Embedded Attendance System is an ESP32-based solution designed to streamline RFID-based attendance tracking in educational or workplace environments. Addressing the challenges of unreliable connectivity and manual attendance methods, the system employs interrupt-driven RFID detection and a Finite State Machine (FSM) for robust online/offline operation. Key features include real-time card reading with hardware interrupts, hybrid connectivity for seamless sync, persistent local storage, and user-friendly interfaces. The expected outcome is a reliable, low-cost device that ensures accurate attendance records even in offline scenarios, with automatic Firebase synchronization when connectivity is restored, enhancing efficiency and reducing administrative overhead.

## Introduction & Motivation
In many settings, such as classrooms or offices, traditional attendance methods are time-consuming, prone to errors, and dependent on manual processes or unstable networks. Existing solutions often fail during connectivity issues, leading to data loss or incomplete records. This project aims to build an embedded system that leverages interrupt-driven architecture for instant responsiveness and FSM-based mode management for adaptability. Unlike polling-based systems that waste resources, TapTrack uses hardware interrupts for RFID detection, ensuring efficiency. The motivation stems from the need for reliable, autonomous devices in IoT applications, providing innovation through hybrid online/offline capabilities and real-time feedback, making attendance tracking more dependable and user-friendly.

## System Requirements

### Functional Requirements
1. The system shall detect RFID card taps using hardware interrupts and read the unique card UID within 100ms of detection.
2. The system shall validate users against a local database or Firebase, determining attendance status (present/late) based on timestamp (e.g., before 9 AM = present).
3. The system shall queue attendance records locally in SPIFFS when offline and sync to Firebase when online, confirming successful transmission.
4. The system shall provide visual and audio feedback (LEDs and buzzer) for successful/error events.
5. The system shall support FSM mode transitions via button presses or serial commands (AUTO, FORCE_ONLINE, FORCE_OFFLINE).
6. The system shall maintain accurate time using DS1302 RTC for timestamps, surviving power loss.
7. The system shall offer a WiFi captive portal for initial setup and credential management.

### Non-Functional Requirements
- The device must operate on 3.3V power with low power consumption (under 500mA peak).
- Firmware must occupy less than 85% of ESP32 Flash (approximately 1.1MB).
- Response time for RFID detection must be under 200ms to ensure real-time feel.
- The system must handle up to 100 users locally and sync queues of up to 500 records.
- Battery backup for RTC must maintain time for at least 1 year.
- User interface (portal and serial) must be accessible on mobile devices and simple for non-technical users.

## Preliminary System Design

### Hardware Block Diagram
```
[ESP32 Microcontroller]
    ├── SPI: MFRC522 RFID Module (SDA: GPIO21, SCK: GPIO18, MOSI: GPIO23, MISO: GPIO19, IRQ: GPIO5)
    ├── GPIO: DS1302 RTC (CLK: GPIO14, DAT: GPIO13, RST: GPIO12)
    ├── GPIO: Indicators (Buzzer: GPIO25, LED Success: GPIO26, LED Error: GPIO27)
    ├── GPIO: Mode Button (GPIO4 with pull-up)
    └── Power: 3.3V Supply (with battery for RTC)
```

### Software Architecture (FSM)
The core control logic uses a Finite State Machine with the following states and transitions:

- **States**:
  - AUTO: Default adaptive mode (online if connected, offline otherwise).
  - FORCE_ONLINE: Forces WiFi/Firebase connection.
  - FORCE_OFFLINE: Disables network features.

- **Transitions**:
  - Short button press: AUTO ↔ FORCE_ONLINE ↔ FORCE_OFFLINE (cycling).
  - Serial 'o': Any state → FORCE_ONLINE.
  - Serial 'f': Any state → FORCE_OFFLINE.
  - WiFi loss (in AUTO): FORCE_ONLINE → AUTO (offline sub-mode).
  - WiFi reconnect (in AUTO): AUTO → FORCE_ONLINE.

This FSM ensures predictable behavior, preventing conflicts and handling connectivity changes gracefully.</content>
<parameter name="filePath">/home/tan/Academic-Projects/coe185/TapTrack-Embedded/PROPOSAL.md