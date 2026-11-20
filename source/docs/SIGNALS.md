# Reference signal definitions

These are the reference Gen 1 B58/340i candidates in the current source. They are not a compatibility list for other engines. Requests use HSFZ TCP port 6801 and UDS service `22` unless marked OBD. Positive UDS replies start with `62` plus the requested DID. The table describes the data bytes after that prefix.

| Display | ECU | DID or PID | Reference conversion |
| --- | --- | --- | --- |
| Boost | DME `12` | `4205` and `4201` | Two big-endian bytes each. Absolute kPa is raw ÷ 128, ambient kPa is raw ÷ 256. The collector subtracts ambient and converts to psi. |
| AFR | DME `12` | `5889` | Two bytes. Lambda is raw × 16 ÷ 65535, then converted to gasoline-equivalent AFR. |
| Ignition timing | DME `12` | `4A49` | Two signed big-endian bytes ÷ 10. The reference `FFFF` was ambiguous and stays blank. |
| Intake air | DME `12` | `580F` | One byte, raw × 0.75 − 48 °C. `FF` is rejected. |
| Transmission temperature | EGS `18` | `DA12` | Two big-endian bytes, raw − 48 °C. |
| Rail pressure | DME `12` | `56D7` | Two big-endian bytes, raw ÷ 50 bar. |
| Oil pressure | DME `12` | `586F` | Two big-endian bytes, raw ÷ 1000 bar. |
| Coolant temperature | DME `12` | `4300` | Two big-endian bytes, raw ÷ 10 − 273.15 °C. |
| ShiftDeck RPM | DME `12` | OBD `01 0C` | Two big-endian bytes ÷ 4. |
| ShiftDeck speed | DME `12` | OBD `01 0D` | One byte in km/h, displayed in mph. |

The worker enforces data length, plausibility limits, and freshness. Look at the code for exact bounds and units. If the DME reports coolant via `01 05`, confirm that reading against the intended coolant channel before adding an explicit fallback. An OBD PID and a BMW DID are distinct requests with distinct reply formats. [Request table](../native/src/collector.c) · [Decoder](../native/src/gauge_core.c)

## F87 M2 N55 profile

`BoostDeck/profile.txt` containing `n55` starts the collector with `--profile n55`. Captured on a 2018 M2 (N55, manual, bootmod3) with ignition on and the engine off. Readings at idle still need comparison with an independent logger.

| Display | Change from reference | Capture |
| --- | --- | --- |
| Coolant temperature | OBD `01 05`, one byte − 40 °C, instead of DID `4300` | `4300` returned one byte (`4B`); `01 05` returned `30` (8 °C) with IAT at 9.75 °C |
| Transmission slot | DME oil temperature `4408`, two signed bytes × 0.1 °C (`mevd172y` SG_FUNKTIONEN). The EGS is not requested (manual) | `DA12` timed out. `4408` still needs an idle comparison with an independent oil reading on this car |
| Rail pressure | DME `58EF`, filtered actual rail pressure, two bytes × 0.0005 MPa (0.005 bar) | `56D7` answered NRC `31`. `58EF` is from the SGBD and still needs a reading on this car |
| Active Ethanol | Not requested; the tile becomes the ENGINE warm-up panel | The query is an MHD+ MG1 layout |
| Oil pressure | `586F` is absolute hPa in the SGBD; the gauge subtracts ambient (`4201`) | `586F` = `0417` (1047 hPa) with the engine off, about ambient |
| AFR | Unchanged, pending idle check | `5889` = `FFFF` with the engine off |

Coolant `4300` is one byte × 0.75 − 48 °C on this DME (`4B` = 8.25 °C, against `01 05` = 8 °C); the profile keeps `01 05`.

BoostDeck captions the N55 transmission slot **OIL TEMP** and turns the tall Active Ethanol tile into an **ENGINE** warm-up panel (background `56301`, from `native/tools/make_n55_background.py`). Both bars span 100–300 °F and take the band colour rather than the selected gauge colour: Cold (blue) below 180 °F, Warm (BMW Orange) from 180 °F, Ready (green) from 194 °F, and Hot (red) from 266 °F. Each band is held until the reading falls 1 °C below its threshold. The ENGINE panel shows the band name under the thermometer, and a strip beside it marks the bands.

### Configurable tiles

