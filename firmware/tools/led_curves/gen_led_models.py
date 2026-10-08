#!/usr/bin/env python3
"""Generate components/ledbrick_scheduler/led_models.cpp from lumileds_curves.json and the
board's LED map, boards/ledbrick-plus/channels.json.

Run from the firmware directory:
    python3 tools/led_curves/gen_led_models.py

Curves are thinned to keep the table small on the ESP32. A point is dropped only while
linear interpolation between the kept points stays within MAX_ERROR of every original
point, so the firmware's curves match the digitized datasheet curves.

The standard LED (standard_model in the JSON) is the point-wise median of a set of models.
It stands in for LEDs without a model.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "lumileds_curves.json")
TARGET = os.path.join(HERE, "..", "..", "components", "ledbrick_scheduler", "led_models.cpp")
BOARD = os.path.join(HERE, "..", "..", "boards", "ledbrick-plus", "channels.json")
MAX_ERROR = 0.002  # relative to the value, for output curves; volts for Vf


def interpolate(points, x):
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        if x0 <= x <= x1:
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0) if x1 > x0 else y1
    return points[-1][1] if x > points[-1][0] else points[0][1]


def thin(points, relative):
    """Greedy: extend each segment as far as it stays within MAX_ERROR of the originals."""
    kept = [points[0]]
    i = 0
    while i < len(points) - 1:
        j = len(points) - 1
        while j > i + 1:
            seg = [points[i], points[j]]
            ok = all(abs(interpolate(seg, x) - y) <= (MAX_ERROR * abs(y) if relative else MAX_ERROR)
                     for x, y in points[i:j + 1])
            if ok:
                break
            j -= 1
        kept.append(points[j])
        i = j
    return kept


def median(values):
    v = sorted(values)
    n = len(v)
    return v[n // 2] if n % 2 else (v[n // 2 - 1] + v[n // 2]) / 2


def grid(start, stop, step):
    xs = list(range(start, stop, step))
    if xs[-1] != stop:
        xs.append(stop)
    return xs


def median_curve(curves, xs):
    """Median of the curves on the range they all cover, so every point has the same set."""
    lo = max(c[0][0] for c in curves)
    hi = min(c[-1][0] for c in curves)
    xs = sorted({x for x in xs if lo <= x <= hi} | {lo, hi})
    return [(float(x), round(median(interpolate(c, x) for c in curves), 4)) for x in xs]


def standard_model(data):
    """The standard LED in the same form as the datasheet models."""
    spec = data["standard_model"]
    sources = [m for m in data["models"] if m["id"] in spec["from"]]
    if len(sources) != len(spec["from"]):
        sys.exit("standard_model: unknown model in 'from'")
    currents = grid(*spec["grid_current_mA"])
    temps = grid(*spec["grid_temp_C"])
    return {
        "id": spec["id"],
        "name": spec["name"],
        "test_current_mA": spec["test_current_mA"],
        "max_dc_current_mA": spec["max_dc_current_mA"],
        "rth_junction_to_pad_C_per_W": median(m["rth_junction_to_pad_C_per_W"] for m in sources),
        "output_vs_current": {
            "source": "median of " + ", ".join(spec["from"]),
            "points_mA_rel": median_curve([m["output_vs_current"]["points_mA_rel"] for m in sources], currents)},
        "output_vs_junction_temp": {
            "source": "median, as above",
            "points_C_rel": median_curve([m["output_vs_junction_temp"]["points_C_rel"] for m in sources], temps)},
        "vf_vs_current": {
            "source": "median, as above",
            "points_mA_V": median_curve([m["vf_vs_current"]["points_mA_V"] for m in sources], currents)},
        "note": spec["purpose"],
    }


def channel_map(board, known):
    """default_channel_leds() from the board's LED map."""
    out = [
        "// The LEDs in each channel's string, as built: " + os.path.relpath(BOARD, os.path.join(HERE, "..", "..")).replace(os.sep, "/"),
        "// Other channel counts have no LED map and get one standard LED.",
        "std::vector<LedGroup> default_channel_leds(uint8_t channel, uint8_t num_channels) {",
        f"    if (num_channels != {board['num_channels']}) {{",
        "        return {{STANDARD_LED_MODEL, 1}};",
        "    }",
        "    switch (channel) {",
    ]
    for index, ch in enumerate(board["channels"]):
        if ch["channel"] != index + 1:
            sys.exit(f"channels.json: channel {ch['channel']} out of order")
        for group in ch["leds"]:
            if group["model"] not in known or not 1 <= group["count"] <= 100:
                sys.exit(f"channels.json: channel {ch['channel']}: bad LED {group}")
        groups = ", ".join(f"{{\"{g['model']}\", {g['count']}}}" for g in ch["leds"])
        out.append(f"        case {index}: return {{{groups}}};  // {ch['name']}")
    out += [
        "        default: return {{STANDARD_LED_MODEL, 1}};",
        "    }",
        "}",
        "",
    ]
    return out


