# LEDBrick Plus findings

What we learned bringing up and testing a LEDBrick Plus (8-channel controller `rv_ledbrick_8ch`, emitter `ledbrick_plus_led`, ESP32-S3). Measured on one board in October 2026 unless noted.

## LED driver (TPS922053)

- **Current (ADIM/HD pin):** the driver reads the PWM duty digitally. LED current = 2 A x duty (0.1 ohm sense resistor). It resolves 8 bits up to 39 kHz and 6 bits up to 156 kHz, so one step is 7.8 mA at 8 bits. The first ADIM pulse must be at least 1 us. Minimum on-time is 100-150 ns.
- **Old firmware gave 80 mA steps.** The legacy MCPWM driver ran a 1 MHz timer at about 40 kHz: 25 counts, so only 25 current steps. INA228 supply current showed the staircase and JTAG register reads confirmed the 25-count timer.
- **Fix:** ADIM runs at 10 kHz on `mcpwm_unified`'s new driver (10 MHz timers, 1000 counts, forced levels at 0% and 100%). Pinned in `ledbrick-plus.yaml` (mcpwm_unified PRs #3 and #4, commit 2b5f4cd).
- **Hybrid dimming hazard:** if ADIM stays low while EN/PWM pulses at enable, the driver sets the current from the EN/PWM duty alone, up to 2 A. `packages/channel.yaml` holds EN/PWM off while the commanded current is below 50 mA. EN/PWM low for 57-77 ms disables the driver.
- **EN/PWM (dimming):** 1 kHz, 14-bit (LEDC).
- **The driver rounds the ADIM duty down.** The datasheet doesn't say. Stepping RBCen's command from 98 to 134 mA in 2 mA steps at 100% PWM, supply current rose once per 7.8 mA, and each edge fell where rounding down puts it (between 100 and 102 mA, 108 and 110, and so on); rounding to nearest would put each edge 4 mA lower. The curve dimmer commands mid-step, the most room either side.
- **Checked:** in curve mode at levels 37.5% and 2%, the ADIM compare and LEDC duty read over JTAG matched the dimmer run on a PC, landing on driver steps 50 and 21. LED supply off.

## PWM pulse loss

Each EN/PWM pulse gives less light than its width. Measured on RBCen (13 Rubix, about 38 V) and CW (8 LUXEON C, about 22 V) with the LED supply on, from INA228 supply current against duty at fixed current, with the other channels frozen:

- **Long pulses lose about 19 us each** (1.9% of the 1 kHz period), at 105 mA to 1.2 A on both channels. The best-conditioned fits gave 17-19.5 us; a rougher one (duties 10-60%) gave 30 us. 100% duty has no edges and loses nothing.
- **Short pulses lose less.** The loss sets in over a tail: at 1.2 A it is sharp (1% duty gives about 20% of its light, 3% about 50%); at the floor currents (about 170 mA) it is wide (3% duty gives most of its light). The firmware models light as d - 1.9% x (1 - e^(-d/tail)), with tail 1.9% at 300 mA and above and 4% at 170 mA and below.
- **The driver draws 1-4 mA more supply current** whenever its EN pin is pulsing, without light.
- **Effect on manual mode:** a manual PWM gives (duty - 1.9%) of the light at that current, so 15% PWM gives about 87% of what 15% suggests, and 5% gives about 65%.
- **The curve dimmer allows for it:** it raises each pulsed duty to give the intended light, keeps pulsed duties at or below 97.5% (shorter off-times were not measured), and converts manual settings by the light they really give. Checked on RBCen with the model: 3-25% of a step's light came out within 1% at 1.2 A and within 5% at 168 mA. Without it, 5-10% came out 20-29% dim at 168 mA.
- The INA228 cannot pin the tail below about 3% duty at low currents: the signal there is a few mA against 0.3 mA of noise. A light sensor would.
- To measure another board: `tools/ina228_sweep.py` (current staircase, or PWM sweep with a loss estimate). The data behind these numbers is in `measurements/2026-10-08/`. The sweeps use `/api/channel/control` with the scheduler off and read `/api/status` `total_current`: a new INA228 value every 5 s, averaged over about 0.5 s.

