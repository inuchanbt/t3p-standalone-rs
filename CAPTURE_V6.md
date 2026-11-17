# Capture V6: normal ENS220 build

**Hardware trial failed for temperature on 2026-11-16.** The device recorded
16 attempts, zero successes, 15 failures and 60 transactions. Capture SAVE/HID
export succeeded in the same trial (3.098 s, all V5 optimizations enabled).
The actual exported SYSTEM image shows ENS220 TEMP N/A. The repaint-starvation
defect is reproduced and corrected in [Capture V6.1](CAPTURE_V6_1.md), whose
initial hardware trial acquired 28.44 C with 21 successes and zero failures,
then saved/exported SYSTEM successfully. The original V6 image and offline
audit remain preserved; the history below describes that archived build.

Capture V6 packages the [verified ENS220 V3 acquisition](ENS220_TEMPERATURE_V3.md)
for normal use on the observed ENS220-based T3P. It restores the three SYSTEM
pages and excludes the eight-target comparison probe from native code.
V3's temperature interval and Capture results remain evidence for V3 and do
not validate V6's failed temperature acquisition.

| Item | ENS220 diagnostic V3 | Capture V6 |
| --- | --- | --- |
| Build option | --temperature-ens220 | --capture-ens220 |
| HID product | T3P ENS220 Temp V3 | T3P Capture V6 |
| SYSTEM APP | T3P ENS220 TEMP V3 | T3P RS CAPTURE V6 |
| SYSTEM pages | 6 | 3 |
| Temperature | Verified-ID ENS220 single shot | Same implementation |
| Eight-target comparison | Once per boot, gated | Excluded |
| Extra PA/PB diagnosis pages | Available | Excluded |
| Cached main temperature JSON | Available | Available |
| Capture backend/optimizations | V5 | Same V5 implementation |

The ENS220 state machine, its transport implementation and the Capture
storage/profile source files were byte compared with the V3 source checkpoint
and match. The V6 change is build composition, UI navigation and identity. It
introduces no new sensor commands, timing policy, address scan or reset policy.
The strict PART_ID 0321 guard remains; this is not an unverified automatic
sensor-family detection feature.

The old V5, V1, V2 and verified V3 binaries are preserved in their respective
build directories. The pre-V6 source checkpoint is
`build/temperature-ens220-v3/source-before-capture-v6.zip`.
The existing `--capture-device` option continues to select its previous V5 build.

## Normal behavior

SYSTEM has INFO, BUS AND FRAMES, and INPUT AND RUNTIME. INFO shows ENS220 TEMP.
The existing page/exit and Capture gestures retain their roles. Unacquired
serial/meter firmware fields continue to show N/A.

Temperature runs only on SYSTEM with the fast timer available, outside PD
listening and armed CHARGER operation. ID verification, first initialization,
configuration readback, stale-ready rejection, conversion deadlines, range
handling and deferred idle cleanup are the verified V3 implementation. See the
V3 document for those details and its earlier one-failure observation.

Comparison probes no longer add their five observed address NACKs to normal
startup diagnostics. Real acquisition failures still count and invalidate a
temperature; V6 does not hide errors or replace unavailable data with constants.
The earlier V3 failure's cause remains unresolved; this UI/build change is not
claimed to fix that event.

Cached temperature operation 4 retains the V3 format (110 words; SYSTEM ABI
133 words), despite the firmware's V6 name. Existing Capture operations 0..3
retain their meaning. Comparison operation 5 is refused. `temperature_read.py`
recognizes T3P Capture V6 and reads its temperature and Capture metadata without
requesting comparison results. The JSON consequently has no `probes` object.
The firmware performs no sensor transaction in response to these USB requests.

Removing the comparison buffers reduces audited static RAM from 27,920 to
20,152 bytes. The original driver and stack reservations are retained. These
numbers describe the linked builds, not measured dynamic stack consumption.

## Historical hardware confirmation procedure

Use the working T3P PC updater **V1.0.9**. Export wanted captures before changing
firmware. V6 candidate path:

```text
D:\t3p_standalone_rs\build\capture-v6\T3P_RUST_METER_CAPTURE_V6_3.0.3.bin
```

After flashing, confirm APP `T3P RS CAPTURE V6`, three SYSTEM sections and a
temperature. Keep SYSTEM visible for about ten seconds and save:

```powershell
cd D:\t3p_standalone_rs
python temperature_read.py --output build/capture-v6/device-v6-01.json --summary
```

Expected evidence is product T3P Capture V6, ENS220 ID 0321, an acquired
temperature and successes, readback mask 0F, and no comparison-probe section.
The hardware-confirmed algorithm is reused; this trial confirms the new image,
three-page navigation and composition. A new 17-minute trial is not required
solely because the comparison UI/code was removed.

Then capture SYSTEM with the established gesture and export to a fresh folder:

```powershell
python capture_export.py --output build/capture-v6/capture-01
```

Inspect the actual image and export sidecar, backend result and timing flags.
Keep reported timing as a single trial unless input images/conditions are
controlled. A pending conversion may expire during the multi-second SAVE and
recover afterward under the unchanged V3 freshness policy.

The agent's runtime did not enumerate the physical HID device in the preceding
trial; the user's PC performs the flash/buttons and saves these records.

## Reproducing the archived V6 image

The V6 source is preserved in `build/capture-v6/source-checkpoint.zip` with
hashes in its original `validation-complete.json`. Restore it into a separate
working directory when reproducing V6. Current `--capture-ens220` builds select
V6.1; use the new directory and commands in [CAPTURE_V6_1.md](CAPTURE_V6_1.md).
The original full offline results below preceded the failed hardware trial.

## Completed offline validation

The new V6 container passed full offline packaging validation on 2026-11-16.
The subsequent hardware trial above failed for temperature; these original offline results remain preserved.

| Artifact or check | Result |
| --- | --- |
| Update container | 1,540,096 bytes |
| Container SHA-256 | `53f56427b219311f05fd835463072e71268d70ccf245c0526e9e59f227a232d3` |
| Linked ELF SHA-256 | `6a8e0032db57cf247ba6e0853d9a3254f700b690dd90d81b02be785f2f243c11` |
| Audited static RAM | 20,152 bytes |
| Rust tests | 41 unit + 10 Capture |
| SYSTEM C checks | 20 passed, including V6 composition |
| Host references | 63 frames generated |
| Native Capture | SAVE/HID decode matches all 57,600 RGB565 pixels |
| Pi32 frames/actions | PD, CABLE, CHARGER, 3-page SYSTEM and graphs passed |
| Cached ENS220 preflight | 10 conversion cases and 12 pages; no GPIO/SPI MMIO |
| Comparison code | No probe symbols; operation 5 refused without MMIO |
| Independent unpacking | Full plaintext match; 44 other files and boot files match |
| Nested checksums | All 4 checked |

The build directory contains `validation-complete.json`, package/build audits,
the full validation log and `source-checkpoint.zip`. V5, V1, V2 and V3 container
hashes were also checked against their preserved values. These are offline
results; V3's physical temperature/SAVE evidence is identified separately.


## 日本語要点

V6は実機で温度取得が16試行・0成功・15失敗となり、温度はN/Aでした。
CAPTUREは3.098秒で保存・取り出しに成功し、V5最適化も有効です。
描画中に温度処理が止まる不具合を再現し、[V6.1](CAPTURE_V6_1.md)で修正します。
V6のBIN・ソース・検査記録・実機データは保持します。
