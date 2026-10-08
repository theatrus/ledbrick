#!/usr/bin/env python3
"""Back up, copy and check LEDBrick settings over the web API.

    python3 tools/board_config.py backup HOST DIR    save schedule.json and led_models.json
    python3 tools/board_config.py apply HOST DIR     post led_models.json, then schedule.json
    python3 tools/board_config.py models HOST FILE   post custom LED models ({"led_models": [...]})
    python3 tools/board_config.py check HOST         show each channel's dimming and LEDs

The schedule file holds the schedule, channel settings (names, limits, dimming, LEDs),
location, moon and temperature settings. The models file holds the custom LED models,
which have their own record on the board; they are rarely needed. apply posts the models
first, since channels name them, and skips an empty list. The PWM scale is not copied.

Set LEDBRICK_USER and LEDBRICK_PASSWORD if the board's web server has a password.
Standard library only.
"""
import base64
import json
import os
import sys
import urllib.error
import urllib.request


def request(host, method, path, body=None):
    url = host if host.startswith("http") else "http://" + host
    data = None if body is None else json.dumps(body).encode()
    headers = {"Content-Type": "application/json"} if data else {}
    user, password = os.environ.get("LEDBRICK_USER"), os.environ.get("LEDBRICK_PASSWORD")
    if user and password:
        headers["Authorization"] = "Basic " + base64.b64encode(f"{user}:{password}".encode()).decode()
    req = urllib.request.Request(url.rstrip("/") + path, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        text = e.read().decode(errors="replace")
        try:
            text = json.loads(text).get("error", text)
        except ValueError:
            pass
        sys.exit(f"{method} {path}: HTTP {e.code}: {text}")
    except urllib.error.URLError as e:
        sys.exit(f"{method} {path}: {e.reason}")


def write_json(path, data):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=1)
        f.write("\n")


def read_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def backup(host, folder):
    os.makedirs(folder, exist_ok=True)
    schedule = request(host, "GET", "/api/schedule")
    schedule.pop("current_time_minutes", None)  # changes every minute
    write_json(os.path.join(folder, "schedule.json"), schedule)
    models = request(host, "GET", "/api/led_models?custom=true")
    write_json(os.path.join(folder, "led_models.json"), models)
    print(f"saved {len(schedule.get('schedule_points', []))} schedule points and "
          f"{len(models.get('led_models', []))} custom LED models to {folder}")


def post_models(host, path):
    models = read_json(path)
    if not isinstance(models.get("led_models"), list):
        sys.exit(f"{path}: expected {{\"led_models\": [...]}}")
    result = request(host, "POST", "/api/led_models", models)
    print(f"custom LED models: {result.get('custom_models')}")


def apply(host, folder):
    models = os.path.join(folder, "led_models.json")
    schedule = os.path.join(folder, "schedule.json")
    if not os.path.exists(models) and not os.path.exists(schedule):
        sys.exit(f"{folder}: no schedule.json or led_models.json")
    # An empty list would only rewrite the board's model record; none are normally needed
    if os.path.exists(models) and read_json(models).get("led_models"):
        post_models(host, models)
    if os.path.exists(schedule):
        result = request(host, "POST", "/api/schedule", read_json(schedule))
        print(f"schedule: {result.get('points')} points")


def check(host):
    status = request(host, "GET", "/api/status")
    schedule = request(host, "GET", "/api/schedule")
    listing = request(host, "GET", "/api/led_models").get("models", [])
    known = {m["id"]: m for m in listing}
    custom = [m["id"] for m in listing if m.get("custom")]
    print(f"{host}: scheduler {'on' if status.get('enabled') else 'off'}, PWM scale {status.get('pwm_scale')}%, "
          f"LED temperature {status.get('led_temp_c')} C")
    print(f"LED models: {len(listing)}, custom: {', '.join(custom) or 'none'}")
    missing = 0
    for i, ch in enumerate(schedule.get("channel_configs", [])):
        dim = ch.get("dimming", {})
        leds = []
        for group in dim.get("leds", []):
            name = group["model"]
            if name not in known:
                name += " (MISSING: uses standard_led)"
                missing += 1
            leds.append(f"{group['count']} x {name}")
        source = "default" if dim.get("leds_default") else "set"
        print(f"  ch{i + 1} {ch.get('name', ''):10} {dim.get('mode', 'manual'):6} max {ch.get('max_current')} A  "
              f"{source}: {', '.join(leds) or 'none'}")
    if missing:
        print("Post the missing models (models HOST FILE) to restore their curves.")


def main(argv):
    if len(argv) < 3 or argv[1] not in ("backup", "apply", "models", "check"):
        sys.exit(__doc__)
    command, host = argv[1], argv[2]
    if command == "check":
        check(host)
    elif len(argv) < 4:
        sys.exit(__doc__)
    elif command == "backup":
        backup(host, argv[3])
    elif command == "apply":
        apply(host, argv[3])
    else:
        post_models(host, argv[3])


if __name__ == "__main__":
    main(sys.argv)
