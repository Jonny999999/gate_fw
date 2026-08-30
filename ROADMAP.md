# Roadmap / Course of Action

Working document for the ongoing rework of `fw_v2.0`.
Purpose: keep the plan and the reasoning in the repo so work can be picked up at any
time without re-explaining the context.

- **Baseline:** tag `V2.1` = state that ran reliably in production from 2025-04 until now.
- **Current branch:** `cleanup-rework` — refactor, bugfixes and documentation **only**,
  no new features. New features get their own branch afterwards.

Status legend: `[ ]` open · `[~]` in progress · `[x]` done

**Progress:**
- `cleanup-rework`, tagged **`V2.2-rc1`** — toolchain moved to ESP-IDF 5.5.1, bugs
  B1, B3–B10, B14, B15 fixed, host test harness added. *Not yet tested on the gate.*
- `rework-tasks` (branched from `V2.2-rc1`) — task/queue restructuring. All 15 bugs
  now fixed. *Not yet tested on the gate.*

Test `V2.2-rc1` first and on its own: it is small and surgical, and some of its fixes
do change behaviour (B6 makes position tracking work for the first time, B5 re-enables
a dead code path). Flashing both sets at once makes any regression hard to attribute.

---

## Phase 1 — Cleanup & rework (branch `cleanup-rework`)

No functional changes intended beyond fixing the defects listed under 1.1.

### 1.1 Bugs found during analysis

| # | Status | Where | Problem |
|---|--------|-------|---------|
| B1 | **fixed** | `components/gpio/gpio_evaluateSwitch.cpp` | `msPressed` was **stale on the rising edge** and not reset per press. Combined with `state` lagging the release by `minOffMs`, a *short* press was evaluated using `msPressed` from the *previous* press → false long-press, self-perpetuating. **Root cause of the reported long-press bug.** Covered by host tests. |
| B2 | **fixed** | `control.cpp` (whole control task) | `Gate::handle()` / `startMovement()` / `stop()` perform **blocking Modbus transactions** (10–150 ms each, up to ~600 ms with retries) inside the control loop, so button sampling stops for that entire time. Input sampling moved to a dedicated 5 ms task, and gate handling to its own task, so the control loop no longer blocks on modbus at all. |
| B3 | **fixed** | `gate.cpp` `Gate::pause()` | Direction was read *after* `stop()` had already overwritten `state` → `wasOpeningBeforePause` always `false`, `resume()` always resumed *closing*. |
| B4 | **fixed** | `gate.cpp` `Gate::resume()` | Unsigned underflow in the remaining-run-time math; `if (targetRunTimeMs <= 0)` unreachable for an unsigned type. Remaining time is now computed once in `pause()`. |
| B5 | **fixed** | `gate.cpp` `startMovement()` | Microsecond difference compared against a millisecond constant (missing `* 1000`). |
| B6 | **fixed** | `gate.cpp` `startMovement()` | `lastPositionUpdateTimestampUs` seeded from the *previous* movement → position estimate slammed to 0 % / 100 % on the first update. |
| B7 | **fixed** | `gate.cpp` ctor | Six members never initialised, read during the first cycles after boot. |
| B8 | **fixed** | `modbus.c` | `memcmp` against an uninitialised `response[8]` without checking the received length. |
| B9 | **fixed** | `control.cpp` | `BARRIER_IS_IGNORED` debug switch had a missing semicolon — broke the build when enabled. Both settings verified to compile. |
| B10 | **fixed** | `buzzer.cpp` | Struct built with assignments instead of designated initialisers. |
| B11 | **fixed** | `gate.cpp` | `runDurationMs + 5000` (15000 ms for gate 1) equals `kMovingTimeout` (15000 ms) → a missed limit switch hits the timeout branch at the exact same instant and ends in `ERROR_STATE`. The target run time is now clamped to stay 2000 ms below the unchanged 15000 ms safety backstop. |
| B12 | **fixed** | `modbus.c` | No mutex around the shared RS485 bus. The gate task is now the only owner of the bus by construction, so no mutex is needed. |
| B13 | **fixed** | `gate.cpp` | `ERROR_STATE` immediately self-transitions to `IDLE_PARTIALLY_OPEN`; `control.cpp` could no longer catch it from another task, so `Gate` now latches it in an atomic flag that the control task clears at the next start command. |
| B14 | **fixed** | `gate.cpp` | Side-effecting `checkLimitSwitch*Active()` calls inside an `ESP_LOGV()` argument list — behaviour depended on the compiled log level. |
| B15 | **fixed** | `gate.cpp` | `checkCurrentLimitExceeded()` read an uninitialised `float` and ignored the Modbus error, so a failed current read could abort a closing movement with `ERROR_STATE`. |

