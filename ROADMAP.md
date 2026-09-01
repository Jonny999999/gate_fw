# Roadmap / Course of Action

Working document for the ongoing rework of `fw_v2.0`.
Purpose: keep the plan and the reasoning in the repo so work can be picked up at any
time without re-explaining the context.

- **Baseline:** tag `V2.1` = state that ran reliably in production from 2025-04 until now.
- **Current branch:** `cleanup-rework` — refactor, bugfixes and documentation **only**,
  no new features. New features get their own branch afterwards.

Status legend: `[ ]` open · `[~]` in progress · `[x]` done

**Progress — three stacked branches:**

| Branch | Tag | Contents |
|---|---|---|
| `cleanup-rework` | **`V2.2-rc1`** | ESP-IDF 5.5.1, bugs B1, B3–B10, B14, B15, host tests |
| `rework-tasks` | **`V2.2-rc2`** | task/queue restructuring, indicator module — all 15 bugs fixed |
| `ui-rework` | — | Phase 2: automatic closing, gesture rework, hardening (targets V2.3) |
| `feature/variable-gate-speed` | — | experiment: two speed levels + travel-time measurement (2.6) |

`ui-rework` has been **tested on the gate** (2026-09-01): the UI features work. Three bugs
only showed up on hardware and are fixed — a task period below one FreeRTOS tick
(boot loop), a beep pattern that never turned the buzzer off, and a publish/pending race
that made the control task leave `MOVING_TO_TARGET` early and silently stop checking the
light barrier while closing.

`V2.2-rc1` and `V2.2-rc2` have **not** been flashed on their own; the branch above contains
everything.

### Hardening pass (2026-09-01) — done
A full read-through after the first successful test, looking for the kind of bug that only
appears occasionally:

- [x] **A failed VFD start left the relay on forever.** No retry at the Gate level, and the
      early return skipped `softStopRelay()`, so `relayTimeoutActive` stayed false and the
      inactivity timeout could never fire. The start is now retried (`kStartAttempts`), and
      only a genuine failure cuts the supply — necessary, because after a lost reply the
      drive may be running with nothing tracking it.
- [x] **Relay inactivity timeout overflowed:** `uint32 × 1000` wrapped, so 3 h really
      expired after ~37 min while the log said 10800 s. Now 64 bit, and raised to 4 h.
- [x] **`updateTargetRunTime()` was unclamped:** ~20 repeated presses produced a target
      longer than the movement timeout, so the gate faulted instead of opening wide.
- [x] **A refused movement was indistinguishable from an executed one** (close while the
      closed limit switch is active) — start signal, then nothing. Now acknowledged with a
      short beep. Also what a stuck limit switch looks like.
- [x] **`setFrequency()`'s result was ignored.** Now checked, but deliberately not fatal:
      the drive keeps the same value that is written on every start.
- [x] **The task watchdog watched nothing of ours.** Control, input and indicator now check
      in. The gate task stays unwatched on purpose — it blocks on modbus by design.
- [x] **Gate commands were dropped when the queue was full.** They now wait 200 ms first;
      losing a STOP is the worst possible outcome.
- [x] **Barrier re-checked in the gate task** immediately before a closing command is
      applied, closing the one-cycle window after the control task's own check.
- [x] **No spurious startup error/beep:** the VFD constructor no longer talks to an
      unpowered drive, and `Gate` starts in the state its limit switches actually report.

Test them **in that order, separately**, if the branches are ever flashed individually.
Some fixes do change behaviour (B6 makes position tracking work for the first time, B5
re-enables a dead code path).

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
- [x] **Split `control.cpp`.** Input handling → `input.cpp`, gate sequencing →
      `gate_task.cpp`, buzzer + status LED → `indicator.cpp`. `control.cpp` is now only the
      user-facing state machine plus the light-barrier timing.
