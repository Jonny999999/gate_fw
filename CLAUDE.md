# gate_fw — project context for Claude

Firmware + hardware for a self-made automated sliding gate (2 gates, custom motors,
VFDs via RS485/Modbus, limit switches, buttons, remote, light barrier, ESP32 on a
custom PCB).

- Active firmware: `fw_v2.0/` (ESP-IDF **5.3**, see note below). `fw_v1.0-legacy/` is
  reference only — do not change it.
- Plan and open work items: **[ROADMAP.md](ROADMAP.md)** — read it before starting work
  and keep it up to date as items are completed.
- Baseline tag `V2.1` = the state that ran reliably in production from 2025-04.

## Build

The project targets ESP-IDF 5.3. The local 5.3 toolchain currently has a broken Python
venv (`idf5.3_py3.13_env` points at a python3.13 that no longer exists after a system
upgrade to 3.14). ESP-IDF 5.5.1 at `/home/jonny/esp/v5.5.1/esp-idf` works and the project
compiles with it.

Build **out of tree** so the checked-in `sdkconfig` is not rewritten by a newer IDF:

```bash
. /home/jonny/esp/v5.5.1/esp-idf/export.sh
idf.py -B /tmp/gate_fw-build build
git checkout -- fw_v2.0/sdkconfig   # 5.5 rewrites it; revert unless intentional
```

`sdkconfig` **is** tracked in git — never commit an IDF-version-induced rewrite of it.

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