### 1.2 Structure / architecture

- [x] **Decouple input sampling from the control loop.** Sample and debounce all buttons /
      remote / light barrier at a fixed, jitter-free rate (dedicated high-priority task or
      an `esp_timer` periodic callback) and hand *events* to the control task through a
      queue. Fixes the whole class of problems behind B1/B2.
- [x] **Move gate handling (and with it all Modbus I/O) off the control task.** Either a dedicated VFD task fed by a
      command queue, or make `Gate` non-blocking with an explicit "waiting for Modbus"
      state. The control state machine must never block.
- [x] **Single time source.** Replace the mix of `esp_log_timestamp()` (10 ms granularity
      at `CONFIG_FREERTOS_HZ=100`, and a *logging* API) and raw `esp_timer_get_time()`
      with one small helper (`millis()` on top of `esp_timer_get_time()`), consistently
      in ms, with wrap-safe comparisons.
- [~] **Split `control.cpp`.** Input handling is out (`input.cpp`) and gate sequencing is
      out (`gate_task.cpp`). Still mixed in: light-barrier timing, fault-LED blinking and
      buzzer patterns → pull out an `indicator` module (LED + buzzer patterns).
- [x] **Own the objects properly.** `vfd1/2`, `gate1West/gate2East` and `controlConfig` are
      locals in `app_main()` kept alive only by `while(1) vTaskDelay(portMAX_DELAY)`.
      Make them file-scope or heap-allocated and drop the keep-alive hack.
- [ ] **Move the buzzer task into `buzzer_t`** (`createTask()`), as the existing TODO says.
- [x] **Replaced `components/gpio`** into a proper reusable debounce component with unit-testable
      logic (see 1.3), or replace it outright.
- [ ] **`config.h` split:** GPIO/pin mapping vs. behaviour tuning (timings, thresholds).
      Timing constants currently live scattered across `control.cpp` and `gate.hpp`.
- [~] **Remove dead code:** `RUN_GATE_TEST` / `RUN_MODBUS_TEST` and `gateHandleTask()`
      removed (the gate test referenced an object that no longer existed and could not have
      compiled). `RUN_GPIO_TEST` kept — still useful for checking cabinet wiring.
      Still open: `Kconfig.projbuild`, a leftover from the ESP-IDF RS485 echo example.
- [ ] **Consistent language & style:** decide on one comment language, one naming scheme
      (`buzzer_t` vs `Gate` vs `VFD`), consistent `esp_err_t` returns (`modbus.c` returns
      a bare `-2` in one place, `0` in another).

### 1.3 Verification

- [x] Add a host-side test for the debounce/long-press state machine (feed a synthetic
      level+timestamp sequence, assert the events). → `fw_v2.0/test_host/`, run with
      `./run_tests.sh` (needs only g++, no ESP-IDF). 8 tests; two of them fail on the
      pre-fix implementation.
- [ ] Extend the host tests to the control state machine once it is decoupled from the
      blocking VFD calls (1.2).
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
- [x] Note the ESP-IDF version actually in use → moved to **5.5.1**, README install/build
      instructions corrected (`/opt/esp-idf` is empty; esp-idf lives in `~/esp/<version>/esp-idf`).
      The flashing quirk (east gate must be slightly open) was already documented, kept.
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