- [x] **Own the objects properly.** `vfd1/2`, `gate1West/gate2East` and `controlConfig` are
      locals in `app_main()` kept alive only by `while(1) vTaskDelay(portMAX_DELAY)`.
      Make them file-scope or heap-allocated and drop the keep-alive hack.
- [x] **Buzzer task** — `buzzer_t` is gone entirely; the indicator task owns the buzzer and
      the status LED, addressed by free functions so nothing has to carry a pointer.
- [x] **Replaced `components/gpio`** into a proper reusable debounce component with unit-testable
      logic (see 1.3), or replace it outright.
- [~] **`config.h` split:** GPIO/pin mapping vs. behaviour tuning (timings, thresholds).
      Half done on `feature/variable-gate-speed`: everything that used to sit at the top of
      `gate.hpp` (speeds, VFD startup delay, relay timeout, current limits, debug switches)
      now lives in `main/config_behaviour.h`, and `config.h` stays the pin map it says it
      is. Still open: the control-task timings at the top of `control.cpp` — button windows,
      light barrier, automatic closing.
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

### 2.0 Indication (done as part of Phase 1)
- [x] Buzzer signals are named (`BuzzerSignal::…`) with the timings unchanged.
- [x] LED is priority-arbitrated: latched fault > status > buzzer mirror. Previously a
      latched fault was overwritten by the barrier passthrough as soon as control returned
      to IDLE, so it was effectively never visible.
- [x] Fault codes blink at a severity-dependent rate (100 / 200 / 500 / 1000 ms).
- [x] **Fault and "pending" are told apart by shape, not speed.** Even blinking means
      something went wrong; a short flash with a long gap means a movement is queued and any
      button cancels it. `WAITING_FOR_BARRIER` used to blink evenly and was indistinguishable
      from a fault code at a glance.
- [x] Buzzer mirroring on the red LED turned **off** (`LED_MIRRORS_BUZZER` in
      `indicator.cpp`): the green panel LED is wired in parallel with the buzzer, so the
      beeps are already echoed visually in hardware. Flip the define back to 1 if the red
      LED should join in as well.
- [ ] **Possible hardware change — individually driven green LED.** The ULN2003 (U1) has a
      seventh channel left, but it is unused by design: input pin 7 is tied to GND and
      output pin 10 (O7) reaches no screw terminal. Wiring it up (free GPIO → isolator →
      U1 pin 7, U1 pin 10 → terminal) would free the green LED from the buzzer and give it
      its own job — "moving" / "movement pending" / "ready" — while red stays purely fault.
      Adding it in firmware is then a second `IndicatorChannel` plus a status mapping.

### 2.1 Reliable button handling
- [x] Long-press is evaluated on a clean, per-press measurement and reported once, while
      the button is still held (`DebouncedButton`, delivered in Phase 1).
- [x] Threshold raised 600 → **800 ms** (`OPEN_BUTTON_LONG_PRESS_MS` in `input.cpp`).
      Safe now: the measurement is exact, and `control.cpp` suspends its input timeout
      while the button is held, so the timeout no longer races the long press.

### 2.2 Simplify the "open further" sequence
The current scheme (`IDLE → WAIT_FOR_INPUT`, repeated presses within
`min(BUTTON_PRESS_INITIAL_OPEN_TIME_MS, BUTTON_PRESS_AGAIN_OPEN_INCREMENT_MS)` add
`700 ms` each) is effectively unused: the window is hard to hit and it competes with
long-press detection.
- [~] **Kept for now**, because the auto-close gesture builds on the same counter and the
      window is no longer hostile: the timeout is suspended while the button is held, so
      the repeated presses and the long press no longer compete.
- [ ] **Still to decide at the gate:** whether anyone actually uses the repeated-press
      widening. If not, drop it — that would also let `WAIT_FOR_INPUT` collapse into a
      simple "short = gap, long = full, short+long = gap with auto-close".

