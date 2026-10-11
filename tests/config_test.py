# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise optional entities and reject invalid settings using ESPHome itself."""
from pathlib import Path
import copy
import subprocess
import sys
import tempfile
import yaml

ROOT = Path(__file__).resolve().parents[1]
base = yaml.safe_load((ROOT / "tests/compile.yaml").read_text())
base["external_components"][0]["source"]["path"] = str(ROOT / "components")

def check(config, expected, label, generate=False):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "test.yaml"
        path.write_text(yaml.safe_dump(config, allow_unicode=True))
        command = [sys.executable, "-m", "esphome"]
        command += ["compile", "--only-generate"] if generate else ["config"]
        result = subprocess.run(command + [str(path)], capture_output=True, text=True)
        assert (result.returncode == 0) == expected, label + "\n" + result.stdout + result.stderr
        if generate and expected:
            cpp = next(Path(directory).rglob("main.cpp")).read_text()
            if label.startswith("DPC "):
                mode = {"preserve": 0, "disabled": 1, "enabled": 2}[config["zenner_nfc"]["pn5180_dpc"]]
                assert f"set_dpc_mode({mode})" in cpp
            if label == "metric flow":
                assert "set_flow_unit(0)" in cpp
            if label == "litres per second":
                assert "set_flow_unit(2)" in cpp
    print("PASS:", label)

check(base, True, "all entities")
minimal = copy.deepcopy(base)
minimal["zenner_nfc"] = {k:v for k,v in base["zenner_nfc"].items() if k.startswith("pn5180_") or k in ("id", "spi_id", "cs_pin")}
minimal["zenner_nfc"]["update_interval"] = "never"
check(minimal, True, "manual-only, no entities", generate=True)
metric = copy.deepcopy(base)
metric["zenner_nfc"]["flow"].pop("unit_of_measurement")
check(metric, True, "metric flow", generate=True)
per_second = copy.deepcopy(base)
per_second["zenner_nfc"]["flow"]["unit_of_measurement"] = "L/s"
check(per_second, True, "litres per second", generate=True)
for timeout in ("0ms", "101ms"):
    bad = copy.deepcopy(base);bad["zenner_nfc"]["spi_busy_timeout"] = timeout
    check(bad, False, "invalid BUSY timeout " + timeout)
for value in (0, 256):
    bad = copy.deepcopy(base);bad["zenner_nfc"]["consecutive_errors_limit"] = value
    check(bad, False, f"invalid error limit {value}")
bad = copy.deepcopy(base);bad["zenner_nfc"]["flow"]["unit_of_measurement"] = "gal/s"
check(bad, False, "unsupported flow unit")
bad = copy.deepcopy(base);bad["esp32"]["framework"]["type"] = "arduino"
check(bad, False, "unsupported framework")

# Validate the documented encrypted API/OTA example with temporary test secrets.
import base64
import os
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    example = (ROOT / "examples/esp32-c6.yaml").read_text().replace(
        "path: ../components", "path: " + str(ROOT / "components"))
    (path / "example.yaml").write_text(example)
    secrets = {"wifi_ssid": "test", "wifi_password": "test-only-password",
               "api_encryption_key": base64.b64encode(os.urandom(32)).decode(),
               "ota_password": "test-only-password"}
    (path / "secrets.yaml").write_text(yaml.safe_dump(secrets))
    result = subprocess.run([sys.executable, "-m", "esphome", "config", str(path / "example.yaml")],
                            capture_output=True, text=True)
    assert result.returncode == 0, "Device example failed validation"
print("PASS: documented device example")

for mode in ("preserve", "disabled", "enabled"):
    test = copy.deepcopy(base);test["zenner_nfc"]["pn5180_dpc"] = mode
    check(test, True, "DPC " + mode, generate=True)
bad = copy.deepcopy(base);bad["zenner_nfc"]["pn5180_dpc"] = "automatic"
check(bad, False, "unsupported DPC mode")

for key in ("mosi_pin", "miso_pin"):
    test = copy.deepcopy(base);test["spi"].pop(key)
    check(test, False, "missing SPI " + key)
for key, value in (("spi_mode", "MODE3"), ("data_rate", "8MHz"), ("release_device", True)):
    test = copy.deepcopy(base);test["zenner_nfc"][key] = value
    check(test, False, "unsupported " + key)
test = copy.deepcopy(base);test["spi"]["interface"] = "software"
check(test, False, "software SPI rejected")
test = copy.deepcopy(base);test["sensor"] = [{"platform": "max6675", "spi_id": "nfc_spi", "cs_pin": "GPIO18", "name": "Other SPI device"}]
check(test, False, "shared SPI rejected")
test = copy.deepcopy(base);test["zenner_nfc"]["pn5180_mosi_pin"] = "GPIO4"
check(test, False, "legacy SPI pin rejected")
