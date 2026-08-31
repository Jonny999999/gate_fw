# gate_fw — project context for Claude

Firmware + hardware for a self-made automated sliding gate (2 gates, custom motors,
VFDs via RS485/Modbus, limit switches, buttons, remote, light barrier, ESP32 on a
custom PCB).

- Active firmware: `fw_v2.0/` (ESP-IDF **5.5.2**, see below). `fw_v1.0-legacy/` is
  reference only — do not change it.
- Plan and open work items: **[ROADMAP.md](ROADMAP.md)** — read it before starting work
  and keep it up to date as items are completed.
- Baseline tag `V2.1` = the state that ran reliably in production from 2025-04.

## Build

The project targets **ESP-IDF 5.5.2**, installed at `/home/jonny/esp/v5.5.2/esp-idf`.
Use that one — building with 5.5.1 leaves a build directory the 5.5.2 toolchain rejects
(`Tool doesn't match supported version`), which then needs an `idf.py fullclean`.
(Note: `/opt/esp-idf` is empty; the 5.3 install is unusable because its python venv
`idf5.3_py3.13_env` points at a python3.13 that no longer exists after the system upgrade
to python 3.14.)

```bash
. /home/jonny/esp/v5.5.2/esp-idf/export.sh
idf.py build
```

`sdkconfig` **is** tracked in git. Building with a different IDF version rewrites it with
that version's option set — if that happens unintentionally, revert it
(`git checkout -- fw_v2.0/sdkconfig`) rather than committing the churn.

## Code style

These apply to all firmware code in this repo.

1. **Names must still be understandable in 10 years.** Prefer a slightly longer, explicit
   name over an abbreviation. `timestampLastBarrierChange`, not `tsLastBarrChg`.
   Widely understood short forms are fine to keep short: `gpio`, `pwm`, `vfd`, `ms`, `us`,
   `crc`, `uart`, `idx`, `i`/`j` as loop counters.
2. **Units belong in the name** whenever a value carries one: `targetRunTimeMs`,
   `timestampStartUs`, `kCurrentLimitAmpere`. Mixed-up units have already caused real bugs
   here (see ROADMAP B5).
3. **Comment the *why*, not the *what*.** Especially for the many empirically tuned values
   (VFD startup delay, debounce times, current thresholds) — record what was tried and what
   failed, as the existing code does for `DELAY_VFD_STARTUP`.
4. **Use separation blocks** to structure longer files, in the style already used here:
   ```c
   //===============================
   //========== Functions ==========
   //===============================
   ```
   and lighter `//--- section ---` markers inside functions and state-machine cases.
5. **Number the steps** in any multi-step sequence (protocol frames, startup order, state
   transitions) so the order and its reason are obvious — `modbus.c` already does this for
   response validation; keep that up.
6. **State machines:** one `case` per state, each state's entry/exit conditions commented,
   and a `*_str[]` lookup table kept in sync with the enum for logging.
7. Keep comments in English and consistent in tone with the surrounding file.
8. **Any periodic task delay must be at least one FreeRTOS tick.** With
   `CONFIG_FREERTOS_HZ=100` that is 10 ms, and `pdMS_TO_TICKS()` silently rounds anything
   shorter down to **zero** — which makes `vTaskDelayUntil()` assert and the board boot-loop.
   Guard every such constant with a `static_assert(pdMS_TO_TICKS(X) > 0, ...)`, as
   `input.cpp` and `indicator.cpp` do.