def flt(value):
    """C++ float literal: 4 significant digits, always with a decimal point (25 -> 25.0f)."""
    text = f"{value:.4g}"
    if "." not in text and "e" not in text:
        text += ".0"
    return text + "f"


def fmt(points, scale_x=1.0):
    return "{" + ", ".join(f"{{{flt(x * scale_x)}, {flt(y)}}}" for x, y in points) + "}"


def main():
    data = json.load(open(SOURCE, encoding="utf-8"))
    board = json.load(open(BOARD, encoding="utf-8"))
    models = data["models"] + [standard_model(data)]
    out = [
        "// Generated by tools/led_curves/gen_led_models.py from tools/led_curves/lumileds_curves.json",
        "// and boards/ledbrick-plus/channels.json.",
        "// Do not edit; change the JSON or the generator and run it again.",
        "#include \"led_dimming.h\"",
        "",
        "namespace ledbrick {",
        "",
        "namespace {",
        "",
        "LedModel make_model(const char* id, const char* name, float test_current_a, float max_current_a,",
        "                    float rth_c_per_w, std::vector<CurvePoint> output_vs_current,",
        "                    std::vector<CurvePoint> output_vs_temp, std::vector<CurvePoint> vf_vs_current) {",
        "    LedModel model;",
        "    model.id = id;",
        "    model.name = name;",
        "    model.test_current_a = test_current_a;",
        "    model.max_current_a = max_current_a;",
        "    model.output_vs_current = std::move(output_vs_current);",
        "    model.curve_temp_c = 85.0f;",
        "    model.output_vs_temp = std::move(output_vs_temp);",
        "    model.rth_c_per_w = rth_c_per_w;",
        "    model.vf_vs_current = std::move(vf_vs_current);",
        "    return model;",
        "}",
        "",
        "}  // namespace",
        "",
        "// " + data["conventions"],
        "const std::vector<LedModel>& builtin_led_models() {",
        "    static const std::vector<LedModel> models = {",
    ]
    total = 0
    for m in models:
        cur = thin([tuple(p) for p in m["output_vs_current"]["points_mA_rel"]], True)
        temp = thin([tuple(p) for p in m["output_vs_junction_temp"]["points_C_rel"]], True)
        vf = thin([tuple(p) for p in m["vf_vs_current"]["points_mA_V"]], False)
        total += len(cur) + len(temp) + len(vf)
        out += [
            f"        // {m['name']}" + (f" ({m['part_number']}, BOM {m['bom_code']})" if "part_number" in m else ""),
            f"        //   current: {m['output_vs_current']['source']}",
            f"        //   temperature: {m['output_vs_junction_temp']['source']}",
            f"        //   Vf: {m['vf_vs_current']['source']}",
        ]
        if "note" in m:
            out.append(f"        //   {m['note']}")
        out += [
            f"        make_model(\"{m['id']}\", \"{m['name']}\", {flt(m['test_current_mA'] / 1000)}, "
            f"{flt(m['max_dc_current_mA'] / 1000)}, {flt(m['rth_junction_to_pad_C_per_W'])},",
            f"                   {fmt(cur, 0.001)},",
            f"                   {fmt(temp)},",
            f"                   {fmt(vf, 0.001)}),",
        ]
        print(f"{m['id']:26} current {len(m['output_vs_current']['points_mA_rel'])}->{len(cur)}  "
              f"temp {len(m['output_vs_junction_temp']['points_C_rel'])}->{len(temp)}  "
              f"vf {len(m['vf_vs_current']['points_mA_V'])}->{len(vf)}")
    out += [
        "    };",
        "    return models;",
        "}",
        "",
    ]
    out += channel_map(board, {m["id"] for m in models})
    out += [
        "}  // namespace ledbrick",
        "",
    ]
    with open(TARGET, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    print(f"wrote {os.path.normpath(TARGET)}: {total} points ({total * 8} bytes of curve data)")


if __name__ == "__main__":
    sys.exit(main())
