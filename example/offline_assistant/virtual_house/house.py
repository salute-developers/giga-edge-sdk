#!/usr/bin/env python3
"""Virtual house for the offline assistant demo: one process instead of a Home Assistant.

It keeps the state of a house, one of houses/*.json, and serves, on one port:
  /api/mcp/assist   the MCP endpoint in the dialect of the Home Assistant MCP Server (context snapshot, GetLiveContext,
                    the Hass* intents, a tool per script), so offline_assistant_demo --ha-url works with it unchanged;
  /                 the 3D page (web/dist);
  /api/scene, /api/events, /api/devices/<id>, /api/chat, /api/report, /api/reset, /api/house   what the page needs.

An assistant command after `--` is started as a child process, by the protocol of offline_assistant_demo: a request is
a line of its stdin, and of its stdout the page shows `>> request:`, `>> answer:`, `>> listening` and `>> report`; the
line `offline_assistant_demo: model loaded...` means it is ready. Standard library only, Python 3.9+.

Usage: house.py [--host 127.0.0.1] [--port 8124] [--house lake_house] [-- assistant command ...]
"""

import argparse
import json
import math
import mimetypes
import os
import queue
import re
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
COLOURS = json.loads((HERE / "colours.json").read_text(encoding="utf-8"))
CONTEXT_URI = "homeassistant://assist/context-snapshot"
CONTEXT_HEAD = "Live Context: An overview of the areas and the devices in this smart home:\n"
READ_TOOL = "homeassistant__GetLiveContext"
READY = "offline_assistant_demo: model loaded"

# A binary sensor class -> the model category and the parameter of the page, as the smart_home_ha adapter maps them.
BINARY_SENSORS = {
    "door": ("sensor_door", "doorcontact_state"),
    "motion": ("sensor_pir", "pir"),
    "smoke": ("sensor_smoke", "smoke_state"),
    "gas": ("sensor_gas", "gas_leak_state"),
    "moisture": ("sensor_water_leak", "water_leak_state"),
}
COVERS = {"blind": "window_blind", "shade": "window_blind", "garage": "gate", "gate": "gate"}
CATEGORIES = {
    "light": "light", "switch": "socket", "climate": "hvac_heater", "fan": "hvac_fan",
    "humidifier": "hvac_humidifier", "valve": "valve", "media_player": "tv", "sensor": "sensor_temp",
    "vacuum": "vacuum_cleaner",
}

PERCENT = {"data_type": "INTEGER", "range": {"min": 0, "max": 100, "step": 1}, "unit": "%"}
READING = {"data_type": "BOOL", "read_only": True}
# What a value may be. read_only: a reading the assistant cannot set; on the page it is set by hand, as the world does.
PARAMS = {
    "on_off": {"data_type": "BOOL"},
    "light_brightness": {**PERCENT, "range": {"min": 1, "max": 100, "step": 1}},
    "light_colour": {"data_type": "STRING"},
    "battery_percentage": {**PERCENT, "read_only": True},
    # Percent of the scale the model was trained on: 0 is the warmest, 2700 K, 100 the coldest, 6500 K.
    "light_colour_temp": PERCENT,
    "open_percentage": PERCENT,
    "open_set": {"data_type": "ENUM", "values": ["open", "close"]},
    "hvac_temp_set": {"data_type": "FLOAT", "range": {"min": 5, "max": 35, "step": 0.5}, "unit": "°C"},
    "temperature": {"data_type": "FLOAT", "range": {"min": -30, "max": 50, "step": 0.5}, "unit": "°C", "read_only": True},
    "humidity": {**PERCENT, "read_only": True},
    "hvac_humidity_set": {**PERCENT, "read_only": True},
    "volume_int": PERCENT,
    "mute": {"data_type": "BOOL"},
    **{parameter: READING for _, parameter in BINARY_SENSORS.values()},
}
# What the intents of a domain read and write: every device of the domain has it in its values.
DOMAIN_PARAMS = {"light": ("on_off", "light_brightness", "light_colour_temp", "light_colour")}