### 2.3 New gesture: "let me through, then close behind me" — implemented
- [x] Gesture: **short press, then press and hold**. Told apart from "hold from the start"
      (= open completely) by `countPressed`, so the familiar full-open gesture is unchanged
      and the gate still starts opening on the very first press.
- [x] Opens the **partial** gap — the same ~1.9 s opening a single short press gives, sized
      for a person to walk through. The press that turns into the long press only selects
      the mode, so its widening increment is undone.
- [x] **Full opening variant** (see 2.5): keep the *first* press held past a second
      threshold (`OPEN_BUTTON_VERY_LONG_PRESS_MS`, 2.5 s). Nothing is committed until the
      button is released, so each stage can announce itself and the user releases when they
      hear what they want: quick = small gap, ~0.8 s = open completely, ~2.5 s = open
      completely and close again.
- [x] Required clear time depends on the opening size: 10 s for the partial gap, **120 s**
      for a full opening. Opening the gate, walking to the car and driving out can leave the
      barrier untouched for a minute or more; a short clear time would close it in front of
      the car.
- [x] Closes once the light barrier has been **continuously free** for the required time —
      not simply a fixed time after opening. The condition
      asks the question that actually matters ("is everybody through and nothing coming?"),
      so walking in and out or taking a while with a trailer postpones the close instead of
      racing a deadline.
- [x] Give-up: if the barrier stays obstructed for `AUTO_CLOSE_GIVE_UP_OBSTRUCTED_MS` (20 s)
      without a break, the pending close is cancelled and the gate stays open. Whoever is
      there is clearly busy; closing on them later would be worse than not closing.
- [x] Distinct confirmation: `BuzzerSignal::AUTO_CLOSE_ARMED` is a two-part signal — one
      long tone, then two short ones. Nothing else in the catalogue has that shape, so it
      cannot be confused with a normal opening by ear. Cancelling plays the mirror image.
- [x] The same accelerating countdown as the barrier resume announces the close
      (`handleCountdownBeeps()`, now shared by both paths).
- [x] The wait restarts whenever the light barrier is interrupted, so the gate never starts
      closing with somebody in the gap — including during the countdown, where the beeping
      stopping makes the restart audible.
- [x] Cancellable: **open** cancels and leaves the gate open, **close** closes immediately
      instead of waiting out the rest. A gate error also cancels it — after a failed
      movement the gate should not start moving again unattended.
- [x] `WAIT_AUTO_CLOSE` control state, with its own LED indication (short flash, long gap —
      deliberately asymmetric so it cannot be read as one of the evenly blinking faults).
- [x] All closing goes through one `startClosingGates()` helper, so a manual close, a close
      requested from the auto-close state and the timed close treat the barrier identically.
- [x] **Barrier interruption during the automatic close: retries.** The close is not a
      special case — it takes the same path as a manual one: pause → 4 s countdown → resume
      on every clear, give up after `BARRIER_WAIT_FOR_FREE_TIMEOUT_MS` (8 s) of *continuous*
      obstruction. Both arguments for cancelling instead are already covered: staying in the
      gateway past 8 s cancels it outright, and any button press cancels immediately.
- [ ] **To decide at the gate:** are 20 s of required clear time, 20 s before giving up and
      a 4 s countdown the right numbers? All three are single constants at the top of
      `control.cpp`. 20 s of *clear* time may well feel long now that it only starts once
      the way is actually free — a lower value is the first thing to try.
- [ ] **Watch for:** the 8 s give-up raises a `BARRIER_BLOCKED_TOO_LONG` fault, so the LED
      blinks slowly afterwards. For an automatic close that may read as "something broke"
      rather than "somebody stayed in the gateway" — see whether it is useful or annoying.
- [ ] No host tests for this yet — the control state machine still depends on the gate,
      input and indicator modules directly. Worth abstracting once the behaviour has
      settled at the gate.

