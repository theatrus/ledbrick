# Boards

What we know about each LEDBrick board, and how to bring more boards up to date.

| Path | What it is |
| --- | --- |
| `ledbrick-plus/channels.json` | The LEDs in each channel, as built. The firmware's default channel LEDs are generated from it. |
| `ledbrick-plus/emitter_netlist.json` | The emitter and controller design files, analysed: strings, driver pins, UV LEDs, sensors, connectors. |
| `ledbrick-plus/FINDINGS.md` | Hardware findings and measurements: driver behaviour, as-built differences, flash storage, test methods. |
| `../tools/led_curves/lumileds_curves.json` | The LED curves (Lumileds datasheets) and the standard LED's definition. |
| `../tools/board_config.py` | Back up, copy and check a board's settings over the web API. |
| `../tools/jtag/` | Read the LED driver registers over USB JTAG, to check what a board outputs. |

## Change the LED data

1. Edit `ledbrick-plus/channels.json` (which LEDs are in a channel) or `tools/led_curves/lumileds_curves.json` (curves).
2. From `firmware/`, run `python3 tools/led_curves/gen_led_models.py`. It rewrites `components/ledbrick_scheduler/led_models.cpp`.
3. Run `make test-led`, then build and flash.

LEDs with no published curve go in as `standard_led`. Use custom models (below) for parts on a single board.

## Update a board

1. Back up its settings. From `firmware/`:
   ```bash
   python3 tools/board_config.py backup 192.168.1.196 backups/tank1
   ```
   This saves `schedule.json` and `led_models.json` (custom LED models). Keep backups out of git: they hold the tank's schedule and location.
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

It posts the custom LED models first, since channels name them, then the schedule. The PWM scale is not copied. Edit `schedule.json` first if the boards' channels differ.

To share custom LED models between boards, keep them in a file such as `ledbrick-plus/led_models.json` and post it:

```bash
python3 tools/board_config.py models 192.168.1.197 boards/ledbrick-plus/led_models.json
```

Posting replaces the board's custom set, so the file must hold every custom model the board uses.
