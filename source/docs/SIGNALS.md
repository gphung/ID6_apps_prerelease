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
| Active Ethanol | Not requested, and the tile is removed from the N55 background | The query is an MHD+ MG1 layout |
| Oil pressure | `586F` is absolute hPa in the SGBD; the gauge subtracts ambient (`4201`) | `586F` = `0417` (1047 hPa) with the engine off, about ambient |
| AFR | Unchanged, pending idle check | `5889` = `FFFF` with the engine off |

Coolant `4300` is one byte × 0.75 − 48 °C on this DME (`4B` = 8.25 °C, against `01 05` = 8 °C); the profile keeps `01 05`.

BoostDeck labels the N55 transmission slot **Engine** (background `56301`, from `native/tools/make_n55_background.py`). The bar spans 100–300 °F and takes its colour from the oil warm-up band rather than the selected gauge colour: Cold (blue) below 180 °F, Warming (BMW Orange) from 180 °F, Ready (green) from 194 °F, and Hot (red) from 266 °F. Each band is held until the reading falls 1 °C below its threshold, and the band name follows the value.

## Engine Fault Codes on the F87 M2 N55

`EngineCodes/profile.txt` holding `n55` selects `native/src/dme_descriptions_n55.h`: all 1,378 fault-memory codes of SGBD `mevd172y` (MEVD17.2 / 17.2.6 / 17.2.G, SGBD index `0xF1910`) from bmw-unified-db 11.004, translated from BMW's German location texts. `native/tools/make_dme_descriptions.py` extracts the German text to a private CSV and writes the table from the translated CSV, so the BMW-derived German text is not committed. The 254 codes that the reference 340i table shares with this DME have identical German texts in both SGBDs.
