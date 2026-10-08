# Boards

What we know about each LEDBrick board, and how to bring more boards up to date.

| Path | What it is |
| --- | --- |
| `ledbrick-plus/channels.json` | The LEDs in each channel, as built. The firmware's default channel LEDs are generated from it. |
| `ledbrick-plus/emitter_netlist.json` | The emitter and controller design files, analysed: strings, driver pins, UV LEDs, sensors, connectors. |
| `ledbrick-plus/FINDINGS.md` | Hardware findings and measurements: driver behaviour, as-built differences, flash storage, test methods. |
| `../tools/led_curves/led_models.json` | Every built-in LED model, standard LED included, with the exact values the firmware uses. Generated. |
| `../tools/led_curves/lumileds_curves.json` | The digitized Lumileds datasheet curves and the standard LED's definition. The models are built from it. |
| `../tools/board_config.py` | Back up, copy and check a board's settings over the web API. |
| `../tools/jtag/` | Read the LED driver registers over USB JTAG, to check what a board outputs. |

## Change the LED data

1. Edit `ledbrick-plus/channels.json` (which LEDs are in a channel) or `tools/led_curves/lumileds_curves.json` (curves).
2. From `firmware/`, run `python3 tools/led_curves/gen_led_models.py`. It rewrites `components/ledbrick_scheduler/led_models.cpp` and `tools/led_curves/led_models.json`. Commit both.
3. Run `make test-led`, then build and flash.

LEDs with no published curve go in as `standard_led`.

## Update a board

1. Back up its settings. From `firmware/`:
   ```bash
   python3 tools/board_config.py backup 192.168.1.196 backups/tank1
   ```
   This saves `schedule.json`, and `led_models.json` with any custom LED models posted to the board. `backups/` is git-ignored: backups hold the tank's schedule and location.
2. Build and flash over the network:
   ```bash
   uvx --from esphome==2026.9.1 esphome run ledbrick-plus.yaml --device 192.168.1.196
   ```
   On Windows, build from PowerShell, not Git Bash: the ESP-IDF install fails under MSYSTEM. `secrets.yaml` in git is a placeholder; use your own.
3. Wait at least a minute before any reset or power cycle. ESPHome marks a new build good after a minute of running. A reset before that rolls the board back to the previous build (boot log: "OTA rollback detected").
4. Check it:
   ```bash
   python3 tools/board_config.py check 192.168.1.196
   ```
   It lists each channel's mode and LEDs, and flags any LED model the board doesn't have.

## Copy settings to another board

```bash
python3 tools/board_config.py apply 192.168.1.197 backups/tank1
```

It posts any custom LED models first, since channels name them, then the schedule. An empty models file is skipped. The PWM scale is not copied. Edit `schedule.json` first if the boards' channels differ.

The built-in models need nothing copied: they are in the firmware. Custom models (`POST /api/led_models`) are only for a part the firmware doesn't know, and they are not normally needed.
