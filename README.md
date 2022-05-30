# Installation
### Install esp-idf
```bash
yay -S esp-idf #alternatively clone the esp-idf repository from github
/opt/esp-idf/install.sh
```
### Clone this repo
```
git clone git@github.com:Jonny999999/gate_fw.git
```

# Compilation
### Set up environment
```
source /opt/esp-idf/export.sh
```
(run once in terminal)

### Compile
```
idf.py build
~~make~~
```

### Upload
```
idf.py flash
~~make flash~~
```

### Monitor
```
idf.py monitor
~~make monitor~~
```




# Pin assignment
## Inputs
### Buttons
Buttons in gate and panel, remote receiver switch to 12V
| Pin | Object | Variable | Description | Wire No (box -> pcb) |
| --- | --- | --- | --- | --- |
| 27 | buttonOpen | GPIO_S_OPEN | S8 Button top [open] | 9 |
| 14 | buttonClose | GPIO_S_CLOSE | S7 Button bottom [close] | 10 |
|  |  |  |  |
| 36 | remoteOpen | GPIO_S_REMOTE_OPEN | Remote receiver [open] |
| 39 | remoteClose | GPIO_S_REMOTE_CLOSE | Remote receiver [close] |

### Limit switches
Limit switches switch to 12V
| Pin | Object | Variable | Description | Wire No (box -> pcb) |
| --- | --- | --- | --- | --- |
| 32 |  | GPIO_B_RIGHT_OPEN | S1 right gate(1) open | 14 |
| 33 |  | GPIO_B_RIGHT_CLOSED | S2 right gate(1) closed | 13 |
| 25 |  | GPIO_B_LEFT_OPEN | S3 left gate(2) open | 12 |
| 26 |  | GPIO_B_LEFT_CLOSED | S4 left gate(2) closed | 11 |
| ? |  | ? | B1 Light beam (not connected) | 3 |



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