### 2.4 Documentation pass — after the UI is tested and settled
Deliberately postponed: documenting an untested design in detail means rewriting it after
the first session at the gate. The README already has a gesture table and a fault-LED blink
table; what is still missing is the complete picture.

- [ ] **Full operating manual in the README** — one table of *every* button/remote event
      against *every* control state, so the state-dependent behaviour is explicit
      (e.g. "while moving, any button stops"; "while waiting to close automatically, open
      cancels but close closes immediately"). Source of truth is the switch in
      `control.cpp`; check the table against it whenever the states change.
- [ ] **Complete fault / indication reference** — the blink-code table plus, for each code,
      what actually triggers it and what to check on the hardware.
- [ ] **Firmware architecture section** — the four tasks (input 8, control 5, gate 5,
      indicator 3), which queues connect them, who owns the RS485 bus, and why gate handling
      is not in the control loop. A diagram would carry this better than prose; a drawio
      next to the existing plans in `doc/` fits the repo.
- [ ] **Tuning-constant reference** — one table of the behaviour constants with their file,
      current value and what changing them does. Best done together with the `config.h`
      split (1.2), so the table has a single file to point at.
- [ ] Keep it one commit, separate from behaviour changes, so the docs can be redone
      cheaply if the UI shifts again.

### 2.5 Automatic closing for a *full* opening — implemented
Chosen: **option 2, a second hold threshold** on the same press. The escalation is
self-announcing, so it needs no extra button and nothing is committed until release:

| Hold the first press | Result | Announced by |
|---|---|---|
| release quickly | small gap | one short beep |
| ≥ `OPEN_BUTTON_LONG_PRESS_MS` (0.8 s) | open completely, stay open | one long tone |
| ≥ `OPEN_BUTTON_VERY_LONG_PRESS_MS` (2.5 s) | open completely **and close again** | long tone, then two short |

- [x] `DebouncedButton` gained a second threshold; both fire for the same press, in order.
      Covered by three host tests (escalation order, release in between, re-arming).
- [x] `control.cpp` stays in `WAIT_FOR_INPUT` while the button is still held after the first
      threshold, and commits on release. The input timeout is already suspended while held.
- [x] Accidental triggering is implausible: 2.5 s is far beyond any absent-minded hold, the
      first meaning is confirmed audibly at 0.8 s, and the two-part signal makes the
      escalation obvious. Any button press cancels afterwards.

Rejected alternatives, kept for the record:
- **Function button as a modifier** (`CONFIG_FN_BUTTON_GPIO`, GPIO 34, still unused). Would
  also work and is impossible to trigger accidentally, but costs a second button to reach
  for. Still the natural choice if the button ever gets another job that needs a modifier.
- **Long press, then long press.** The second press stops the movement, and stop has to act
  on the press event rather than the release, so it cannot wait to see whether the press
  turns long. Not worth trading stop responsiveness for.
- **Short, short, long.** Already means "wider gap + auto-close".

- [ ] **Still worth questioning at the gate:** should a full opening auto-close at all? A car
      is out in seconds, and the light barrier only covers the gateway, not the driveway — so
      the area worth protecting is larger than what the sensor sees. The 120 s clear time is
      the mitigation; see whether it feels right.

### 2.6 Variable gate speed — proof of concept on branch `feature/variable-gate-speed`
Right now every movement runs at a single frequency (`DEFAULT_VFD_FREQUENCY`, 50 Hz) from
standstill to limit switch. A speed curve — slow at the start, faster in the middle, slow
again before the end — would be gentler on the mechanics and noticeably kinder to the limit
stops, which currently take the full speed every time.

`VFD::setFrequency()` already exists and is called on every start, so the drive side is
there; what is missing is knowing *where* the gate is.

**Reassessed 2026-09-01, and built as an experiment.** The prerequisite below turned out
to be overstated: it assumed the speed profile needs to *know the position*, when what it
actually needs is to know when the movement is roughly one second from its end. Those are
very different accuracy requirements, and the second one a hand-measured travel time
already meets.

Two things make the estimate good enough:

- **Slowing down early is free.** It costs a moment, nothing else. There is no equivalent
  of "stopped in the wrong place" — the limit switch still ends the movement.
- **The error is one-sided by construction.** The final-approach test holds not only in
  the last stretch but also *past* where the end was expected, so a late or missed limit
  switch leaves the gate creeping instead of accelerating back into the stop. The estimate
  being wrong makes the gate gentler, never more violent.

The re-derivation problem also mostly dissolved once the underlying quantity was named
correctly: every movement constant in this firmware (`1900 ms` of gap, `10000 ms` of rail)
describes a **distance** and was only ever written as a time because the speed never
changed. Integrating speed × time keeps all of them valid, so the partial-open gap stays
exactly as wide as before. `kMovementTimeoutMs` is the one genuinely wall-clock constant
and is the only one that had to move.

- [x] **Distance instead of wall-clock time** (`travelledDistanceUs`, `updateTravel()`).
      The target checks, `pause()`/`resume()` and the position estimate all run on it. This
      is also the exact quantity encoders would supply (3.2): the integration is the only
      thing that has to be swapped for a pulse count.
- [x] **Two discrete speed levels** rather than a continuous curve — one `setFrequency()`
      per phase change, `kSlowStartDistanceMs` / `kSlowApproachDistanceMs`, behind
      `VARIABLE_SPEED_ENABLED`.
- [x] **The distance unit is pinned to a reference speed** (`VFD_FREQUENCY_REFERENCE_HZ`,
      40 Hz — what actually ran from V2.0 to V2.2), not to whatever the full speed happens
      to be. Without that, raising the speed re-invalidates the rail length, the pedestrian
      gap and the profile distances every time — the trap this section warned about.
      Changing `VFD_FREQUENCY_FULL_HZ` now only changes how fast those distances are
      covered.
- [x] **Full speed raised 40 → 60 Hz** so the contrast with the slow phases is obvious.
      Safe only because of how the drives are parameterised, which is now recorded next to
      the constant: `-1.7-` max frequency is 70 Hz (above that the drive clamps silently,
      which would also break the distance bookkeeping), and `-2.0-` is 99.9 / 95 Hz, so
      60 Hz is still constant V/f — no field weakening, no torque loss.
- [x] **`kMovementTimeoutMs` stays at its historical 15 s.** A full run at 60/25 Hz takes
      ~9.4 s against the ~11 s it used to, so it fits with more margin than before. The
      arithmetic for the speeds worth trying is written out next to the constant.
- [x] **Measure the real travel time on every limit-to-limit run** and log it with the
      deviation from `runDurationMs` and a running mean. Only logged, never fed back —
      learning it into NVS is a separate decision, best made with these numbers in hand,
      and largely pointless if encoders arrive.
- [x] **Speed-dependent current limit** (`kCurrentLimitSlowAmpere`), currently the same
      value as at full speed because the slow one has not been measured yet.
      `LOG_VFD_CURRENT_WHEN_CLOSING` is enabled on the branch to collect it, and no longer
      costs a second modbus read per cycle.
- [x] Fixed along the way: `DEFAULT_VFD_FREQUENCY` said **50 Hz and was never read**. The
      speed actually sent to the drives came from a different constant saying 40, which is
      what the run durations in `main.cpp` were measured against.

#### First hardware test (2026-09-01) — it works, and here is what it taught
The profile runs on the gate. Three things only showed up there:

- [x] **The final approach barely happened.** It is timed against the expected end, and the
      expected end came from the hand-measured `runDurationMs`, which was 13 % (west) and
      21 % (east) too long — so the "last 1500 ms" window mostly lay past where the gate
      actually stops. The east gate effectively never slowed. **The rail is now measured**
      on every limit-to-limit run and used from then on (`effectiveFullTravelMs()`); the
      configured values are only a starting point until the first full movement.
      Real travel: west 8826, east 9123 (against 10000 / 11000 configured).
- [x] **Every partial movement crawled.** The approach was timed against the target as well
      as the limit switch, and the pedestrian gap is shorter than the two slow phases put
      together, so it ran slowly end to end and got no benefit from the higher speed. Only
      the limit switch matters now: a target stop needs no approach, the motor just stops.
- [x] **The backstop no longer held.** A fixed 15 s could not cover a full movement run
      entirely at the slow speed (14.6 s — reachable whenever the position estimate says
      "nearly there"), and the target distance itself needed 20.8 s at that speed, so a
      missed limit switch hit the error path instead of the clean stop B11 guarantees. The
      backstop is derived from the target distance and the slowest speed now, and the
      overtravel allowance drops 5000 → 2000 travel-ms because at the slow speed the old
      value was 8 s of pushing against the end stop.
- [x] **An uncalibrated position plus a high speed is worse than no feature at all.** After
      a boot with the gate parked mid-rail the estimate can put the limit switch further
      away than it is, so the gate arrives in the fast phase — at 75 Hz that is 1.9× the
      speed it ran at for years, 3.5× the energy, into the stop this feature exists to
      protect. Until a limit switch confirms the position the gate is capped at the
      reference speed.

#### Calibration hardening (2026-09-01) — done
Feeding measurements back into the gate's own behaviour is only safe with rules about what
may become one. Four, each closing a way the first version could learn something wrong:

- [x] **Only clean full runs count.** Started on one limit switch, ended on the other,
      nothing in between — no barrier pause, stop, cancel, timeout, obstruction or failed
      VFD start. This subsumes "ignore the first run after a restart": a boot with the gate
      parked mid-rail cannot produce a measurement at all, and a boot *on* a limit switch
      legitimately can. The case that made it necessary is subtler — a pause while the gate
      still sits on the switch produces a run that starts on a switch and reaches the other,
      but only times the part after the resume.
- [x] **Only values within ±25 % of the configured constant count**, rejected with the band
      printed if not. Anchored to the constant rather than to the current estimate on
      purpose: a sequence of individually plausible steps can then never walk the value away
      from the hand-measured reality, however many runs go by.
- [x] **Weighted in at 1/8, not adopted.** Converges in about ten runs; one odd run moves
      the gate by at most ~12 %.
- [x] **Kept in NVS**, so a restart does not throw it away. Written only when the estimate
      has moved more than 2 % from what is stored, or at most every 10th accepted
      measurement, and never when unchanged. A stored value has to pass the same ±25 % band
      before it is used — which makes the reset free: correct the constant in `main.cpp` and
      a stale stored value is discarded on the next boot.
- [x] Every outcome logs which rule it hit and what the estimate did, so the log answers
      "why is the gate behaving like that" without a rebuild.
- [x] **Full speed capped at 70 Hz**, the drives' `-1.7-` ceiling. It had been set to 75, so
      the drives ran at 70 while the firmware counted distance for 75 — a ~7 % error that the
      measured travel absorbed into a consistently larger number, which is the worst kind:
      self-consistent and invisible.

**Open, and worth settling before a long-term test:**

- [ ] **Both current limits are still the threshold measured at 40 Hz**, and the range is
      now 25–70 Hz. This is the most likely source of a nuisance fault in daily use.
- [ ] The NVS keys are `travel_` + the gate name truncated to the 15 characters NVS allows.
      `Gate1_West` / `Gate2_East` stay distinct; renaming the gates to share a longer prefix
      would silently make them share one stored value.

#### What the hardware test has to answer
The open questions are all physical, and none of them are answered by having encoders —
they are the reason to test now rather than later:

- [ ] **Does the drive change speed cleanly mid-movement?** `setFrequency()` has only ever
      been called from standstill. If the drive jolts, refuses, or trips on the change, the
      whole idea ends here for a few euros of test time.
- [ ] **Is 25 Hz enough to move the gate at all** — from standstill, and against wind or a
      stiff spot on the rail? The drive's low-speed torque boost (`-0.3-` / `-0.4-`) only
      reaches up to 20 Hz, so 25 Hz gets plain reduced voltage. Raise
      `VFD_FREQUENCY_SLOW_HZ` if it stalls, or extend the boost range on the drive.
- [ ] **Does 60 Hz feel right, or too fast for a gate?** It is 1.5× the speed the gate has
      run at for years, and the light barrier only covers the gateway. Lower
      `VFD_FREQUENCY_FULL_HZ` without touching anything else if it does.
- [ ] **What does the motor draw at 25 and 60 Hz while closing?** The obstruction
      threshold was tuned at 40 Hz, so both ends of the new range are unverified. Read them off the
      `LOG_VFD_CURRENT_WHEN_CLOSING` lines and set `VFD_CURRENT_LIMIT_*` from real numbers.
- [ ] **How repeatable is the travel time really?** The `FULL TRAVEL measured:` lines give
      the spread over a session. If it is a few percent, the time-based approach is fine
      and encoders buy exactness rather than function. If it wanders, that is the argument
      for 3.2.
- [ ] **Does the gentler pedestrian gap feel better or just slow?** It is short enough to
      run at 25 Hz throughout — so it is the same width as before but takes ~3 s, and gets
      no benefit from the higher full speed. Lower the two profile distances if it should.
- [ ] **Is the drive's own accel/decel ramp visible?** The distance integration counts the
      new speed from the moment the command is sent, while the drive ramps to it over its
      configured time (`-0.1-` start 7, `-0.2-` stop 10). Worth a few hundred ms of travel
      per speed change — check whether full movements now consistently over- or undershoot.
      Register 2 is documented as "speed setting **and speed feedback**", so a read may show
      the real output frequency and settle this directly. Not polled today: while opening
      there is no modbus traffic at all, and adding a read per cycle would slow the response
      to a stop button.
- [x] Nice side effect, and now explicit in the code: a slow final approach makes a missed
      limit switch far less violent, which is the failure mode `kMovementTimeoutMs`
      currently backstops.

Merge only after all of the above. The branch is deliberately a single subject, so it
either becomes part of V2.3 or is dropped whole.

### 2.7 Switch the light barrier supply off when it is not needed
`CONFIG_LIGHTBARRIER_EN_GPIO` (GPIO 14) drives a P-MOSFET for the barrier supply and is
currently **never driven by the firmware** — the barrier is simply always powered.

- [ ] Only power the barrier while it is actually being used (closing, and while an
      automatic close is pending), to save the ~6 mA it draws when interrupted.
- [ ] **Exception that must not be forgotten:** the barrier state is also used for
      indication and debugging — `SHOW_BARRIER_STATE_ON_LED_IN_IDLE` and
      `DEBUG_BARRIER_BEEP_ON_CHANGE` in `control.cpp`. If either is enabled the supply has
      to stay on permanently, otherwise those turn into "always free" and become quietly
      misleading. The same applies to any future plausibility check on the sensor.
- [ ] Note the sensor needs time to settle after power-up; the barrier would have to be
      switched on *before* a closing movement starts, not with it.
- [ ] Low priority — with PV excess the standby current is not worth much.
- [ ] **Related, and the more interesting half:** the barrier is fail-unsafe by
      construction. The NPN sensor pulls low when interrupted, so a dead sensor, a cut wire
      or an unpowered barrier all read as *free* — the firmware cannot tell "nothing in the
      way" from "no sensor at all". A switchable supply would actually make a self-test
      possible: power it down, confirm the input follows, power it back up. Worth keeping in
      mind if 2.7 is ever implemented, because it turns a power-saving feature into a safety
      one. No change for now.

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
