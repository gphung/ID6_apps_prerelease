# F87 M2 N55 work: handoff

Status and context for continuing the F87 M2 work in this fork (branch `m2-n55`) on another computer
or with another assistant session. Read this first, then [AgentReadMe.md](AgentReadMe.md),
[source/AGENTS.md](source/AGENTS.md) and [source/docs/SETUP-NOTES.md](source/docs/SETUP-NOTES.md).
Last updated 2026-11-22.

## The car

- 2018 BMW M2 (F87), N55 engine, manual, bootmod3 flashed. DME MEVD17.2.G, SGBD `mevd172y`.
  RWD (no xDrive), no HUD, no MHD.
- Head unit NBTevo_Y21432J (ID5_2111 build 28), 1280x480, split node (SSH lands on hu-jacinto; the HMI
  runs on hu-omap, reached as `/net/hu-omap`). Frameworks App `75aff211…` / Core `235d275f…` are in
  upstream's COMPATIBILITY table.
- The diagnostic gateway's link-local address is specific to the car. It is in each app's
  `gateway-ip.txt` on the car and in the owner's private notes, and is deliberately not in this repo.
  The head unit's SSH address is `169.254.199.99`.

## What is installed on the car (2026-11-20)

| App | Version | Notes |
| --- | --- | --- |
| BoostDeck | v11b (`465e5d5`) | N55 configurable tiles, ENGINE warm-up panel. Owner layout: Oil temp, Oil pressure, Coolant, Knock / Boost, Timing, Intake air, Battery. About 78 ms per scan |
| Engine Fault Codes | v10 (`0106d35`) | N55 description table, selectable codes with detail pages |
| ShiftDeck | v9 | Paired RPM + speed, adaptive easing |
| Bootmod3 Suite | v9 | Opened only; no button has been pressed |

Settings files on the car: `BoostDeck/units.txt` (`c psi`) and `BoostDeck/tiles.txt`. Both survive iDrive
restarts.

Backups on the head unit, each verified by reading it back:
- `/fs/sda1/id6-backup/v8-2026-11-18`
- `/fs/sda1/id6-backup/v10-2026-11-20` (BoostDeck v9 + Engine Fault Codes v9 files)
- `/fs/sda1/id6-backup/v11-2026-11-20` (BoostDeck v11 lib + collector)

Undo, run from the computer through the SSH control socket, then restart iDrive. Paths are on hu-jacinto:

```sh
P=/net/hu-omap/fs/sda1/opt/hmi/data/ID5/Plugins
# v11b -> v11
B=/net/hu-omap/fs/sda1/id6-backup/v11-2026-11-20; ssh … "cp $B/libBoostDeck.so $B/boostdeck-collector-qnx $P/BoostDeck/"
# v11 + v10 -> v9
B=/net/hu-omap/fs/sda1/id6-backup/v10-2026-11-20; ssh … "cp $B/BoostDeck/* $P/BoostDeck/ && cp $B/EngineCodes/* $P/EngineCodes/ && rm -f $P/BoostDeck/tiles.txt"
# everything off
ssh … "mv $P /net/hu-omap/fs/sda1/opt/hmi/data/ID5/Plugins.off"
```

## Owner's rules and preferences

- **The owner types every password.** Never store, echo or commit one. SSH: the owner enables it with
  Bavarian SSH Client, then runs `ssh -fN 169.254.199.99` in a real terminal. Later commands reuse the
  control socket with `-o BatchMode=yes`. Restarting iDrive turns SSH off.
- **DME writes need explicit approval for each action**: bm3 buttons, map changes, adaptation resets,
  battery registration, and the IBS standby-current test. Never send routine `31 01 F031`.
  Adaptation reset is `31 01 F030 73 89 03` (`ADAP_SELEKTIV_LOESCHEN`), but bm3's exact selection is
  unconfirmed. Battery registration uses the same `F030` routine family.
- **Writes to the head unit's system partition** (`/fs/sda0`, for example the iDrive hero image) need a
  separate go-ahead.
