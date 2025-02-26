Custom pcb-design and firmware for an esp32 controlling a self-made automated sliding gate.  
Full Documentation: https://pfusch.zone/automated-sliding-gate


# Changelog V2.0
- Replace old multipurpose board with a custom pcb for controlling the gate
- custom pcb:
  - fully isolate microcontroller from long cables and VFD noise
  - RS485 interface
  - Servo interface to lock the gate
  - Preparation for Encoders
- control motors with VFDs instead of Relays
- firmware rework or rewrite

<br>

## pcb v2.0-isolated-gate-control
- **[KiCad Project](pcb_v2.0-isolated-gate-control/)**
- **[Schematic.pdf](pcb_v2.0-isolated-gate-control/export/schematic.pdf)**
- **[G-code for Isolation Milling](pcb_v2.0-isolated-gate-control/pcb2gcode)**  
  
- **[Connection plan](doc/V2.0_connection-plan.drawio.pdf)**

Schematic + layout preview:  
<p align="center">
  <a href="pcb_v2.0-isolated-gate-control/export/schematic.pdf">
    <img src="pcb_v2.0-isolated-gate-control/export/schematic.svg" width="50%" alt="Schematic"/>
  </a>
  <img src="pcb_v2.0-isolated-gate-control/export/layout.jpg" width="40%" alt="PCB Layout"/>
</p>

  
<br>

## Photos
Photos of the V2.0 hardware setup (new custom made pcb, control cabinet and additional box with outsourced power supply and VFDs)
<p align="center">
  <img src="doc/img/V2.0_pcb_isolated-gate-control.jpg" width="58%"/>
  <img src="doc/img/V2.0_control+vfd-box.jpg" width="40%"/>
</p>


<br>


# Installation
### Install esp-idf
For this project **ESP-IDF v5.3** is required  
(with other versions it most likely will not compile)
```bash
#download esp-idf (verify version!)
yay -S esp-idf #alternatively clone the esp-idf repository from github
#run installation script in installed folder
/opt/esp-idf/install.sh
```
### Clone this repo
```
git clone git@github.com:Jonny999999/gate_fw.git
```

# Compilation
### Set up environment
```bash
source /opt/esp-idf/export.sh
```
(run once in terminal)

### Compile
```bash
idf.py build
```

### Upload
**Important:** Since V2.0 the **east Gate has to be slightly open for the upload to work**  
(Since the gpio used for limit switch fully closed has to be pulled low during flashing)
- connect micro-usb cable to ESP32 module on pcb
- press REST and BOOT button
- release RESET button (keep pressing boot)
- run flash command:
```bash
idf.py flash
```
- once "connecting...' successfully, BOOT button can be released  


# Usage
See [function diagram](function-diagram.drawio.pdf) for a detailed overview of the implemented control.

### Open slightly
- press open button once -> opens for ~1s

### Open more
- press open button several times with less than 1s gap  
  -> opens 1s + 0.4s * times-pressed

### Open completely
- press open button for more than 0.8s
or
- press 'A-Button' on remote once

### Close completely
- press close button once  
  or
- press 'B-Button' on remote once

### Stop moving gate
- press any button (on gate or remote) while a gate moves  
Note: within 1s after pressing open button only close button or remote works to stop the gates



<br>

---

# Legacy -  Old Version V1.0
Left: Photo of control-pcb + RC-module (V1.0)  
Right: Photo of cabinet with power relays and V0.1 board (in V1.0 new board version was used and outsourced due to EMV issues when switching)
<p align="center">
  <img src="doc/img/V1.0_controlBox.jpg" width="58%"/>
  <img src="doc/img/V0.1_controlCabinet.jpg" width="40%"/>
</p>

# Schema
![image](doc/img/schema.png)