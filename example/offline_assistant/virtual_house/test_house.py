"""Checks of the virtual house. Run from this directory: python3 -m unittest test_house

The expected replies repeat the ones recorded from a live Home Assistant in
giga_edge/modules/smart_home_ha/tests/live_house.cpp.
"""

import json
import os
import queue
import sys
import tempfile
import threading
import unittest
from unittest import mock
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from pathlib import Path

import house

KITCHEN_LIGHT = {"name": "Свет на кухне", "area": "Кухня"}
# What offline_assistant_demo --text does with its stdin and stdout; «выход» makes it exit by itself, and a report
# without a comment fails.
STUB = f"""import sys
print('{house.READY}', flush=True)
for line in sys.stdin:
    if line.strip() == 'выход':
        sys.exit(7)
    if line.startswith('/report'):
        comment = line[len('/report'):].strip()
        print('>> report: bug_reports/' + comment + '.json' if comment else '>> report failed: no scene', flush=True)
        continue
    print('>> answer: ok ' + line.strip(), flush=True)
"""


class HouseTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.house = house.Handler.house = house.House(Path(house.__file__).with_name("houses"), "lake_house")
        cls.server = ThreadingHTTPServer(("127.0.0.1", int(os.environ.get("VIRTUAL_HOUSE_TEST_PORT", "0"))), house.Handler)
        cls.server.daemon_threads = True
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.url = f"http://127.0.0.1:{cls.server.server_port}"
        cls.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))  # a proxy cannot reach 127.0.0.1

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def setUp(self):
        self.house.switch("lake_house")

    def post(self, path, body):
        request = urllib.request.Request(self.url + path, json.dumps(body).encode(), {"Content-Type": "application/json"})
        try:
            with self.opener.open(request) as reply:
                return reply.status, json.loads(reply.read() or b"{}")
        except urllib.error.HTTPError as error:
            return error.code, json.loads(error.read())

    def mcp(self, method, **params):
        status, reply = self.post("/api/mcp/assist", {"jsonrpc": "2.0", "id": 1, "method": method, "params": params})
        self.assertEqual(status, 200)
        return reply["result"]

    def tool(self, tool, /, **arguments):
        """The reply of a tool: its JSON, or the error text of a failed call."""
        result = self.mcp("tools/call", name=tool, arguments=arguments)
        text = result["content"][0]["text"]
        return text if result["isError"] else json.loads(text)

    def read(self, /, **target):
        return self.tool(house.READ_TOOL, **target)["result"]

    def test_context_lists_every_device_as_a_distinct_target(self):
        self.assertEqual(self.mcp("initialize", protocolVersion="2025-06-18")["protocolVersion"], "2025-06-18")
        self.assertEqual({tool["name"] for tool in self.mcp("tools/list")["tools"]}, {*house.TOOLS, "script__good_morning"})
        self.assertEqual(self.mcp("resources/list")["resources"][0]["uri"], house.CONTEXT_URI)
        self.assertEqual({key: len(value["devices"]) for key, value in self.house.houses.items()}, {"apartment": 17, "lake_house": 40})
        for name in self.house.houses:
            with self.subTest(house=name):
                self.house.switch(name)
                text = self.mcp("resources/read", uri=house.CONTEXT_URI)["contents"][0]["text"]
                self.assertTrue(text.startswith(house.CONTEXT_HEAD))
                devices = self.house.devices.values()
                # Besides the devices the context lists the scene of the house, and not its script.
                self.assertEqual([scenario["domain"] for scenario in self.house.scenarios], ["scene", "script"])
                self.assertEqual(text.count("- names: "), len(devices) + 1)
                # The adapter leaves out devices that their name, area and domain do not tell apart.
                targets = [(device["domain"], device["name"].casefold()) for device in devices]
                self.assertEqual(len(set(targets)), len(targets))
                # current_room of the demo model (model/function_calling/model/config.yaml) must be a room of the house.
                self.assertIn("Гостиная", {room["name"] for room in self.house.rooms})
                self.assertLessEqual({device["area"] for device in devices}, {room["name"] for room in self.house.rooms})

    def test_read_is_worded_as_home_assistant_does(self):
        self.assertEqual(
            self.read(**KITCHEN_LIGHT, domain="light"),
            house.CONTEXT_HEAD
            + "- names: Свет на кухне\n  domain: light\n  state: 'on'\n  areas: Кухня\n  attributes:\n"
            + "    brightness: '178'\n    brightness_pct: '70'\n    color_temp_kelvin: '4980'\n",
        )
        self.assertIn("  state: '24.5'\n", self.read(name="Датчик температуры в гостиной"))
        self.assertIn("    current_temperature: 25.5\n    temperature_unit: °C\n    temperature: 27.0\n", self.read(name="Тёплый пол"))
        self.assertIn("    unit_of_measurement: '%'\n", self.read(name="Датчик влажности"))
        self.assertEqual(self.tool(house.READ_TOOL, name="Сауна"), {"success": False, "error": "No entities matched"})

    def test_intent_confirms_the_area_and_the_entity(self):
        reply = self.tool("intent__HassTurnOff", **KITCHEN_LIGHT, domain=["light"])
        self.assertEqual(reply, {"speech": {}, "response_type": "action_done", "data": {"failed": [], "success": [
            {"name": "Кухня", "type": "area", "id": "кухня"},
            {"name": "Свет на кухне", "type": "entity", "id": "light.light_0"},
        ]}})
        # A light that is off reports an empty brightness, and turning it on brings the old one back.
        self.assertIn("  state: 'off'\n  areas: Кухня\n  attributes:\n    brightness:\n", self.read(**KITCHEN_LIGHT))
        self.tool("intent__HassTurnOn", name="свет на кухне")
        self.assertIn("brightness_pct: '70'", self.read(**KITCHEN_LIGHT))

    def test_scene_and_script(self):
        # A scene is an entity of the context and is activated by HassTurnOn, as Home Assistant does it.
        self.assertEqual(
            self.read(name="Спокойной ночи", domain="scene"),
            house.CONTEXT_HEAD + "- names: Спокойной ночи\n  domain: scene\n  state: unknown\n",
        )
        listener = self.house.subscribe()
        reply = self.tool("intent__HassTurnOn", name="спокойной ночи", domain=["scene"])
        self.assertEqual(reply["data"], {"failed": [], "success": [
            {"name": "Спокойной ночи", "type": "entity", "id": "scene.good_night"},
        ]})
        self.assertNotIn("state: 'on'", self.read(domain="light"))
        self.assertIn("state: 'on'", self.read(name="Вентилятор"))
        self.assertIn("HassTurnOff does not apply", self.tool("intent__HassTurnOff", name="Спокойной ночи", domain=["scene"]))

        # A script is not in the context: it is a tool named by its object id, with the name of the script in the title.
        self.assertEqual(self.tool(house.READ_TOOL, name="Доброе утро"), {"success": False, "error": "No entities matched"})
        tools = {tool["name"]: tool for tool in self.mcp("tools/list")["tools"]}
        self.assertEqual(tools["script__good_morning"], {
            "name": "script__good_morning", "title": "Доброе утро", "description": "",
            "inputSchema": {"type": "object", "properties": {}},
        })
        self.assertEqual(self.tool("script__good_morning"), {"result": {}})
        self.assertIn("state: 'on'", self.read(name="Чайник"))
        self.assertIn("current_position: '100'", self.read(name="Шторы в спальне"))
        # It is stopped as an entity is switched off; a virtual script is over at once.
        reply = self.tool("intent__HassTurnOff", name="Доброе утро", domain=["script"])
        self.assertEqual(reply["data"]["success"], [{"name": "Доброе утро", "type": "entity", "id": "script.good_morning"}])
        self.assertIn("state: 'on'", self.read(name="Чайник"))

        self.house.unsubscribe(listener)
        calls = []
        while not listener.empty():
            event = listener.get_nowait()
            if event["type"] == "call":
                calls.append(event)
        # The page shows which devices a scenario changed, and a script by its name.
        self.assertIn("hvac_fan_0", calls[0]["changed"])
        self.assertEqual([call.get("title") for call in calls if call["tool"].startswith("script__")], ["Доброе утро"])

    def test_light_brightness_and_colour_temperature(self):
        self.tool("light__HassLightSet", **KITCHEN_LIGHT, brightness=30)
        self.assertIn("brightness_pct: '30'", self.read(**KITCHEN_LIGHT))
        for kelvin, percent in ((2700, 0), (4600, 50), (6500, 100), (2000, 0), (9000, 100)):
            self.tool("light__HassLightSet", **KITCHEN_LIGHT, temperature=kelvin)
            self.assertEqual(self.house.values["light_0"]["light_colour_temp"], percent)
        self.tool("light__HassLightSet", **KITCHEN_LIGHT, brightness=0)
        self.assertIn("state: 'off'", self.read(**KITCHEN_LIGHT))

    def test_all_lights_support_colour_in_both_houses(self):
        for name in ("lake_house", "apartment"):
            self.house.switch(name)
            lights = [d for d in self.house.scene()["devices"] if d["type"] == "light"]
            self.assertEqual(len(lights), 14 if name == "lake_house" else 5)
            for device in lights:
                with self.subTest(house=name, light=device["name"]):
                    parameters = {p["name"]: p["value"] for p in device["parameters"]}
                    self.assertIn("light_colour", parameters)
                    self.assertIsNone(parameters["light_colour"])
                    reply = self.tool("light__HassLightSet", name=device["name"], color="blue")
                    self.assertEqual(reply["response_type"], "action_done")
                    self.assertEqual(self.house.values[device["device_id"]]["light_colour"], "0;0;255")
            self.house.reset()
            self.assertTrue(all(self.house.values[d["device_id"]]["light_colour"] is None for d in lights))

    def test_named_colours_and_aliases(self):
        tools = {t["name"]: t for t in self.mcp("tools/list")["tools"]}
        self.assertEqual(tools["light__HassLightSet"]["inputSchema"]["properties"]["color"], {"type": "string"})
        for name, rgb in [("red", "255;0;0"), ("Dark Orange", "255;140;0"), (" LIME ", "0;255;0"),
                          ("cyan", "0;255;255"), ("aqua", "0;255;255"), ("blue", "0;0;255"),
                          ("magenta", "255;0;255"), ("pink", "255;192;203"), ("white", "255;255;255"),
                          ("black", "0;0;0"), ("navy blue", "0;0;128"), ("homeassistant", "24;188;242")]:
            with self.subTest(colour=name):
                self.tool("intent__HassTurnOff", **KITCHEN_LIGHT)
                reply = self.tool("light__HassLightSet", **KITCHEN_LIGHT, color=name)
                self.assertEqual(reply["response_type"], "action_done")
                self.assertEqual(self.house.values["light_0"]["light_colour"], rgb)
                self.assertTrue(self.house.values["light_0"]["on_off"])

    def test_bad_colours_do_not_change_any_target(self):
        before = self.house.snapshot()
        for value in ["unknown", "255;0;0", "rebeccapurple", "", None, 123, [], "dark\tblue"]:
            with self.subTest(colour=value):
                result = self.mcp("tools/call", name="light__HassLightSet",
                                  arguments={"domain": ["light"], "color": value, "brightness": 10})
                self.assertTrue(result["isError"])
                self.assertIn("Unknown color", result["content"][0]["text"])
                self.assertEqual(self.house.snapshot(), before)
        self.assertIsInstance(self.tool("light__HassLightSet", **KITCHEN_LIGHT, color="red", temperature=2700), str)
        self.assertEqual(self.house.snapshot(), before)

    def test_colour_temperature_clears_colour_and_brightness_preserves_it(self):
        for level in (0, 30, 100):
            self.tool("light__HassLightSet", **KITCHEN_LIGHT, color="red")
            self.tool("light__HassLightSet", **KITCHEN_LIGHT, brightness=level)
            self.assertEqual(self.house.values["light_0"]["light_colour"], "255;0;0")
            self.assertEqual(self.house.values["light_0"]["on_off"], level > 0)
            self.tool("light__HassLightSet", **KITCHEN_LIGHT, temperature=2700)
            self.assertIsNone(self.house.values["light_0"]["light_colour"])
            self.assertEqual(self.house.values["light_0"]["light_colour_temp"], 0)
            self.assertTrue(self.house.values["light_0"]["on_off"])

    def test_context_reports_colour_or_warmth_of_a_light_that_is_on(self):
        self.tool("light__HassLightSet", **KITCHEN_LIGHT, color="red")
        self.assertTrue(self.read(**KITCHEN_LIGHT).endswith("    brightness_pct: '70'\n    rgb_color: [255, 0, 0]\n"))
        self.tool("light__HassLightSet", **KITCHEN_LIGHT, temperature=2700)
        self.assertTrue(self.read(**KITCHEN_LIGHT).endswith("    brightness_pct: '70'\n    color_temp_kelvin: '2700'\n"))
        self.tool("intent__HassTurnOff", **KITCHEN_LIGHT, domain=["light"])
        self.assertNotIn("color_temp_kelvin", self.read(**KITCHEN_LIGHT))

    def test_page_colour_contract(self):
        for value in ("255;18;0", "0;0;0", "255;255;255", None):
            self.assertEqual(self.post("/api/devices/light_0", {"param": "light_colour", "value": value})[0], 200)
            self.assertEqual(self.house.values["light_0"]["light_colour"], value)
        for value in ("red", "256;0;0", "0;-1;0", "0;0;256", "0;0", "1.5;0;0", 1, True, []):
            self.assertEqual(self.post("/api/devices/light_0", {"param": "light_colour", "value": value})[0], 400)

    def test_vacuum_start_return_and_context_in_both_houses(self):
        for name in ("lake_house", "apartment"):
            self.house.switch(name)
            devices = [d for d in self.house.scene()["devices"] if d["type"] == "vacuum_cleaner"]
            self.assertEqual(len(devices), 1)
            device = devices[0]
            self.assertEqual(device["name"], "Робот-пылесос")
            params = {p["name"]: p for p in device["parameters"]}
            self.assertFalse(params["on_off"]["value"])
            self.assertEqual(params["battery_percentage"]["value"], 80)
            self.assertTrue(params["battery_percentage"]["read_only"])
            target = {"name": device["name"], "area": device["room"], "domain": ["vacuum"]}
            for tool, state, on in [("HassVacuumStart", "cleaning", True),
                                    ("HassVacuumReturnToBase", "docked", False)]:
                reply = self.tool("vacuum__" + tool, **target)
                self.assertEqual(reply["response_type"], "action_done")
                self.assertEqual(reply["data"]["success"][-1]["id"], "vacuum.vacuum_cleaner_0")
                self.assertEqual(self.house.values["vacuum_cleaner_0"]["on_off"], on)
                self.assertEqual(self.read(**target), house.CONTEXT_HEAD
                                 + f"- names: Робот-пылесос\n  domain: vacuum\n  state: {state}\n  areas: {device['room']}\n")
            self.assertEqual(self.house.values["vacuum_cleaner_0"]["battery_percentage"], 80)

    def test_page_colour_temperature_clears_colour(self):
        self.assertEqual(self.post("/api/devices/light_0", {"param": "light_colour", "value": "255;0;0"})[0], 200)
        self.assertEqual(self.post("/api/devices/light_0", {"param": "light_colour_temp", "value": 30})[0], 200)
        self.assertIsNone(self.house.values["light_0"]["light_colour"])
        self.assertEqual(self.house.values["light_0"]["light_colour_temp"], 30)
        # Brightness keeps the colour, as by the assistant.
        self.post("/api/devices/light_0", {"param": "light_colour", "value": "255;0;0"})
        self.post("/api/devices/light_0", {"param": "light_brightness", "value": 40})
        self.assertEqual(self.house.values["light_0"]["light_colour"], "255;0;0")

    def test_assistant_place(self):
        for name in self.house.houses:
            with self.subTest(house=name):
                self.house.switch(name)
                assistant = self.house.scene()["assistant"]
                # The room of the speaker is current_room of the demo model; the page puts the speaker at pos.
                self.assertEqual(assistant["area"], "Гостиная")
                self.assertEqual(len(assistant["pos"]), 3)
                self.assertEqual({key: assistant[key] for key in ("mode", "ready")}, self.house.assistant)
        spec = json.loads((Path(house.__file__).parent / "houses" / "lake_house.json").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "lake_house.json"
            for place, error in [(None, False), ({"area": "Гостиная", "pos": [1, 2, 3], "mode": "voice", "ready": True}, False),
                                 ({"area": "Чердак", "pos": [0, 0, 0]}, True),
                                 ({"area": "Гостиная", "pos": [0, 0]}, True), ({"area": "Гостиная", "pos": [0, "0", 0]}, True)]:
                with self.subTest(place=place):
                    path.write_text(json.dumps({**spec, "assistant": place} if place else
                                               {key: value for key, value in spec.items() if key != "assistant"}), encoding="utf-8")
                    if error:
                        self.assertRaises(ValueError, house.House, directory, "lake_house")
                    elif place:
                        # Only the place is taken from the record: the state of the assistant is the house's own.
                        self.assertEqual(house.House(directory, "lake_house").scene()["assistant"],
                                         {"mode": None, "ready": False, "area": "Гостиная", "pos": [1, 2, 3]})
                    else:
                        # Without the record the page draws no speaker.
                        self.assertNotIn("area", house.House(directory, "lake_house").scene()["assistant"])

    def test_a_light_needs_every_value_of_the_light_intents(self):
        spec = json.loads((Path(house.__file__).parent / "houses" / "lake_house.json").read_text(encoding="utf-8"))
        for parameter in house.DOMAIN_PARAMS["light"]:
            with self.subTest(parameter=parameter), tempfile.TemporaryDirectory() as directory:
                devices = [{**device, "values": {key: value for key, value in device["values"].items() if key != parameter}}
                           if device["name"] == "Свет на кухне" else device for device in spec["devices"]]
                (Path(directory) / "lake_house.json").write_text(json.dumps({**spec, "devices": devices}), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, f"Свет на кухне needs {parameter}"):
                    house.House(directory, "lake_house")

    def test_a_scene_setting_a_colour_temperature_clears_the_colour(self):
        spec = json.loads((Path(house.__file__).parent / "houses" / "lake_house.json").read_text(encoding="utf-8"))
        scene = {"domain": "scene", "id": "warm", "name": "Тёплый свет", "set": [{"name": "Свет на кухне", "values": {"light_colour_temp": 10}}]}
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / "lake_house.json").write_text(json.dumps({**spec, "scenarios": [scene]}), encoding="utf-8")
            home = house.House(directory, "lake_house")
            home.call("light__HassLightSet", {**KITCHEN_LIGHT, "color": "red"})
            home.call("intent__HassTurnOn", {"name": "Тёплый свет", "domain": ["scene"]})
            self.assertEqual((home.values["light_0"]["light_colour"], home.values["light_0"]["light_colour_temp"]), (None, 10))

    def test_an_unexpected_failure_is_a_tool_error(self):
        events = self.house.subscribe()
        self.addCleanup(self.house.unsubscribe, events)
        events.get(timeout=1)
        before = self.house.snapshot()
        with mock.patch.object(house, "intent", side_effect=KeyError("light_colour")):
            text = self.tool("light__HassLightSet", **KITCHEN_LIGHT, color="red")
        self.assertEqual(text, "KeyError: 'light_colour'")
        event = events.get(timeout=1)
        self.assertEqual((event["type"], event["is_error"], event["error"]), ("call", True, "KeyError: 'light_colour'"))
        self.assertEqual(self.house.snapshot(), before)

    def test_vacuum_intents_do_not_control_other_domains(self):
        before = self.house.snapshot()
        for tool, arguments in [("vacuum__HassVacuumStart", KITCHEN_LIGHT),
                                ("vacuum__HassVacuumReturnToBase", KITCHEN_LIGHT),
                                ("light__HassLightSet", {"name": "Робот-пылесос", "brightness": 10})]:
            self.assertIsInstance(self.tool(tool, **arguments), str)
        self.assertEqual(self.house.snapshot(), before)

    def test_cover_valve_climate_and_tv(self):
        self.tool("intent__HassSetPosition", name="Шторы в спальне", position=40)
        self.assertIn("  state: open\n  areas: Спальня\n  attributes:\n    current_position: '40'\n", self.read(name="Шторы в спальне"))
        self.tool("intent__HassTurnOff", name="Шторы в спальне", domain=["cover"])
        self.assertIn("  state: closed\n", self.read(name="Шторы в спальне"))
        self.tool("intent__HassTurnOff", name="Кран воды", domain=["valve"])
        self.assertIn("  state: closed\n", self.read(name="Кран воды"))
        self.tool("climate__HassClimateSetTemperature", name="Радиатор", temperature=23.5)
        self.assertIn("    temperature: 23.5\n", self.read(name="Радиатор"))
        self.tool("intent__HassTurnOn", name="Кондиционер", domain=["climate"])
        self.assertIn("  state: cool\n", self.read(name="Кондиционер"))
        self.tool("media_player__HassSetVolume", name="Телевизор", volume_level=40)
        self.assertIn("    volume_level: 0.4\n    device_class: tv\n", self.read(name="Телевизор"))

    def test_a_setting_turns_an_off_device_on(self):
        self.tool("climate__HassClimateSetTemperature", name="Кондиционер", temperature=20)
        self.assertTrue(self.house.values["hvac_heater_3"]["on_off"])
        self.assertIn("  state: cool\n", self.read(name="Кондиционер"))
        self.tool("media_player__HassMediaPlayerMute", name="Телевизор")
        self.assertTrue(self.house.values["tv_0"]["on_off"])
        self.tool("intent__HassTurnOff", name="Свет на кухне", domain=["light"])
        self.tool("light__HassLightSet", **KITCHEN_LIGHT, brightness=0)  # an explicit off stays off
        self.assertFalse(self.house.values["light_0"]["on_off"])
        # By hand: a setting turns the heater on, a reading of the room does not.
        self.assertEqual(self.post("/api/devices/hvac_heater_4", {"param": "temperature", "value": 18})[0], 200)
        self.assertFalse(self.house.values["hvac_heater_4"]["on_off"])
        self.assertEqual(self.post("/api/devices/hvac_heater_4", {"param": "hvac_temp_set", "value": 25})[0], 200)
        self.assertTrue(self.house.values["hvac_heater_4"]["on_off"])

    def test_bad_calls_fail_and_change_nothing(self):
        before = self.house.snapshot()
        for name, arguments in [
            ("intent__HassTurnOn", {"name": "Сауна"}),  # no such device
            ("intent__HassTurnOn", {"name": "Входная дверь"}),  # a sensor is not switched
            ("climate__HassClimateSetTemperature", {"name": "Радиатор", "temperature": 99}),
            ("light__HassLightSet", {**KITCHEN_LIGHT, "brightness": "bright"}),
            ("light__HassLightSet", KITCHEN_LIGHT),  # nothing to set
            ("intent__HassBrew", {"name": "Чайник"}),
        ]:
            self.assertIsInstance(self.tool(name, **arguments), str, name)
        self.assertEqual(self.house.snapshot(), before)

    def test_page_edits_and_events(self):
        events = self.house.subscribe()
        first = events.get(timeout=1)
        self.assertEqual((first["type"], first["id"], len(first["devices"])), ("house", "lake_house", 40))
        self.assertEqual(self.post("/api/devices/sensor_door_0", {"param": "doorcontact_state", "value": True})[0], 200)
        self.assertEqual(events.get(timeout=1), {"type": "values", "values": {"sensor_door_0": {"doorcontact_state": True}}})
        self.assertIn("  state: 'on'\n", self.read(name="Входная дверь"))
        self.assertEqual(events.get(timeout=1)["tool"], house.READ_TOOL)
        self.tool("intent__HassTurnOn", name="Чайник")
        self.assertEqual(events.get(timeout=1), {"type": "call", "tool": "intent__HassTurnOn", "arguments": {"name": "Чайник"}, "changed": ["socket_0"], "is_error": False, "background": False})
        for body in [
            {"param": "doorcontact_state", "value": "open"},
            {"param": "on_off", "value": True},  # a door has no switch
            {"param": "doorcontact_state"},
            [],
        ]:
            self.assertEqual(self.post("/api/devices/sensor_door_0", body)[0], 400, body)
        self.assertEqual(self.post("/api/devices/light_0", {"param": "light_brightness", "value": 0})[0], 400)
        self.assertEqual(self.post("/api/chat", {"text": "включи свет"})[0], 400)  # no assistant is attached
        self.assertEqual(self.post("/api/reset", {})[0], 200)
        self.assertFalse(self.house.values["sensor_door_0"]["doorcontact_state"])
        self.house.unsubscribe(events)

    def test_switch_to_another_house(self):
        events = self.house.subscribe()
        events.get(timeout=1)
        self.tool("intent__HassTurnOn", name="Чайник")
        self.assertEqual(self.post("/api/house", {"id": "apartment"})[0], 200)
        seen = [events.get(timeout=1) for _ in range(3)][-1]
        self.assertEqual((seen["type"], seen["id"], seen["name"], len(seen["devices"])), ("house", "apartment", "Городская квартира", 17))
        self.assertEqual(seen["houses"][1], {"id": "lake_house", "name": "Дом у озера", "devices": 40})
        # The assistant now gets the other house, and that house starts from its initial state.
        self.assertEqual(self.read().count("- names: "), 17 + 1)  # the devices and the scene
        self.assertIsInstance(self.tool("intent__HassTurnOn", name="Ворота гаража"), str)
        self.house.switch("lake_house")
        self.assertIn("  state: 'off'\n", self.read(name="Чайник"))
        for body in [{"id": "castle"}, {"id": "../house"}, {}]:
            self.assertEqual(self.post("/api/house", body)[0], 400, body)
        self.assertEqual(self.house.id, "lake_house")
        self.house.unsubscribe(events)

    def test_every_tool_call_publishes_one_event(self):
        events = self.house.subscribe()
        self.addCleanup(self.house.unsubscribe, events)
        events.get(timeout=1)
        cases = [
            (house.READ_TOOL, KITCHEN_LIGHT, [], False, False),
            (house.READ_TOOL, {}, [], False, True),
            (house.READ_TOOL, {"name": "Missing"}, [], True, False),
            ("intent__HassTurnOn", KITCHEN_LIGHT, [], False, False),
            ("intent__HassTurnOff", KITCHEN_LIGHT, ["light_0"], False, False),
            ("light__HassLightSet", {**KITCHEN_LIGHT, "color": "unknown"}, [], True, False),
            ("not_a_tool", KITCHEN_LIGHT, [], True, False),
            ("light__HassLightSet", [], [], True, False),
            (None, {}, [], True, False),
            ("script__good_morning", {}, ["curtain_0", "light_0", "socket_0"], False, False),
            ("script__good_morning", {}, [], False, False),
        ]
        for tool, arguments, changed, failed, background in cases:
            with self.subTest(tool=tool, arguments=arguments):
                self.mcp("tools/call", name=tool, arguments=arguments)
                seen = []
                while not events.empty():
                    seen.append(events.get_nowait())
                self.assertEqual([e["type"] for e in seen], ["call", "values"] if changed else ["call"])
                event = seen[0]
                # Whatever the call brought, the page gets a name and an object.
                self.assertEqual(event["tool"], tool or "")
                self.assertEqual(event["arguments"], arguments if isinstance(arguments, dict) else {})
                self.assertEqual(event["changed"], changed)
                self.assertEqual(event["is_error"], failed)
                self.assertEqual(event["background"], background)
                self.assertEqual(bool(event.get("error")), failed)
                if isinstance(tool, str) and tool.startswith("script__"):
                    self.assertEqual(event["title"], "Доброе утро")

    def test_context_snapshot_event_is_background(self):
        events = self.house.subscribe()
        self.addCleanup(self.house.unsubscribe, events)
        events.get(timeout=1)
        self.mcp("resources/read", uri=house.CONTEXT_URI)
        self.assertEqual(events.get(timeout=1), {"type": "call", "tool": "resources/read",
                         "arguments": {"uri": house.CONTEXT_URI}, "changed": [], "is_error": False, "background": True})
        self.assertTrue(events.empty())

    def answer(self, events):
        """Events up to the next answer of the assistant."""
        seen = []
        while not seen or seen[-1]["type"] != "answer":
            try:
                seen.append(events.get(timeout=5))
            except queue.Empty:
                self.fail(f"no answer from the assistant: {seen}")
        return seen

    def test_assistant_pipe(self):
        events = self.house.subscribe()
        assistant = house.Assistant(self.house, [sys.executable, "-c", STUB, "--text"], None)
        self.addCleanup(assistant.stop)
        assistant.send("включи  свет\nна кухне")  # one request is one line of the assistant
        with self.assertRaises(ValueError):
            assistant.send(" ")
        seen = self.answer(events)
        self.assertIn({"type": "assistant", "mode": "text", "ready": True}, seen)
        self.assertIn({"type": "request", "text": "включи свет на кухне"}, seen)
        self.assertEqual(seen[-1], {"type": "answer", "text": "ok включи свет на кухне"})
        # A restart, as on a switch of the house: another process that is not ready at first; the house keeps running.
        first = assistant.process
        assistant.restart()
        self.assertIsNot(assistant.process, first)
        self.assertIsNotNone(first.poll())
        assistant.send("привет")
        seen = self.answer(events)
        self.assertLess(seen.index({"type": "assistant", "mode": "text", "ready": False}),
                        seen.index({"type": "assistant", "mode": "text", "ready": True}))
        self.assertFalse(assistant.exited.is_set())
        # Only an assistant that exits by itself stops the house.
        assistant.send("выход")
        self.assertTrue(assistant.exited.wait(5))
        self.assertEqual(assistant.code, 7)
        self.house.unsubscribe(events)

    def report(self, events, comment):
        """The event of the page for a report asked over /api/report; a report is not a request."""
        self.assertEqual(self.post("/api/report", {"comment": comment}), (200, {}))
        while True:
            event = events.get(timeout=5)
            self.assertNotEqual(event["type"], "request")
            if event["type"] == "report":
                return event

    def test_report_in_both_modes(self):
        no_assistant = (400, {"error": "the house runs without an assistant"})
        self.assertEqual(self.post("/api/report", {"comment": "x"}), no_assistant)
        events = self.house.subscribe()
        self.addCleanup(self.house.unsubscribe, events)
        self.addCleanup(setattr, house.Handler, "assistant", None)
        for mode in (["--text"], []):
            with self.subTest(mode=mode):
                assistant = house.Assistant(self.house, [sys.executable, "-c", STUB, *mode], None)
                house.Handler.assistant = assistant
                self.addCleanup(assistant.stop)
                self.assertEqual(self.report(events, "свет  не\nвключился"),
                                 {"type": "report", "path": "bug_reports/свет не включился.json"})
                self.assertEqual(self.report(events, " "), {"type": "report", "error": "no scene"})
                assistant.stop()
                self.assertEqual(self.post("/api/report", {"comment": "x"}),
                                 (400, {"error": "the assistant is not running"}))


if __name__ == "__main__":
    unittest.main()