# What a house file calls a scenario: Home Assistant lists a scene among the entities and makes a script a tool.
SCENARIOS = ("scene", "script")
TARGET = {"name": {"type": "string"}, "area": {"type": "string"}, "domain": {"type": "array", "items": {"type": "string"}}}
NUMBER = {"type": "number"}
# The tools of the Home Assistant MCP Server the adapter uses: name -> (description, arguments besides the target).
TOOLS = {
    READ_TOOL: ("Current state of the devices", {}),
    "intent__HassTurnOn": ("Turns on or opens a device", {}),
    "intent__HassTurnOff": ("Turns off or closes a device", {}),
    "intent__HassSetPosition": ("Sets the position of a cover or a valve", {"position": NUMBER}),
    "intent__HassStopMoving": ("Stops a moving cover or valve", {}),
    "light__HassLightSet": ("Sets the brightness (percent), colour or colour temperature (kelvin) of a light",
                            {"brightness": NUMBER, "color": {"type": "string"}, "temperature": NUMBER}),
    "vacuum__HassVacuumStart": ("Starts a vacuum", {}),
    "vacuum__HassVacuumReturnToBase": ("Returns a vacuum to base", {}),
    "climate__HassClimateSetTemperature": ("Sets the target temperature", {"temperature": NUMBER}),
    "media_player__HassSetVolume": ("Sets the volume (percent)", {"volume_level": NUMBER}),
    "media_player__HassMediaPlayerMute": ("Mutes a media player", {}),
    "media_player__HassMediaPlayerUnmute": ("Unmutes a media player", {}),
}


def category(spec):
    """The device category of the model for an entity."""
    domain, device_class = spec["domain"], spec.get("device_class", "")
    if domain == "binary_sensor":
        return BINARY_SENSORS[device_class][0]
    if domain == "cover":
        return COVERS.get(device_class, "curtain")
    return CATEGORIES[domain]


def check(parameter, value):
    """The value as stored, or ValueError when the parameter cannot take it."""
    rule = PARAMS[parameter]
    kind = rule["data_type"]
    if parameter == "light_colour":
        if value is None or (isinstance(value, str) and re.fullmatch(r"[0-9]{1,3}(;[0-9]{1,3}){2}", value)
                             and all(int(channel) <= 255 for channel in value.split(";"))):
            return value
    elif kind == "BOOL":
        if isinstance(value, bool):
            return value
    elif kind == "ENUM":
        if value in rule["values"]:
            return value
    elif isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value):
        if kind == "INTEGER":
            if value != int(value):
                raise ValueError(f"{parameter}: an integer is expected")
            value = int(value)
        if rule["range"]["min"] <= value <= rule["range"]["max"]:
            return value
        raise ValueError(f"{parameter}: {value} is out of {rule['range']['min']}..{rule['range']['max']}")
    raise ValueError(f"{parameter}: bad value {value!r}")


def yaml_scalar(value):
    """A scalar the way Home Assistant prints it: numbers bare, text quoted only when YAML would misread it."""
    if value is None:
        return ""
    if isinstance(value, list):
        return "[" + ", ".join(map(str, value)) + "]"  # a list of numbers, as rgb_color
    if isinstance(value, float):
        return repr(value)
    text = str(value)
    plain = (
        text == text.strip()
        and not re.search(r"[:#\[\]{},&*!|>'\"%@`\n]", text)
        and not re.match(r"[-?]", text)
        and not re.fullmatch(r"[-+.\d]+|on|off|yes|no|true|false|null|~", text, re.IGNORECASE)
    )
    return text if plain else "'" + text.replace("'", "''") + "'"


