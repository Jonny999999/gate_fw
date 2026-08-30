# Roadmap / Course of Action

Working document for the ongoing rework of `fw_v2.0`.
Purpose: keep the plan and the reasoning in the repo so work can be picked up at any
time without re-explaining the context.

- **Baseline:** tag `V2.1` = state that ran reliably in production from 2025-04 until now.
- **Current branch:** `cleanup-rework` — refactor, bugfixes and documentation **only**,
  no new features. New features get their own branch afterwards.

Status legend: `[ ]` open · `[~]` in progress · `[x]` done

---

## Phase 1 — Cleanup & rework (branch `cleanup-rework`)

No functional changes intended beyond fixing the defects listed under 1.1.

### 1.1 Bugs found during analysis

| # | Where | Problem |
|---|-------|---------|
| B1 | `components/gpio/gpio_evaluateSwitch.cpp` | `msPressed` is **stale on the rising edge** and is not updated on the press→release transition. Combined with `state` lagging the release by `minOffMs`, a *short* press can be evaluated using `msPressed` from the *previous* press → false long-press. Self-perpetuating once it happens once. **Root cause of the reported long-press bug.** |
| B2 | `control.cpp` (whole control task) | `Gate::handle()` / `startMovement()` / `stop()` perform **blocking Modbus transactions** (10–150 ms each, up to ~600 ms with retries) inside the control loop. Button sampling stops for that entire time, so debounce/edge timing is not deterministic. Amplifies B1. |
| B3 | `gate.cpp:285` `Gate::pause()` | `stop()` sets `state = IDLE_PARTIALLY_OPEN` *before* `wasOpeningBeforePause = (state == MOVING_OPENING)` is evaluated → always `false`. `resume()` therefore always resumes *closing*. Currently masked because pause is only used while closing. |
| B4 | `gate.cpp:300-306` `Gate::resume()` | `targetRunTimeMs` is `uint64_t`; `targetRunTimeMs*1000 - elapsedSinceStart` **underflows** when the elapsed time exceeds the target, and the guard `if (targetRunTimeMs <= 0)` can never be true for an unsigned type → resume with an enormous run time. |
| B5 | `gate.cpp:135` `startMovement()` | Unit mismatch: microsecond difference compared against `DELAY_VFD_STARTUP` in **milliseconds** (missing `* 1000`). The "relay was only just switched on" branch is effectively dead. |
| B6 | `gate.cpp:142` `startMovement()` | `lastPositionUpdateTimestampUs = timestampStartUs;` is assigned **before** `timestampStartUs` is refreshed → the first `updatePosition()` of every movement integrates the time since the *previous* movement → position estimate jumps to 0 % / 100 % immediately. Position tracking is effectively non-functional. |
| B7 | `gate.cpp` ctor | `timestampStartUs`, `targetRunTimeMs`, `nextDirection`, `lastActivityTimestampUs`, `timestampRelayTurnedOnUs`, `lastPositionUpdateTimestampUs` are **never initialised** → undefined behaviour on the first cycles after boot. |
| B8 | `modbus.c:138-155` | `uint8_t response[8]` is **uninitialised** and `memcmp(frame, response, 8)` is executed without checking `len == 8` → a short/absent reply is compared against stack garbage. |
| B9 | `control.cpp:71-73` | `#if (BARRIER_IS_IGNORED) return false #endif` — **missing semicolon**; enabling the debug switch breaks the build. |
| B10 | `buzzer.cpp:93-97` | Struct initialised with `{ count = count, msOn = msOn, ... }` — those are *assignments*, not designated initialisers. Works by accident through evaluation order. |
| B11 | `gate.cpp` | `runDurationMs + 5000` (15000 ms for gate 1) equals `kMovingTimeout` (15000 ms) → the timeout branch is checked first, so a missed limit switch ends in `ERROR_STATE` at the exact same instant. Margins should be explicit and separated. |
| B12 | `modbus.c` | No mutex around the shared RS485 bus. Correct today only because a single task touches it; breaks silently as soon as gate handling is moved into its own task. |
| B13 | `gate.cpp` | `ERROR_STATE` immediately self-transitions to `IDLE_PARTIALLY_OPEN` in the next `handle()` cycle; `control.cpp` relies on catching it inside that one cycle (see comment at `control.cpp:238`). Fragile — the error must be latched. |
| B14 | `gate.cpp:325` | Side-effecting calls (`checkLimitSwitch*Active()` update `prev*SwitchState` and log) inside an `ESP_LOGV()` argument list — compiled out at the current log level, so behaviour changes with the log level. |