- **Installs are staged**:
  1. back up the files being replaced, outside `Plugins`
  2. stage the new files outside `Plugins`
  3. `chmod` workers on hu-omap with `on -f hu-omap /fs/sda0/bin/chmod`
  4. read every staged file back and compare hashes
  5. run the worker standalone, read-only
  6. move the files into place and read them back again
  7. the owner restarts iDrive
  8. add the undo command to the rollback notes

  Never time anything with `--trace`, since Qnet output is slow. Use `ssh -n` in loops.
- **Live checks start read-only**: UDS `22` and OBD `01` only (`source/tools/probe_signals.py`).
- **Speed matters most for RPM.** The owner always prefers the fastest possible RPM in ShiftDeck, and
  fast refresh in BoostDeck.
- **Keep car-specific data out of this public repo**: VIN, the gateway's link-local address, captures,
  battery history, the owner's backups, BMW framework binaries, and German DTC source text.
  Commit as `Gary Phung <27773875+gphung@users.noreply.github.com>`. Before any push, scan every new
  commit (patches and messages) for VINs, MAC addresses, keys, tokens and addresses.
- **Ask before installing, restarting or writing to an ECU.** Report results with the raw readings.

## Branches

- `m2-n55` on this fork holds the 22 commits of M2 work, based on upstream `5d19aa1`.
- Upstream `inotbarnacle/ID6_apps_prerelease` has moved on 18 commits since then (v0.7.4–v0.7.6). See
  "Catching up with upstream" below.
- The old work computer has local-only branches (`m2-n55`, `m2-n55-a402dd0`, `m2-n55-pre-*`,
  `m2-n55-backup-*`) with an older author address and the car's gateway address. They must never be
  pushed. They are superseded by this branch.

## What the 22 commits add

**BoostDeck, N55 profile** (`BoostDeck/profile.txt` = `n55`):
- `source/native/src/n55_metrics.{c,h}` is the one table of all 41 N55 tile metrics: DID, scaling, read
  rate, bar range and style, and warning rule. It also holds the read planner (each DID read once,
  however many tiles show it), the knock and misfire tracker, and the `NBN1` frame format.
  Tests: `tests/test_n55_metrics.c`, using replies read on the car.
- The collector's `--metrics key[@ms],...` mode (`src/collector.c`) reads two DIDs per `22` request.
  Any two pairable values due in the same scan share a request. A DID the DME refuses is skipped for
  30 s, and pairing stops only if the DME refuses pairs themselves. Ambient pressure and the wastegate
  end stops are read first. Slow values are spread across their period.
  Tests: `tests/test_transport.py` (`test_metrics_*`).
- Plugin (`src/plugin.cpp`):
  - **OPTIONS > Tiles** shows a 4x2 grid of positions. Each position has a list of all metrics plus
    blank, a pick applies at once and swaps with a tile already showing that metric, and the layout is
    saved in `tiles.txt`.
  - Captions are images, bars for Timing, Fuel trim and Battery amps start at a zero mark, and tiles
    turn red on their warnings.
  - The tall tile is the ENGINE oil warm-up panel. The bands are Cold, Warm (180 °F), Ready (194 °F) and
    Hot (266 °F), each held until the reading falls 1 °C below the threshold.
  - Temperature and pressure units are separate, saved settings.
- Tools:
  - `source/native/tools/n55_table.py` reads the C table.
  - `make_n55_layout.py --tiles` generates the XML.
  - `make_n55_tiles_art.py` makes the captions (DIN Alternate Bold on macOS, or `--font`), background
    `56302` and the zero bars.
  - `make_n55_background.py` makes background `56301`.

  `tests/test_n55_tiles.py` checks that the layout, artwork and plugin IDs agree with the C table.
- `source/docs/SIGNALS.md` ("F87 M2 N55 profile", "Configurable tiles") lists every DID and scaling,
  with readings from the car.

**ShiftDeck**: RPM and speed are read in one request (`22 4807 4AB1`), with OBD as fallback. Easing
adapts to the interval between readings (50–200 ms).