With the tiles layout (`native/tools/make_n55_layout.py --tiles`, artwork from `native/tools/make_n55_tiles_art.py`), the eight tiles show any of the 41 metrics in `native/src/n55_metrics.c`, chosen in **OPTIONS > Tiles** and saved in `BoostDeck/tiles.txt` (eight keys, `-` for a blank tile; a damaged file gives the default layout). The plugin starts the collector with `--metrics` and the keys it needs: each tile's metric, oil temperature for the ENGINE panel, and RPM when a battery tile needs to know whether the engine runs. The collector reads each DID once per due time, however many tiles show it (Timing and Timing 1 are both `4A49`), two per request. A DID the DME refuses is skipped for 30 s without ending pairing.

Values read on the car on 2026-11-19 at warm idle, then with the engine off and loads on:

| Metric | DID and scaling | Reading |
| --- | --- | --- |
| Timing 1–6 | `4A49`, `4A4E`, `4A4C`, `4A4F`, `4A4A`, `4A4D`, signed × 0.1 ° | −6.8, −6.4, −7.6, −5.7, −6.9, −5.4 ° |
| Throttle (pedal) | `480B` × 0.0015259 % | 0 % |
| Knock | `4A36`, 0/1 | 0 |
| Boost target | `4AB0` × 0.0390625 hPa, minus ambient | ambient at idle |
| HPFP / HPFP target | `58EF` / `5837` × 0.005 bar | 76.9 / 76.6 bar |
| LPFP | `58F3` × 0.1 kPa | 6.8 bar, engine off and at idle |
| Wastegate | `4654` mm between the learned stops `576A` (open) and `576B` (closed) | 3.6 % open |
| Battery | `5853` (IBS) × 0.001 V | 15.03–15.14 V charging, 12.49 V engine off at 10 A |
| Battery amps | `5852` × 0.02 − 200 A; the IBS reports charging as negative, so the tile negates it | +20.6 A after the start |
| Battery % | `4023` byte 17 (charge state today) | 58 % |
| Start margin | `460E` × 0.004 − 100 %: charge minus the start-capability limit | 24.7 % |
| Exhaust temp | `5896` × 0.0234375 − 273.15 °C, the DME's model after the main catalyst | 242 °C |

The timing DIDs are the DME's firing-order array (1-5-3-6-2-4), mapped to cylinders in the same order as its per-cylinder roughness and knock entries, which name their cylinders. That mapping is an assumption. `5916` (battery charge) answers NRC `31`. Whether LPFP is absolute or gauge pressure is not yet known. The misfire counters (`4B23`–`4B32`) are taken to count a window and restart at 0; the tile's second number adds the windows for the drive.

Warnings turn a tile red: Knock for 1 s after each knock, fading (the text stays live, with the count since BoostDeck opened); a misfire count above 0; Battery below 12.0 V, or above 15.5 V while the engine runs; Battery amps after 15 s of more than 10 A discharge with the engine running; Battery % below 40 %; Start margin below 5 %. Timing, Fuel trim and Battery amps bars start at a zero mark.

## Engine Fault Codes on the F87 M2 N55

`EngineCodes/profile.txt` holding `n55` selects `native/src/dme_descriptions_n55.h`: all 1,378 fault-memory codes of SGBD `mevd172y` (MEVD17.2 / 17.2.6 / 17.2.G, SGBD index `0xF1910`) from bmw-unified-db 11.004, translated from BMW's German location texts. `native/tools/make_dme_descriptions.py` extracts the German text to a private CSV and writes the table from the translated CSV, so the BMW-derived German text is not committed. The 254 codes that the reference 340i table shares with this DME have identical German texts in both SGBDs.

The N55 profile also narrows three bar ranges, which otherwise stay mostly empty on this engine: boost 0–25 psi (bm3 maps peak around 20–22 psi), rail pressure 0–200 bar (the N55 system maximum; about 50 bar at idle), and gauge oil pressure 0–6 bar. These are typical N55 figures; compare them with a log from the car.

With the N55 profile, BoostDeck keeps temperature and pressure units as separate choices, saved to `BoostDeck/units.txt` (`c` or `f`, then `psi` or `bar`) and loaded at start. The defaults are Celsius and psi. `native/tools/make_n55_layout.py` writes the N55 `BoostDeck.xml`, whose OPTIONS offer Temperature: Celsius / Fahrenheit and Pressure: psi / bar in place of Units: Imperial / Metric.

The N55 DME answers two DIDs per `22` request in the time of one and refuses three or more. The N55 profile pairs boost (`4205`) with AFR (`5889`) and timing (`4A49`) with oil pressure (`586F`) in BoostDeck, and reads RPM (`4807`, × 0.25) with speed (`4AB1`, × 1/128 km/h) in ShiftDeck (`ShiftDeck/profile.txt` holding `n55`). A refused pair switches that worker to single requests for the rest of its run. Measured from the head unit, each request takes about 75 ms.
