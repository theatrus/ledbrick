# Driver measurements, 2026-10-08

One LEDBrick Plus, LED supply on (50.9 V), other channels frozen at their schedule values. Each file lists the commands sent (`pwm` in %, `current` in A) and the INA228 supply current readings (A) after each, plus readings with the channel off (`baselines`). Channels are 1-based. Findings: `../../FINDINGS.md`.

| File | Channel | What |
| --- | --- | --- |
| `staircase_ch5.json` | 5 RBCen | 100% PWM, current 98-134 mA in 2 mA steps: driver steps and rounding |
| `pwm_sweep_ch5.json` | 5 | Duty 1-25% at 1.2 A and 164 mA |
| `pwm_sweep2_ch5.json` | 5 | Duty 2-100% at 164 and 300 mA |
| `pwm_sweep_ch2.json` | 2 CW | Duty 2-100% at 105 and 200 mA, 2-15% at 1.0 A |
| `pwm_high_ch2.json` | 2 | Duty 40-100% at 105 and 200 mA: pulse loss near full duty |
| `pwm_high_ch5.json` | 5 | Duty 40-100% at 164 mA |
| `floor_check_ch5.json` | 5 | 168 mA: shares 2-25% sent as plain duty (`old`) and with the first loss model (`new`) |
| `floor_check2_ch5.json` | 5 | 168 mA: shares 2-25% with the final loss model |
| `pwmfirst_check_ch5.json` | 5 | 1.2 A: shares 2-25% with the final loss model |
| `tool_pwm_ch5.json`, `tool_stair_ch5.json` | 5 | Trial runs of `tools/ina228_sweep.py` |