def entity(spec, values):
    """The state and the attributes of an entity as GetLiveContext reports them."""
    domain, device_class = spec["domain"], spec.get("device_class")
    on = "on" if values.get("on_off") else "off"
    if domain == "vacuum":
        return "cleaning" if values["on_off"] else "docked", {}
    if domain == "light":
        # Home Assistant reports no brightness for a light that is off.
        level = values["light_brightness"]
        if not values["on_off"]:
            return on, {"brightness": None}
        # The colour, or the warmth of white light, lets the assistant read and copy them; a light that is off has
        # neither.
        colour = values["light_colour"]
        tint = ({"rgb_color": [int(channel) for channel in colour.split(";")]} if colour
                else {"color_temp_kelvin": round(2700 + values["light_colour_temp"] * (6500 - 2700) / 100)})
        return on, {"brightness": str(round(level * 255 / 100)), "brightness_pct": str(level), **tint}
    if domain == "climate":
        return spec.get("hvac_mode", "heat") if values["on_off"] else "off", {
            "current_temperature": float(values["temperature"]),
            "temperature_unit": "°C",
            "temperature": float(values["hvac_temp_set"]),
        }
    if domain == "humidifier":
        return on, {"humidity": values["hvac_humidity_set"]}
    if domain == "cover":
        attributes = {"current_position": str(values["open_percentage"])}
        if device_class:
            attributes["device_class"] = device_class
        return "open" if values["open_percentage"] else "closed", attributes
    if domain == "valve":
        return "open" if values["open_set"] == "open" else "closed", {}
    if domain == "media_player":
        return on, {"volume_level": values["volume_int"] / 100, "device_class": device_class}
    if domain == "sensor":
        unit = "°C" if device_class == "temperature" else "%"
        return str(values[device_class]), {"unit_of_measurement": unit, "device_class": device_class}
    if domain == "binary_sensor":
        return "on" if values[BINARY_SENSORS[device_class][1]] else "off", {"device_class": device_class}
    return on, {}  # switch, fan


def number(arguments, key, low, high):
    value = arguments.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not low <= value <= high:
        raise ValueError(f"{key}: a number {low}..{high} is expected")
    return value


def percent(arguments, key):
    return round(number(arguments, key, 0, 100))


def whitened(update):
    """An update of values as stored: a light set to a colour temperature loses its colour, whoever sets it."""
    return {"light_colour": None, **update} if "light_colour_temp" in update and "light_colour" not in update else update


def powered(values, update):
    """An update as stored: a setting changed on a device that is off turns it on, as the model expects. A reading set
    by hand or an explicit on_off does not."""
    if "on_off" in values and "on_off" not in update and any(not PARAMS[key].get("read_only") for key in update):
        return {**update, "on_off": True}
    return update


def intent(tool, spec, values, arguments):
    """The values an intent changes on an entity; ValueError when it does not apply."""
    domain, name = spec["domain"], tool.split("__")[-1]
    if domain == "vacuum":
        if name in ("HassVacuumStart", "HassVacuumReturnToBase"):
            return {"on_off": name == "HassVacuumStart"}
        raise ValueError(f"{name} does not apply to {spec['name']}")
    if name in ("HassTurnOn", "HassTurnOff"):
        on = name == "HassTurnOn"
        if domain == "cover":
            return {"open_percentage": 100 if on else 0}
        if domain == "valve":
            return {"open_set": "open" if on else "close"}
        if "on_off" in values:
            return {"on_off": on}
    elif name == "HassSetPosition" and domain == "cover":
        return {"open_percentage": percent(arguments, "position")}
    elif name == "HassSetPosition" and domain == "valve":
        return {"open_set": "open" if percent(arguments, "position") else "close"}
    elif name == "HassStopMoving" and domain in ("cover", "valve"):
        return {}  # the virtual drives arrive at once: nothing is moving
    elif name == "HassLightSet" and domain == "light":
        changes = {}
        if "color" in arguments and "temperature" in arguments:
            raise ValueError("color and temperature cannot be combined")
        if "brightness" in arguments:
            level = percent(arguments, "brightness")
            changes = {"on_off": level > 0, **({"light_brightness": level} if level else {})}
        if "temperature" in arguments:
            kelvin = number(arguments, "temperature", 1000, 10000)
            changes["light_colour_temp"] = max(0, min(100, round((kelvin - 2700) * 100 / (6500 - 2700))))
        if "color" in arguments:
            colour = arguments["color"]
            rgb = COLOURS.get(colour.replace(" ", "").lower()) if isinstance(colour, str) else None
            if rgb is None:
                raise ValueError("Unknown color")
            changes["light_colour"] = ";".join(map(str, rgb))
        if changes:
            return changes
    elif name == "HassClimateSetTemperature" and domain == "climate":
        return {"hvac_temp_set": check("hvac_temp_set", arguments.get("temperature"))}
    elif name == "HassSetVolume" and domain == "media_player":
        return {"volume_int": percent(arguments, "volume_level")}
    elif name in ("HassMediaPlayerMute", "HassMediaPlayerUnmute") and domain == "media_player":
        return {"mute": name == "HassMediaPlayerMute"}
    raise ValueError(f"{name} does not apply to {spec['name']}")


