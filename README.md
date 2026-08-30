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
For this project **ESP-IDF v5.5.1** is required  
(with other versions it most likely will not compile)
```bash
#clone the esp-idf repository and check out the required version
git clone -b v5.5.1 --recursive https://github.com/espressif/esp-idf.git ~/esp/v5.5.1/esp-idf
#run installation script in the cloned folder
~/esp/v5.5.1/esp-idf/install.sh esp32
```
### Clone this repo
```
git clone git@github.com:Jonny999999/gate_fw.git
```

# Compilation
### Set up environment
```bash
source ~/esp/v5.5.1/esp-idf/export.sh
```
(run once per terminal)

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

All gestures are on the **open button** unless noted otherwise. The buzzer confirms every
one of them, so the gate can be operated without looking at it.

| Gesture | Result | Sound |
|---|---|---|
| **Open button, short press** | opens a small gap (~1.9 s of travel) | one short beep |
| **Open button, press again** (while it is still waiting) | widens the gap by ~0.7 s per press | one very short beep per press |
| **Open button, hold ~0.8 s** | opens completely, stays open | one long tone |
| **Open button, keep holding ~2.5 s** | opens completely and **closes again by itself** | long tone, then long-short-short |
| **Open button: short press, then press and hold** | opens the small gap and **closes again by itself** | long tone, then long-short-short |
| **Close button** or **remote B** | closes completely | one long tone |
| **Remote A** | opens completely | one long tone |
| **Any button while a gate moves** | stops the movement | one medium tone |

### The open button, held down
The gate starts opening the moment you press, and holding on escalates. **Release when you
hear what you want** - each stage announces itself:

| Hold | Meaning | You hear |
|---|---|---|
| release quickly | small gap | one short beep |
| ~0.8 s | open completely | one long tone |
| ~2.5 s | open completely **and close again afterwards** | long tone, then two short ones |

Nothing is committed until you let go, so the familiar "hold it down to open the gate fully"
gesture is unchanged - you simply now have the option of holding on a moment longer.

### Close behind me
Two ways in, depending on how wide you need it:

- **Short press, then press and hold** - opens the small gap, for walking through.
- **Hold the first press ~2.5 s** - opens completely, for driving out.

Either way the gate closes again on its own **once the light barrier has been continuously
free for long enough** - not simply a fixed time after opening.

That distinction is the point: the gate waits until everybody is actually through and
nothing else is coming. Walking in and out, or taking a while with a trailer, restarts the
wait instead of racing a deadline.

| Opening | Barrier must stay clear for |
|---|---|
| small gap (walking through) | 10 s |
| fully open (driving out) | 120 s |

The full opening waits much longer on purpose: you open the gate, walk to the car, start it
and drive out - the barrier may not be interrupted at all for a minute or more, and the gate
must not close in front of you meanwhile.

- An accelerating beep countdown announces the closing over the last 4 seconds, the same one
  the gate uses whenever it starts moving by itself. Interrupting the light barrier during
  the countdown restarts the wait - audibly, because the beeping stops.
- If the light barrier stays interrupted for 20 seconds without a break, the gate gives up
  and stays open (two short beeps, then a long one). Whoever is there is clearly busy.
- Pressing **open** cancels it and leaves the gate open. Pressing **close** closes
  immediately instead of waiting.
- If the barrier is interrupted while it is already closing, it behaves like any other
  close: it pauses, and resumes 4 seconds after the way is clear again.

### Fault LED
The red LED keeps showing the last problem until the next movement is started. The blink
rate says how serious it is - the faster, the more attention it needs:

Two shapes, told apart at a glance:

- **Even blinking** - something went wrong. The faster, the more serious.
- **Short flash with a gap** - nothing is wrong, but the gate will move on its own in a
  while. Any button press calls it off.

| Pattern | Meaning |
|---|---|
| even, very fast (10/s) | VFD communication failed - the drives could not be reached |
| even, fast (2.5/s) | movement timeout - a limit switch was not reached in time |
| even, slow (1/s) | motor current too high - something was in the way while closing |
| even, very slow (0.5/s) | the light barrier stayed blocked, the movement was given up |
| flash every 0.5 s | movement paused, waiting for the light barrier to clear |
| flash every second | an automatic close is pending |
| steady on (while idle) | the light barrier is interrupted (useful for aligning it) |

The green panel LED is wired in parallel with the buzzer and simply echoes the beeps.

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