**Engine Fault Codes**:
- A table of 1,378 N55 descriptions in English (`dme_descriptions_n55.h`, from `make_dme_descriptions.py`).
- Selectable code rows with a detail page each, and status words; text helpers in `fault_text.h`.

**All workers**: HSFZ gateway address list (up to 4), broadcast discovery cached in `/tmp`, and a return
from MOST to HSFZ every 30 s (`diag_link.c`, `diagnostic_gateway.h`).

**Docs**: `source/docs/SETUP-NOTES.md` covers ENET on macOS, the SSH client, split-node paths, the install
procedure and the QNX VM on Apple silicon.

## N55 facts measured on the car

- **Two DIDs per `22` request** cost the time of one. Three or more are refused (NRC `31`), and so is
  defining a block with `2C 01 F3F0`.
- **Request time:** about 40 ms per request from the head unit and 30 ms from a computer.
- **Engine values:**
  - `4408` oil temperature, signed × 0.1 °C
  - `58EF` rail pressure, × 0.0005 MPa
  - `586F` oil pressure, absolute hPa (subtract ambient `4201`)
  - `4300` coolant, 1 byte × 0.75 − 48 (the profile uses OBD `0105` instead)
  - `4807` RPM × 0.25
  - `4AB1` speed × 1/128 km/h
  - `0152` gives no reply (no ethanol)
- **Gear:**
  - `5940` is the gear sensor: bit 7 means in gear, plus one gear bit (1 = 81, 2 = 82, 3 = 84, 4 = 88,
    5 = 90, 6 = A0, R = C0, N = 0C).
  - `587D` is 1 in neutral.
  - `5881` is the gear calculated from RPM and speed. It reads 0 in neutral; it has not yet been
    compared with `5940` while driving.
- **Battery:**
  - `5853` is the battery sensor (IBS) voltage. `586A` agrees within 10 mV when steady; the DME's own
    pins read 0.1–0.8 V lower.
  - `5852`: positive means discharge.
  - `460E` = charge minus the start-capability limit.
  - `4022` / `4023` hold the IBS records: lifetime counters, charge by band, 5-day charge and
    start-limit history, standby-current history for the last 32 parkings (4-bit codes in words 23–30).
    The days are calendar days.
  - `5916` and `409E` answer NRC `31`.
  - While charging after a start the battery sits at 15.0–15.1 V, which is normal; that is why the red
    limit while running is 15.5 V.

## Open items, roughly in order

1. **Catch up with upstream.** It is 18 commits ahead as of 2026-11-22, touches about 200 files, and
   overlaps our work. See the next section.
2. **v12: Battery details screen and start-up battery log.**
   - Screen: OPTIONS > Battery details, with the battery tiles at the top (Battery, Battery %, Battery
     amps, Start margin, Battery temp). Below them: state of health, registered capacity, deep-discharge
     index, the last 5 days with calendar dates (Today, Yesterday, Nov 17 …), standby-current time per
     band, lifetime counters, and a 32-column standby-current chart.
     - Column colours: gauge colour under 80 mA, orange 80–200 mA, red 200–1000 mA, bright red over 1 A.
     - Height is the length of the parking, from our own log.
     - A mark shows where the car intervened.
   - Start-up log: a separate worker, about 90 s after the HMI starts, read-only.
     - It records time, charge %, start margin, mileage and the newest standby-current entry, so
       parkings get real dates.
     - Safeguards agreed with the owner:
       - separate process, never blocking the HMI, and the plugin never parses the log at start-up
       - the shared diagnostic lease, at most one run per 10 minutes, and a 20 s limit
       - turns itself off after 3 failures in a row, plus an OPTIONS on/off switch
       - a fixed 512 × 32-byte ring file with a CRC and version on each record
       - temporary file, sync, then rename for every write
       - free-space check, clock sanity checks, strict bounded parsing
       - host tests including simulated power cuts
   - Unknowns to settle with the log: which history entry is the newest (one changed during a read
     check), and whether the `586B`/`586C`/`586E` minute counters cover only the last parking.
3. **Drive checks:**
   - `5881` against `5940` while shifting
   - VANOS ranges (`4506`/`4507` are absolute cam positions, so they need a reference), then add the
     VANOS tiles
   - whether LPFP (`58F3`) is absolute or gauge
   - knock and timing at full throttle