## Emitter, as built

- **WW is not as drawn.** The schematic has 4 LUXEON C 3900K, 2 Deep Red and 2 PC Amber. Built boards have PC Amber in the Deep Red places: 4 3900K and 4 PC Amber. `channels.json` has the as-built list.
- **Violet has 2 UV LEDs:** VIOSYS CUN8 (full part number not in the design files), 4th and 5th in the string. No published curve, so the firmware treats them as standard LEDs.
- **0-ohm jumpers** sit in the PCBlue (mid-string) and violet (cathode end) strings. They are not LEDs.
- **Common anode:** all 8 strings share JA1 pins 1-8; each channel's cathode comes back on JC1.
- **Temperature sensors on the emitter:** two MCP9808 (U1, U3) both at I2C address 0x18, and a DS18B20 on 1-Wire. Both MCP9808s are in the BOM and pick-and-place, so fitting both would clash on the bus. The firmware uses the DS18B20.
- **Harnesses:** the 6-pin sensor connector is mirrored between emitter and controller (pin n to pin 7-n), so that harness reverses pin order. The 10-pin LED harnesses are 1:1.
- Details, with LED designators and positions: `emitter_netlist.json`.

## LED curves

- Curves come from Lumileds DS144 (LUXEON C) and DS309 (LUXEON Rubix), digitized from the PDFs' vector paths: `tools/led_curves/lumileds_curves.json` names the figure and page for each.
- The Rubix uses DS309 2023 thermal data, which matches the emitter BOM. The 2026 revision's current curve is the same.
- Neither datasheet publishes colour or wavelength shift against current, so PWM-first dimming is the choice where colour must not move.
- **Standard LED (`standard_led`):** the point-wise median of nine LUXEON C curves (all but Deep Red), over the range they all cover. Rated 1250 mA, Rth 3.0 C/W. Five of the nine curves stop at 1050 mA; they continue along their last 100 mA to 1250 mA first. Up to 1225 mA the result lies between the real LUXEON C white and mint curves. It stands in for LEDs without a model: the UV LEDs, boards without an LED map, and models lost from flash.
- `tools/led_curves/led_models.json` has every built-in model with the exact values the firmware uses, generated alongside `led_models.cpp`.

## Fan and sensors

- Fan PWM is GPIO37 (16 kHz, inverted), enable GPIO36, tach GPIO35 with an external pull-up. A tach that reads zero while the fan spins was a connector fault, not firmware.
- The 1-Wire bus is noisy: occasional scratchpad CRC errors are normal. The firmware holds a sensor's last good value through 5 failed reads in a row.
- INA228 (I2C 0x40) measures supply voltage and current. With the LED supply on, it shows each current step.

## Settings in flash

- The NVS partition is 448 KB (`partitions.csv`). Settings use two fixed-size records: the schedule (8 KB) and custom LED models (12 KB). Any change rewrites the whole record; an unchanged record is not rewritten.
- A schedule save uses about two 4 KB pages, so each page is erased about once per 50 saves. At 100,000 erase cycles that is about 5 million saves.
- ESPHome holds preference writes for up to a minute. The firmware writes settings 250 ms after the last change, so a power cut soon after a change keeps it.

## Updating and testing

- Wait at least a minute after an OTA update before resetting. A reset before ESPHome marks the build good rolls back to the previous build.
- The USB serial log misses about 2 s after a reset, so setup messages are lost. Check boot results through the API.
- On Windows, build from PowerShell. The ESP-IDF install fails under Git Bash (MSYSTEM).
- JTAG register reads (`tools/jtag/`) show what each pin outputs, independent of the firmware's own reports. Halting the CPU stops the watchdogs, so reset afterwards.