### 1.2 Structure / architecture

- [ ] **Decouple input sampling from the control loop.** Sample and debounce all buttons /
      remote / light barrier at a fixed, jitter-free rate (dedicated high-priority task or
      an `esp_timer` periodic callback) and hand *events* to the control task through a
      queue. Fixes the whole class of problems behind B1/B2.
- [ ] **Move VFD/Modbus I/O off the control task.** Either a dedicated VFD task fed by a
      command queue, or make `Gate` non-blocking with an explicit "waiting for Modbus"
      state. The control state machine must never block.
- [ ] **Single time source.** Replace the mix of `esp_log_timestamp()` (10 ms granularity
      at `CONFIG_FREERTOS_HZ=100`, and a *logging* API) and raw `esp_timer_get_time()`
      with one small helper (`millis()` on top of `esp_timer_get_time()`), consistently
      in ms, with wrap-safe comparisons.
- [ ] **Split `control.cpp`.** It currently mixes user-input interpretation, light-barrier
      logic, fault-LED handling and gate sequencing. Proposed split:
      `input` (events) · `control` (state machine) · `indicator` (LED + buzzer patterns).
- [ ] **Own the objects properly.** `vfd1/2`, `gate1West/gate2East` and `controlConfig` are
      locals in `app_main()` kept alive only by `while(1) vTaskDelay(portMAX_DELAY)`.
      Make them file-scope or heap-allocated and drop the keep-alive hack.
- [ ] **Move the buzzer task into `buzzer_t`** (`createTask()`), as the existing TODO says.
- [ ] **Turn `components/gpio`** into a proper reusable debounce component with unit-testable
      logic (see 1.3), or replace it outright.
- [ ] **`config.h` split:** GPIO/pin mapping vs. behaviour tuning (timings, thresholds).
      Timing constants currently live scattered across `control.cpp` and `gate.hpp`.
- [ ] **Remove dead code:** `Kconfig.projbuild` (leftover from the ESP-IDF RS485 echo
      example, unused), the `RUN_GATE_TEST` / `RUN_MODBUS_TEST` / `RUN_GPIO_TEST` blocks in
      `main.cpp` (move to a separate `selftest` module or delete), `gateHandleTask()`.
- [ ] **Consistent language & style:** decide on one comment language, one naming scheme
      (`buzzer_t` vs `Gate` vs `VFD`), consistent `esp_err_t` returns (`modbus.c` returns
      a bare `-2` in one place, `0` in another).

### 1.3 Verification

- [ ] Add a host-side test for the debounce/long-press state machine (feed a synthetic
      level+timestamp sequence, assert the events). Cheap and catches B1-type regressions.
- [ ] Note the manual test procedure in the README (which sequences to try on the real gate).

### 1.4 Documentation

- [ ] **README:** the *Usage* section links `function-diagram.drawio.pdf` which **does not
      exist at that path** (only `fw_v1.0-legacy/function-diagram.drawio.pdf`, and that one
      describes V1). Either redraw for V2 or drop the link.
- [ ] Document the V2 control flow (states, timings, light-barrier behaviour) — text and/or
      a new diagram.
- [ ] Update the *Usage* section for the reworked UI (see Phase 2).
- [ ] Add a short "firmware architecture" section: tasks, queues, modules, who owns the
      RS485 bus.
- [ ] Note the ESP-IDF version actually in use (`sdkconfig` says `5.3.2`) and the
      flashing quirk (east gate must be slightly open) — the latter is already there, keep it.
- [ ] Document the VFD parameter set required on the drives (addresses 11 / 77, Modbus
      control mode, baud rate) — currently only implicit in `doc/vfd/`.

---

## Phase 2 — UI / operating-concept rework

Depends on Phase 1 (a reliable, deterministic input layer).

