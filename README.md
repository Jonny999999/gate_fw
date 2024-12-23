Firmware for an automated sliding gate. Documentation: https://pfusch.zone/automated-sliding-gate

# Photos
Left: Photo of control-pcb + RC-module (V1.0)  
Right: Photo of cabinet with power relays and V0.1 board (in V1.0 new board version was used and outsourced due to EMV issues when switching)
<p align="center">
  <img src="doc/img/V1.0_controlBox.jpg" width="58%"/>
  <img src="doc/img/V0.1_controlCabinet.jpg" width="40%"/>
</p>

# Schema
![image](doc/img/schema.png)

# Installation
### Install esp-idf
For this project **ESP-IDF v4.4.1** is required  
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
- connect FTDI programmer to board (VCC to VCC; TX to RX; RX to TX)
- press REST and BOOT button
- release RESET button (keep pressing boot)
- run flash command:
```bash
idf.py flash
```
- once "connecting...' successfully, BOOT button can be released  
Note: it is known that the **right gate opens while flashing**...  
(the relay still gets turned off by limit switch, so there will be no damage)

### Monitor
- connect FTDI programmer to board (VCC to VCC; TX to RX; RX to TX)
- press REST and BOOT button
- release RESET button (keep pressing boot)
- run monitor command:
```bash
idf.py monitor
```
- once connected release BOOT button
- press RESET button once for restart





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





# Pin assignment
See connection plan: [connection-plan.odp](connection-plan.odp)
## Inputs
### Buttons, Remote control
Buttons in gate and panel, remote receiver switch to 12V
| Pin | Object | Variable | Description | Wire No (box -> pcb) |
| --- | --- | --- | --- | --- |
| 27 | buttonOpen | GPIO_S_OPEN | S8 Button top [open] | 9 (switches to VCC) |
| 14 | buttonClose | GPIO_S_CLOSE | S7 Button bottom [close] | 10 (switches to VCC) |
|  |  |  |  |
| 36 | remoteOpen | GPIO_S_REMOTE_OPEN | Remote receiver [open] | (switches to GND) |
| 39 | remoteClose | GPIO_S_REMOTE_CLOSE | Remote receiver [close] | (switches to GND) |

### Limit switches
Limit switches switch to 12V
| Pin | Object | Variable | Description | Wire No (box -> pcb) |
| --- | --- | --- | --- | --- |
| 32 |  | GPIO_B_RIGHT_OPEN | S1 right gate(1) open | 14 |
| 33 |  | GPIO_B_RIGHT_CLOSED | S2 right gate(1) closed | 13 |
| 25 |  | GPIO_B_LEFT_OPEN | S3 left gate(2) open | 12 |
| 26 |  | GPIO_B_LEFT_CLOSED | S4 left gate(2) closed | 11 |
|  |  |  | |  |



## Outputs
### Leds, buzzer
| Pin | Object | Variable | Description |
| --- | --- | --- | --- |
| 12 |  |  | Buzzer/LED on the board (select via jumper) |
| 12 |  |  |  |

### Relays
right 5x screw terminal (Servo driver):
when gpio is high output is switched to gnd
| Pin | Object | Variable | Description | Wire No (box -> pcb) |
| --- | --- | --- | --- | --- |
| 15 |  | GPIO_K_OPEN_RIGHT | ST1 K1 open right gate(1) | 6 |
| 2 |  | GPIO_K_CLOSE_RIGHT | ST2 K2 close right gate(1) | 5 |
| 16 |  | GPIO_K_OPEN_LEFT | ST3 K3 open left gate(2) | 4 |
| 4 |  | GPIO_K_CLOSE_LEFT | ST4 K4 close left gate(2) | 3 |

