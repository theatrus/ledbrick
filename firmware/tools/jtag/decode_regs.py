"""Decode an ESP32-S3 register dump from read_regs.cfg, independent of the drivers.

    python3 decode_regs.py dump.log status_before.json status_after.json

Follows each LED pin through the GPIO matrix to its MCPWM generator, operator, timer and
comparator, or to its LEDC channel, and reports frequencies, duties and forced levels. Each
channel's ADIM duty is checked against the current /api/status reported before and after
the dump (TPS922053: current = 2 A x duty). Pins are the LEDBrick Plus controller's.
"""
import re, sys, json
log, st_before, st_after = sys.argv[1], sys.argv[2], sys.argv[3]
mem = {}
for line in open(log, encoding="utf-8", errors="replace"):
    m = re.match(r"^0x([0-9a-f]+): ((?:[0-9a-f]{8} ?)+)", line.strip())
    if m:
        for i, w in enumerate(m.group(2).split()): mem[int(m.group(1), 16) + 4 * i] = int(w, 16)
R = lambda a: mem[a]
BASE = {0: 0x6001E000, 1: 0x6002C000}
def timer(g, t):
    b = BASE[g]; gclk = 160e6 / ((R(b) & 0xFF) + 1)
    cfg0, cfg1 = R(b + 0x4 + 0x10 * t), R(b + 0x8 + 0x10 * t)
    tclk = gclk / ((cfg0 & 0xFF) + 1); counts = ((cfg0 >> 8) & 0xFFFF) + 1
    return dict(gclk=gclk, tclk=tclk, counts=counts, freq=tclk / counts, running=(cfg1 & 7) == 2, mode=(cfg1 >> 3) & 3)
def generator(g, op, gen):
    b = BASE[g]; ob = b + 0x3C + 0x38 * op
    t = (R(b + 0x38) >> (2 * op)) & 3
    act = R(ob + 0x14 + 4 * gen); force = R(ob + 0x10)
    fmode = (force >> (6 + 2 * gen)) & 3
    utez, utea, uteb = act & 3, (act >> 4) & 3, (act >> 6) & 3
    cmp_sel = "A" if utea else ("B" if uteb else "?")
    cmp = R(ob + 0x4 + (0 if cmp_sel == "A" else 4)) & 0xFFFF
    tm = timer(g, t)
    if fmode == 1: duty = 0.0; how = "forced LOW"
    elif fmode == 2: duty = 1.0; how = "forced HIGH"
    else: duty = cmp / tm["counts"]; how = f"cmp{cmp_sel}={cmp}/{tm['counts']}"
    return dict(timer=t, tm=tm, duty=duty, how=how, act=act, cmp=cmp, forced=fmode)
SIG = {**{73 + i: ("LEDC", i) for i in range(8)},
       **{160 + 2 * o + k: ("MCPWM", 0, o, k) for o in range(3) for k in range(2)},
       **{166 + 2 * o + k: ("MCPWM", 1, o, k) for o in range(3) for k in range(2)}}
def pin_out(pin):
    return SIG.get(R(0x60004554 + 4 * pin) & 0x1FF)
print("== MCPWM timers in use")
seen = set()
for pin in [6, 8, 10, 12, 14, 16, 18, 34, 37]:
    s = pin_out(pin)
    if s and s[0] == "MCPWM":
        gdesc = generator(s[1], s[2], s[3]); key = (s[1], gdesc["timer"])
        if key not in seen:
            seen.add(key); tm = gdesc["tm"]
            print(f"  MCPWM{s[1]} timer{gdesc['timer']}: group {tm['gclk']/1e6:.0f} MHz, timer {tm['tclk']/1e6:.2f} MHz, "
                  f"{tm['counts']} counts -> {tm['freq']/1e3:.3f} kHz, {'running' if tm['running'] else 'STOPPED'}, mode {tm['mode']}")
print("== LEDC")
for tmr in range(4):
    c = R(0x600190A0 + 8 * tmr); res = c & 0xF; div = ((c >> 4) & 0x3FFFF) / 256
    if c and div: print(f"  timer{tmr}: {res} bits, div {div:.4f} -> {80e6/div/(1<<res):.1f} Hz")
b, a = json.load(open(st_before)), json.load(open(st_after))
print("== Dimming (EN/PWM) and current (ADIM) per channel")
for i, (pp, cp) in enumerate(zip([5, 7, 9, 11, 13, 15, 17, 33], [6, 8, 10, 12, 14, 16, 18, 34])):
    lp = pin_out(pp); cfg0 = R(0x60019000 + 0x14 * lp[1])
    ledc_duty = (R(0x60019010 + 0x14 * lp[1]) & 0x7FFFF) / 16 / (1 << (R(0x600190A0 + 8 * (cfg0 & 3)) & 0xF)) * 100
    s = pin_out(cp); gd = generator(s[1], s[2], s[3]); counts = gd["tm"]["counts"]
    ib, ia = b["channels"][i]["current"], a["channels"][i]["current"]
    exp = sorted({0 if x < 0.05 else max(1, min(counts - 1, int(round(counts * x / 2.0)))) for x in (ib, ia)})
    got = 0 if gd["forced"] == 1 else (counts if gd["forced"] == 2 else gd["cmp"])
    ok = min(exp) <= got <= max(exp)
    print(f"  ch{i+1}: EN/PWM GPIO{pp} LEDC ch{lp[1]} t{cfg0 & 3} {ledc_duty:6.2f}% (sched {b['channels'][i]['pwm']:6.2f}%) | "
          f"ADIM GPIO{cp} MCPWM{s[1]} op{s[2]}{'AB'[s[3]]} t{gd['timer']} {gd['how']} = {gd['duty']*2:.4f} A "
          f"(cmd {ib:.3f}->{ia:.3f}, expect ticks {exp}) {'OK' if ok else 'MISMATCH'}")
s = pin_out(37); gd = generator(s[1], s[2], s[3])
print(f"  fan GPIO37 MCPWM{s[1]} op{s[2]}{'AB'[s[3]]} t{gd['timer']} {gd['how']} -> pin duty {gd['duty']*100:.1f}% (inverted: fan {100 - gd['duty']*100:.1f}%)")