### 2.1 Reliable button handling
- [ ] Long-press must be unambiguous: evaluate it on a **clean, per-press** measurement,
      report `LONG_PRESS` once the hold time is exceeded *while still held*, and report
      `SHORT_PRESS` only on release below the threshold. No reliance on carried-over state.
- [ ] Review `FULLY_OPEN_LONG_PRESS_DURATION_MS` (currently 600 ms) once the timing is
      deterministic — probably raise to ~800–1000 ms for a clear separation.

### 2.2 Simplify the "open further" sequence
The current scheme (`IDLE → WAIT_FOR_INPUT`, repeated presses within
`min(BUTTON_PRESS_INITIAL_OPEN_TIME_MS, BUTTON_PRESS_AGAIN_OPEN_INCREMENT_MS)` add
`700 ms` each) is effectively unused: the window is hard to hit and it competes with
long-press detection.
- [ ] Decide: **remove it**, or replace it with "press-and-hold = keep moving, release =
      stop" (jog mode), which is self-explanatory and needs no timing window.
- [ ] Recommendation to evaluate: keep exactly three unambiguous gestures on the open
      button — *short* = small opening, *long* = fully open, *press while moving* = stop.

### 2.3 New gesture: "let me through, then close behind me"
Requested behaviour: one easy, unmistakable sequence that opens the small gap and
auto-closes after a delay.
- [ ] Proposed gesture: **short press, then long press** (`1 short + 1 long`) on the
      open button, within a generous window (~2 s).
- [ ] Behaviour: open to the small opening → hold for `AUTO_CLOSE_DELAY_MS` (~20 s,
      configurable) → run the same warning-beep countdown already used when resuming
      after the light barrier clears → close completely.
- [ ] Safety: the light barrier already guards the closing movement, so an unattended
      auto-close is acceptable. Auto-close must still be cancellable by any button press
      and must respect the existing barrier pause/timeout logic.
- [ ] Needs a distinct state in the control state machine (e.g. `WAIT_AUTO_CLOSE`) and a
      clear acoustic confirmation that auto-close is armed.
- [ ] Consider making the auto-close delay restart while the barrier is obstructed
      (someone still standing in the gap).

---

## Phase 3 — Future hardware expansions

Both are already prepared on the PCB (`pcb_v2.0-isolated-gate-control`).

### 3.1 Servo lock
- [ ] Servo on `CONFIG_SERVO_PWM_GPIO` (GPIO 33), supply enable via
      `CONFIG_SERVO_ENABLE_GPIO` (GPIO 12, P-MOSFET).
- [ ] Mechanically locks the gate overnight against forced opening.
- [ ] New `LOCKED` control state (there is already a `TODO` for it in `control.cpp:38`):
      reject movement commands while locked; unlock → wait → move.
- [ ] Trigger: time-of-day schedule, function button, and/or remote. Needs a decision —
      an RTC/NTP source is not currently present.
- [ ] Power the servo only while actuating (that is what the enable MOSFET is for).

### 3.2 Encoders
- [ ] Pulse inputs on `CONFIG_ENCODER1_GPIO` (GPIO 35) / `CONFIG_ENCODER2_GPIO` (GPIO 32).
- [ ] Replaces the time-based position estimate in `Gate::updatePosition()` (which is
      broken today anyway, see B6) with a real distance measurement.
- [ ] Enables: exact partial-open positions, stall/obstruction detection independent of the
      VFD current reading, plausibility check against the limit switches.
- [ ] Use PCNT (hardware pulse counter) rather than GPIO interrupts.
- [ ] Keep the time-based estimate as a fallback when no encoder pulses arrive.

---

## Open questions / decisions to make

- [ ] Should the "open further by pressing again" feature be removed entirely, or replaced
      by jog mode? (2.2)
- [ ] Auto-close delay: fixed 20 s, or configurable via the function button?
- [ ] The function button (`CONFIG_FN_BUTTON_GPIO`, GPIO 34) is wired but **completely
      unused in the firmware** — reserve it for lock/unlock, or a service menu?
- [ ] Is a WiFi/MQTT/web status interface wanted at some point, or should the controller
      stay strictly offline?