def targets(entities, arguments):
    """Entities by name, area and domain, as Home Assistant targets them: case-insensitive, a slot left out fits all."""
    name = str(arguments.get("name", "")).casefold()
    area = str(arguments.get("area", "")).casefold()
    domains = arguments.get("domain") or []
    domains = [domains] if isinstance(domains, str) else domains
    return [
        entity
        for entity in entities
        if (not name or entity["name"].casefold() == name)
        and (not area or entity["area"].casefold() == area)
        and (not domains or entity["domain"] in domains)
    ]


def load(path):
    """A house of houses/: its name, rooms, devices by the id the page shows, scenarios and where the assistant stands."""
    spec = json.loads(path.read_text(encoding="utf-8"))
    devices, counters = {}, {}
    for device in spec["devices"]:
        # The same numbering as the aliases of the adapter: per category, in the order of the context.
        kind = category(device)
        device = {**device, "device_id": f"{kind}_{counters.get(kind, 0)}", "type": kind}
        counters[kind] = counters.get(kind, 0) + 1
        for parameter, value in device["values"].items():
            check(parameter, value)
        missing = [parameter for parameter in DOMAIN_PARAMS.get(device["domain"], ()) if parameter not in device["values"]]
        if missing:
            raise ValueError(f"{path.name}: {device['name']} needs {', '.join(missing)} in its values")
        devices[device["device_id"]] = device
    scenarios = []
    for scenario in spec.get("scenarios", []):
        if scenario["domain"] not in SCENARIOS:
            raise ValueError(f"{path.name}: {scenario['name']} is neither a scene nor a script")
        for step in scenario["set"]:
            found = targets(devices.values(), step)
            if not found:
                raise ValueError(f"{path.name}: {scenario['name']} sets no device with {step}")
            for device in found:
                for parameter, value in step["values"].items():
                    if parameter not in device["values"]:
                        raise ValueError(f"{path.name}: {scenario['name']} sets {parameter}, which {device['name']} lacks")
                    check(parameter, value)
        scenarios.append({**scenario, "area": ""})  # Home Assistant gives a scene or a script no area by itself
    # Where the assistant stands: its room, which is current_room of its graph, and the place of its speaker on the page.
    place = spec.get("assistant", {})
    position = place.get("pos")
    if place and (place.get("area") not in {room["name"] for room in spec["rooms"]} or not isinstance(position, list)
                  or len(position) != 3 or not all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in position)):
        raise ValueError(f"{path.name}: the assistant needs the area of one of its rooms and a pos [x, y, z]")
    place = {key: place[key] for key in ("area", "pos")} if place else {}
    return {"name": spec["name"], "rooms": spec["rooms"], "devices": devices, "scenarios": scenarios, "place": place}


