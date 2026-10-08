#!/usr/bin/env python3
"""Measure a channel's driver with the board's INA228 supply current sensor.

    python3 tools/ina228_sweep.py staircase HOST CH FROM_MA TO_MA STEP_MA [OUT.json]
    python3 tools/ina228_sweep.py pwm HOST CH CURRENT_A DUTY [DUTY ...] [OUT.json]

staircase holds channel CH (1-8) at 100% PWM and steps its current command, to show the
driver's current steps. pwm holds a current and steps the PWM duty (in %). To estimate the
light each pulse loses (boards/ledbrick-plus/FINDINGS.md), use duties near 100%, such as
100 97.5 95 90 80 60 40: the estimate extends a line through the pulsed duties to 100%, so
duties far from 100% make small errors large.

The scheduler is switched off while measuring, which freezes the other channels, and
switched back on afterwards if it was on. The channel lights during the test: keep the
currents and duties near what the tank normally gets. Each point waits for 3 new INA228
readings (one every 5 s). Needs the LED supply on. Standard library only.
"""
import json
import sys
import time
import urllib.request

READINGS = 3


def request(host, method, path, body=None):
    url = (host if host.startswith("http") else "http://" + host).rstrip("/") + path
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, method=method,
                                 headers={"Content-Type": "application/json"} if data else {})
    with urllib.request.urlopen(req, timeout=15) as resp:
        text = resp.read().decode()
        return json.loads(text) if text.strip().startswith("{") else text


def supply(host, n, last):
    """n new supply current readings (A)."""
    values = []
    deadline = time.time() + 8 * n + 10
    while len(values) < n and time.time() < deadline:
        time.sleep(0.7)
        value = request(host, "GET", "/api/status")["total_current"]
        if value != last:
            values.append(value)
            last = value
    return values, last


def mean_after_first(values):
    # The first new reading can still average in the previous setting
    rest = values[1:] or values
    return sum(rest) / len(rest)


def run(host, channel, points, out):
    """points: list of (pwm %, current A). Returns baseline (A) and per-point means (A)."""
    log = {"channel": channel + 1, "points": [], "baselines": []}
    was_enabled = request(host, "GET", "/api/status")["enabled"]
    try:
        request(host, "POST", "/switch/web_scheduler_enable/turn_off")
        time.sleep(2)
        last = request(host, "GET", "/api/status")["total_current"]

        def baseline(label):
            nonlocal last
            request(host, "POST", "/api/channel/control", {"channel": channel, "pwm": 0, "current": 0})
            values, last = supply(host, READINGS, last)
            log["baselines"].append({"label": label, "readings": values})
            print(f"channel off ({label}): {mean_after_first(values) * 1000:.2f} mA")

        baseline("before")
        for pwm, current in points:
            request(host, "POST", "/api/channel/control", {"channel": channel, "pwm": pwm, "current": current})
            values, last = supply(host, READINGS, last)
            log["points"].append({"pwm": pwm, "current": current, "readings": values})
            print(f"pwm {pwm:6.2f}%  current {current * 1000:6.1f} mA  supply {mean_after_first(values) * 1000:8.2f} mA")
        baseline("after")
    finally:
        if was_enabled:
            request(host, "POST", "/switch/web_scheduler_enable/turn_on")
        if out:
            with open(out, "w", encoding="utf-8", newline="\n") as f:
                json.dump(log, f, indent=1)
                f.write("\n")
    bases = [mean_after_first(b["readings"]) for b in log["baselines"]]
    base = sum(bases) / len(bases)
    return base, [(p["pwm"], p["current"], mean_after_first(p["readings"])) for p in log["points"]]


def line(rows):
    n = len(rows); sx = sum(x for x, _ in rows); sy = sum(y for _, y in rows)
    sxx = sum(x * x for x, _ in rows); sxy = sum(x * y for x, y in rows)
    slope = (n * sxy - sx * sy) / (n * sxx - sx * sx)
    return (sy - slope * sx) / n, slope


def staircase(host, channel, start, stop, step, out):
    currents = []
    value = start
    while value <= stop + 1e-9:
        currents.append(round(value / 1000, 4))
        value += step
    base, results = run(host, channel, [(100, c) for c in currents], out)
    print("\nedges (supply rises by more than half the typical step):")
    deltas = [(results[i][1], results[i][2] - results[i - 1][2]) for i in range(1, len(results))]
    typical = sorted(d for _, d in deltas)[-max(1, len(deltas) // 4)]
    for current, delta in deltas:
        if delta > typical / 2:
            print(f"  at {current * 1000:.0f} mA: +{delta * 1000:.2f} mA supply")


def pwm_sweep(host, channel, current, duties, out):
    base, results = run(host, channel, [(d, current) for d in duties], out)
    rows = [(d, (s - base) * 1000) for d, _, s in results]
    pulsed = [(d, y) for d, y in rows if 8 <= d < 100]
    full = [y for d, y in rows if d >= 100]
    if len(pulsed) >= 2 and full:
        a, k = line(pulsed)
        gap = full[0] - (a + 100 * k)
        loss = gap / k
        print(f"\npulsed duties 8-99%: {k:.3f} mA per %; 100% sits {gap:.2f} mA above that line")
        print(f"light lost per pulse: {loss:.2f}% of the period ({loss * 10:.1f} us at 1 kHz); "
              f"driver overhead while pulsing: {a + k * loss:.2f} mA")
        if min(d for d, _ in pulsed) < 40 or max(d for d, _ in pulsed) < 90:
            print("(rough: duties from 40% to 97.5% pin the loss best)")
    else:
        print("\ninclude 100 and two or more duties from 8% to 99% to estimate the pulse loss")


def main(argv):
    if len(argv) < 6 or argv[1] not in ("staircase", "pwm"):
        sys.exit(__doc__)
    host, channel = argv[2], int(argv[3]) - 1
    if not 0 <= channel < 8:
        sys.exit("CH is 1-8")
    args = argv[4:]
    out = args.pop() if args and args[-1].endswith(".json") else None
    if argv[1] == "staircase":
        if len(args) != 3:
            sys.exit(__doc__)
        staircase(host, channel, float(args[0]), float(args[1]), float(args[2]), out)
    else:
        pwm_sweep(host, channel, float(args[0]), [float(d) for d in args[1:]], out)


if __name__ == "__main__":
    main(sys.argv)