4. **ShiftDeck gear display**: the gear sensor `5940`, holding the last gear for about 300 ms while
   passing through neutral. RPM speed stays the priority.
5. **bm3 button tests.** These are DME writes and need opt-in for each one.
6. **bm3 Reset Adaptations**: confirm bm3's selection first, either from bm3 support or by reading
   `400A 4046 4008 4006 400B 402D 4025 403F 403D` before and after.
7. **Install script** for the proven staged procedure.
8. **iDrive hero image**: replace `hero_myvehicle_F22`. This is a system-partition write and needs a
   separate go-ahead.
9. Head-unit request latency (40 ms against 30 ms from a computer), and a cold start without the ENET
   cable plugged in.
10. The battery is the original from 2018. It spent 2–3 days below its start limit in November and read
    11.74 V with the ignition on. It probably needs load-testing and replacement before winter; a new
    one needs registering, which is a DME write and needs approval.

## Catching up with upstream

Upstream (as of `60704f8`):
- restructured `source/native/src` into `common/`, `plugins/`, `workers/` and `ipc/`
- moved `plugin/` to `manifests/` and merged `native/tools` into `source/tools`
- dropped the `PERF_LAB`, `NATIVE_FAST` and `*_ONLY` defines
- split `collector.c` into one file per worker (`workers/boost_deck_worker.c` …)
- moved the BoostDeck wire format to `NBG7`
- added its **own configurable BoostDeck**: 31 signals in `common/boost_signals.def`, per-slot editing,
  `BoostDeck.prefs`, firmware/engine profiles and alerts
- added WarmDeck, LaunchDeck, HybridDeck and others

A plain merge would conflict in most of our files and duplicate what upstream now offers. The
recommended approach is to **port the N55 work onto upstream's framework**, one step at a time, on a
new branch from `origin/main`, with host tests, a QNX VM build and a framework ABI audit after each
step:

1. HSFZ robustness (`diag_link.c`, `diagnostic_gateway.h`): check what upstream already has, then port
   the gateway list, discovery and return from MOST.
2. Two DIDs per request (`did_pair.h`), and ShiftDeck's paired RPM and speed.
3. Engine Fault Codes: the N55 table, selectable rows and detail pages.
4. BoostDeck: add the N55 signals to upstream's signal list (or an N55 profile), with our scalings
   (`n55_metrics.c`, `SIGNALS.md`). Keep what upstream lacks:
   - one read per DID, and any-pair scheduling
   - per-cylinder timing and misfire
   - knock with a fade and an event count
   - battery tiles with their rules
   - the ENGINE warm-up panel
   - zero-mark bars

   Map the owner's `tiles.txt` layout onto upstream's preferences.
5. Rebuild, run the ABI audit against the car's frameworks, and install with the staged procedure. The
   car currently runs our v11b, and the backups above allow going back.

Decide with the owner whether to keep our tile UI or adopt upstream's gauge editor before step 4.

## Setting up a new computer

From the repo:
- the QNX SDP 6.5 VM: `source/docs/SETUP-NOTES.md` and `source/docs/BUILD.md`
- Python with `pillow`, `pyelftools` and `capstone`, for the artwork and the ABI audit
- host tests: `make -C source/native test`

Private, not in the repo; copy from the old computer over a private channel (USB drive or an encrypted
archive), never through GitHub:
- the car's framework pair (`libAppFramework.A.so`, `libCoreFramework.A.so`), for building and the ABI
  audit. They can also be read again from the car.
- the backup of the car's original `ID5` data folder (2026-11-18), install packages v8–v11b, the
  rollback notes, status notes, and read-check findings (`check-2026-11-19`)
- the SSH config and `known_hosts` entry for the head unit
- the VM's SSH key, unless the VM is rebuilt

Optional: the SGBD database used for DID lookups and fault texts, `bmw.sqlite` from
[gediz/bmw-unified-db](https://github.com/gediz/bmw-unified-db/releases/latest).