class House:
    def __init__(self, directory, house):
        # Every house is read at the start: a broken file fails here, not at a switch.
        self.houses = {path.stem: load(path) for path in sorted(Path(directory).glob("*.json"))}
        self.lock = threading.Lock()
        self.listeners = frozenset()  # replaced, never changed: publish needs no lock
        self.assistant = {"mode": None, "ready": False}
        self.switch(house)

    def switch(self, house):
        """Makes a house of houses/ the current one, with its initial values."""
        if house not in self.houses:
            raise ValueError(f"no house {house!r}: there are {', '.join(self.houses)}")
        with self.lock:
            self.id = house
            self.name, self.rooms, self.devices = (self.houses[house][key] for key in ("name", "rooms", "devices"))
            self.scenarios, self.place = self.houses[house]["scenarios"], self.houses[house]["place"]
            self.values = {key: dict(device["values"]) for key, device in self.devices.items()}
            event = {"type": "house", **self.describe()}
        self.publish(event)

    def snapshot(self, keys=None):
        """A copy of the values. Call with the lock held."""
        return {key: dict(self.values[key]) for key in (self.values if keys is None else keys)}

    def reset(self):
        with self.lock:
            self.values = {key: dict(device["values"]) for key, device in self.devices.items()}
            snapshot = self.snapshot()
        self.publish({"type": "values", "values": snapshot})

    def publish(self, event):
        for listener in self.listeners:
            listener.put(event)

    def subscribe(self):
        """A queue of events that starts with the current house."""
        listener = queue.Queue()
        with self.lock:
            listener.put({"type": "house", **self.describe()})
            self.listeners = self.listeners | {listener}
        return listener

    def unsubscribe(self, listener):
        with self.lock:
            self.listeners = self.listeners - {listener}

    def describe(self):
        """Everything the page draws: the current house, the list of houses, the assistant. Call with the lock held."""
        devices = [
            {
                **{key: value for key, value in device.items() if key not in ("values", "area")},
                "kind": device.get("kind", device["type"]),
                "room": device["area"],
                "parameters": [
                    {"name": parameter, "value": value, **PARAMS[parameter]}
                    for parameter, value in self.values[key].items()
                ],
            }
            for key, device in self.devices.items()
        ]
        houses = [{"id": key, "name": house["name"], "devices": len(house["devices"])} for key, house in self.houses.items()]
        scenarios = [{"name": scenario["name"], "domain": scenario["domain"]} for scenario in self.scenarios]
        return {"id": self.id, "name": self.name, "rooms": self.rooms, "devices": devices, "scenarios": scenarios,
                "houses": houses, "assistant": {**self.assistant, **self.place}}

    def scene(self):
        with self.lock:
            return self.describe()

    def entities(self):
        """What the context lists: the devices and the scenes. A script is not there, it is a tool."""
        return [*self.devices.values(), *(scenario for scenario in self.scenarios if scenario["domain"] == "scene")]

    def script_tools(self):
        """The tools of the scripts, as Home Assistant describes them: named by the object id, the name in the title."""
        return [
            {"name": f"script__{scenario['id']}", "title": scenario["name"], "description": "",
             "inputSchema": {"type": "object", "properties": {}}}
            for scenario in self.scenarios if scenario["domain"] == "script"
        ]

    def context(self, devices=None):
        """The house, or some of its entities, as the text of GetLiveContext. Call with the lock held."""
        lines = []
        for device in self.entities() if devices is None else devices:
            if device["domain"] == "scene":
                # Home Assistant puts the time of the last activation there; the adapter does not read it.
                lines += [f"- names: {yaml_scalar(device['name'])}", "  domain: scene", "  state: unknown"]
                continue
            state, attributes = entity(device, self.values[device["device_id"]])
            lines += [
                f"- names: {yaml_scalar(device['name'])}",
                f"  domain: {device['domain']}",
                f"  state: {yaml_scalar(state)}",
                f"  areas: {yaml_scalar(device['area'])}",
            ]
            if attributes:
                lines.append("  attributes:")
                lines += [f"    {key}: {yaml_scalar(value)}".rstrip() for key, value in attributes.items()]
        return CONTEXT_HEAD + "".join(line + "\n" for line in lines)

    def steps(self, tool, target, arguments):
        """What a call does to a target: the devices and their new values. ValueError when it does not apply."""
        if target["domain"] not in SCENARIOS:
            return [(target, intent(tool, target, self.values[target["device_id"]], arguments))]
        name = tool.split("__")[-1]
        if tool.startswith("script__") or name == "HassTurnOn":
            return [(device, step["values"]) for step in target["set"] for device in targets(self.devices.values(), step)]
        if target["domain"] == "script" and name == "HassTurnOff":
            return []  # stopped: a virtual script is over at once, so nothing is running
        # A scene is only activated: Home Assistant has no other service for it.
        raise ValueError(f"{name} does not apply to {target['name']}")

    def call(self, tool, arguments):
        """A tools/call: the reply as Home Assistant words it. ValueError becomes an MCP tool error."""
        changed = []
        snapshot = {}
        # The page gets a name and an object whatever the call brought.
        event = {"type": "call", "tool": "" if tool is None else str(tool), "arguments": arguments if isinstance(arguments, dict) else {},
                 "changed": changed, "is_error": False,
                 "background": tool == READ_TOOL and isinstance(arguments, dict)
                 and not any(arguments.get(key) for key in ("name", "area", "domain"))}
        try:
            if not isinstance(tool, str) or not isinstance(arguments, dict):
                raise ValueError("a tool name and an arguments object are expected")
            with self.lock:
                # A script is called by its own tool, without a target; everything else names its targets.
                script = next((s for s in self.scenarios if s["domain"] == "script" and tool == f"script__{s['id']}"), None)
                # A script is not in the context, yet an intent finds it by its name, as any entity.
                pool = self.entities() if tool == READ_TOOL else [*self.devices.values(), *self.scenarios]
                found = [script] if script else targets(pool, arguments)
                # The page shows a script by its name: its call has no arguments.
                if script:
                    event["title"] = script["name"]
                if tool == READ_TOOL:
                    if not found:
                        event.update(is_error=True, error="No entities matched")
                        return {"success": False, "error": "No entities matched"}
                    return {"result": self.context(found)}
                if not script and tool not in TOOLS:
                    raise ValueError(f"unknown tool {tool}")
                if not found:
                    raise ValueError(f"no device matches {json.dumps(arguments, ensure_ascii=False)}")
                # All or nothing: an intent that does not fit one of its targets changes none of them.
                updates = [self.steps(tool, target, arguments) for target in found]
                success = []
                if arguments.get("area"):
                    area = found[0]["area"]
                    success.append({"name": area, "type": "area", "id": area.casefold()})
                for target, steps in zip(found, updates):
                    for device, update in steps:
                        values = self.values[device["device_id"]]
                        update = powered(values, whitened(update))
                        if any(values[key] != value for key, value in update.items()):
                            values.update(update)
                            if device["device_id"] not in changed:
                                changed.append(device["device_id"])
                    entity_id = f"{target['domain']}.{target['id' if target['domain'] in SCENARIOS else 'device_id']}"
                    success.append({"name": target["name"], "type": "entity", "id": entity_id})
                snapshot = self.snapshot(changed)
            if script:
                return {"result": {}}  # the response of a script that returns nothing
            return {"speech": {}, "response_type": "action_done", "data": {"success": success, "failed": []}}
        except Exception as error:  # whatever fails, the assistant gets a tool error and the page a failed call
            message = str(error) if isinstance(error, (ValueError, TypeError, AttributeError)) else f"{type(error).__name__}: {error}"
            event.update(is_error=True, error=message)
            raise ValueError(message) from error
        finally:
            self.publish(event)
            if changed:
                self.publish({"type": "values", "values": snapshot})

    def edit(self, device_id, parameter, value):
        """A change made by hand on the page."""
        with self.lock:
            if parameter not in self.values.get(device_id, {}):
                raise ValueError(f"{device_id} has no {parameter}")
            values = self.values[device_id]
            values.update(powered(values, whitened({parameter: check(parameter, value)})))
            snapshot = self.snapshot([device_id])
        self.publish({"type": "values", "values": snapshot})

    def set_assistant(self, **changes):
        self.assistant = {**self.assistant, **changes}
        self.publish({"type": "assistant", **self.assistant})


class Assistant:
    """The assistant process: requests go to its stdin, its stdout lines become events of the page."""

    def __init__(self, house, command, environment):
        self.house, self.command, self.environment = house, command, environment
        # offline_assistant_demo takes typed requests only with --text; otherwise a line just opens a voice session.
        self.voice = "--text" not in command
        self.lock = threading.Lock()
        self.exited = threading.Event()  # the assistant has exited by itself, with `code`
        self.code = None
        self.start()

    def start(self):
        self.house.set_assistant(mode="voice" if self.voice else "text", ready=False)
        self.process = subprocess.Popen(
            self.command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=self.environment, encoding="utf-8",
            bufsize=1,
        )
        threading.Thread(target=self.read, args=(self.process,), daemon=True).start()

    def read(self, process):
        for line in process.stdout:
            line = line.rstrip("\n")
            if line.startswith(">> answer: "):
                self.house.publish({"type": "answer", "text": line[len(">> answer: "):]})
            elif line.startswith(">> request: "):
                # A typed request is already on the page; a spoken one is known only from here.
                if self.voice:
                    self.house.publish({"type": "request", "text": line[len(">> request: "):]})
            elif line.startswith(">> listening"):
                if self.voice:
                    self.house.publish({"type": "listening"})
            elif line.startswith(">> report: "):
                self.house.publish({"type": "report", "path": line[len(">> report: "):]})
            elif line.startswith(">> report failed: "):
                self.house.publish({"type": "report", "error": line[len(">> report failed: "):]})
            else:
                if line.startswith(READY):
                    self.house.set_assistant(ready=True)
                print(line, flush=True)  # the rest of its output belongs to the terminal
        process.stdout.close()
        code = process.wait()
        if process is self.process:  # it was not stopped from here
            self.code = code
            self.exited.set()

    def restart(self):
        """A new house needs a new assistant: Home Assistant does not notify about changed entities, the assistant
        reads the house at its start, and its dialogue is about the old one."""
        with self.lock:
            self.stop()
            self.start()

    def send(self, text):
        text = " ".join(text.split())
        if not text and not self.voice:
            raise ValueError("an empty request")
        if text and self.voice:
            raise ValueError("typed requests need the assistant started with --text")
        self.write(text, {"type": "request", "text": text} if text else None)

    def report(self, comment):
        """Asks the assistant to save the scene of the house it sees, with what went wrong, in either mode."""
        self.write(" ".join(["/report", *comment.split()]))

    def write(self, line, event=None):
        """A line to the stdin of the running assistant, after its event for the page."""
        with self.lock:
            if self.process is None or self.process.poll() is not None:
                raise ValueError("the assistant is not running")
            if event:
                self.house.publish(event)
            self.process.stdin.write(line + "\n")
            self.process.stdin.flush()

    def stop(self):
        process, self.process = self.process, None
        if process:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            process.stdin.close()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    house = None
    assistant = None
    web = HERE / "web" / "dist"

    def log_message(self, *args):
        pass

    def reply(self, status, body=b"", content_type="application/json"):
        if not isinstance(body, bytes):
            body = json.dumps(body, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def body(self):
        data = self.rfile.read(int(self.headers.get("Content-Length") or 0))
        return json.loads(data) if data else {}

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/api/scene":
            self.reply(200, self.house.scene())
        elif path == "/api/events":
            self.events()
        elif path.startswith("/api/"):
            self.reply(404, {"error": "not found"})
        else:
            self.page(path)

    def do_POST(self):
        path = self.path.split("?")[0]
        try:
            body = self.body()
            if path == "/api/mcp/assist":
                self.mcp(body)
            elif path.startswith("/api/devices/"):
                self.house.edit(path[len("/api/devices/"):], body.get("param"), body.get("value"))
                self.reply(200, {})
            elif path in ("/api/chat", "/api/report"):
                if not self.assistant:
                    raise ValueError("the house runs without an assistant")
                if path == "/api/chat":
                    self.assistant.send(str(body.get("text", "")))
                else:
                    self.assistant.report(str(body.get("comment", "")))
                self.reply(200, {})
            elif path == "/api/reset":
                self.house.reset()
                self.reply(200, {})
            elif path == "/api/house":
                self.house.switch(body.get("id"))
                if self.assistant:
                    self.assistant.restart()
                self.reply(200, {})
            else:
                self.reply(404, {"error": "not found"})
        except (ValueError, TypeError, AttributeError) as error:  # TypeError, AttributeError: a body of a wrong shape
            self.reply(400, {"error": str(error)})

    def page(self, path):
        file = (self.web / (path.lstrip("/") or "index.html")).resolve()
        if self.web.resolve() not in file.parents or not file.is_file():
            hint = "The page is not built: run `npm ci && npm run build` in virtual_house/web."
            self.reply(404, (hint if path == "/" else "not found").encode(), "text/plain; charset=utf-8")
            return
        self.reply(200, file.read_bytes(), mimetypes.guess_type(file.name)[0] or "application/octet-stream")

    def events(self):
        """Server-sent events: the values of all devices first, then every change."""
        listener = self.house.subscribe()
        self.close_connection = True
        try:
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "close")
            self.end_headers()
            while True:
                try:
                    data = "data: " + json.dumps(listener.get(timeout=15), ensure_ascii=False)
                except queue.Empty:
                    data = ": keep-alive"
                self.wfile.write((data + "\n\n").encode())
                self.wfile.flush()
        except OSError:
            pass  # the page is closed
        finally:
            self.house.unsubscribe(listener)

    def mcp(self, message):
        """MCP Streamable HTTP, the stateless subset Home Assistant serves: one JSON reply to one POST."""
        if "id" not in message:
            self.reply(202)  # a notification
            return
        method, params = message.get("method"), message.get("params") or {}
        reply = {"jsonrpc": "2.0", "id": message["id"]}
        if method == "initialize":
            reply["result"] = {
                "protocolVersion": params.get("protocolVersion", "2025-06-18"),
                "capabilities": {"tools": {}, "resources": {}},
                "serverInfo": {"name": "virtual-house", "version": "1"},
            }
        elif method == "ping":
            reply["result"] = {}
        elif method == "tools/list":
            reply["result"] = {"tools": [
                {"name": name, "description": description,
                 "inputSchema": {"type": "object", "properties": {**TARGET, **extra}}}
                for name, (description, extra) in TOOLS.items()
            ] + self.house.script_tools()}
        elif method == "resources/list":
            reply["result"] = {"resources": [{"uri": CONTEXT_URI, "name": "Assist context", "mimeType": "text/plain"}]}
        elif method == "resources/templates/list":
            reply["result"] = {"resourceTemplates": []}
        elif method == "resources/read" and params.get("uri") == CONTEXT_URI:
            with self.house.lock:
                text = self.house.context()
            self.house.publish({"type": "call", "tool": "resources/read", "arguments": {"uri": CONTEXT_URI},
                                "changed": [], "is_error": False, "background": True})
            reply["result"] = {"contents": [{"uri": CONTEXT_URI, "mimeType": "text/plain", "text": text}]}
        elif method == "tools/call":
            try:
                result = self.house.call(params.get("name"), params.get("arguments", {}))
                text, failed = json.dumps(result, ensure_ascii=False), False
            except ValueError as error:
                text, failed = str(error), True
            reply["result"] = {"content": [{"type": "text", "text": text}], "isError": failed}
        else:
            reply["error"] = {"code": -32601, "message": f"unknown method {method}"}
        self.reply(200, reply)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8124)
    parser.add_argument("--house", default="lake_house", help="the house to start with: a file name of houses/")
    parser.add_argument("assistant", nargs="*", help="after --: the assistant command; the house stops when it exits")
    args = parser.parse_args()

    try:
        Handler.house = House(HERE / "houses", args.house)
    except ValueError as error:
        parser.error(str(error))
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    server.daemon_threads = True
    url = f"http://{args.host}:{server.server_port}"
    print(f"virtual house {args.house}: page {url}/, MCP {url}/api/mcp/assist", flush=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        if args.assistant:
            # The virtual house takes any token; offline_assistant_demo refuses to start without one.
            environment = {"GIGA_EDGE_HA_TOKEN": "virtual-house", **os.environ}
            Handler.assistant = Assistant(Handler.house, args.assistant, environment)
            Handler.assistant.exited.wait()
            sys.exit(Handler.assistant.code)
        threading.Event().wait()
    except KeyboardInterrupt:
        pass
    finally:
        if Handler.assistant:
            Handler.assistant.stop()


if __name__ == "__main__":
    main()
