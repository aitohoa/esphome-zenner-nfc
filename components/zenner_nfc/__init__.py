# SPDX-License-Identifier: LGPL-2.1-or-later
# Modified for Zenner NFC on 2026-10-08; see NOTICE.md.
"""Zenner IUW/S NFC reader with an exclusive PN5180 SPI bus."""

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome import pins
from esphome.components import binary_sensor, sensor, spi, text_sensor
from esphome.components.spi import (
    CONF_INTERFACE_INDEX,
    CONF_RELEASE_DEVICE,
    CONF_SPI_MODE,
)
from esphome.const import (
    CONF_CS_PIN,
    CONF_DATA_RATE,
    CONF_FLOW,
    CONF_ID,
    CONF_INVERTED,
    CONF_SPI,
    CONF_SPI_ID,
    CONF_STATUS,
    CONF_UID,
    CONF_UNIT_OF_MEASUREMENT,
    CONF_VOLUME,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

CONF_FORWARD_VOLUME = "forward_volume"
CONF_REVERSE_VOLUME = "reverse_volume"
CONF_WATER_TEMPERATURE = "water_temperature"
CONF_AMBIENT_TEMPERATURE = "ambient_temperature"
CONF_RAW_RESPONSE = "raw_response"
CONF_READ_SUCCESS = "read_success"
CONF_CONSECUTIVE_ERRORS_LIMIT = "consecutive_errors_limit"
CONF_PN5180_BUSY_PIN = "pn5180_busy_pin"
CONF_PN5180_RST_PIN = "pn5180_rst_pin"
CONF_PN5180_DPC = "pn5180_dpc"
CONF_SPI_BUSY_TIMEOUT = "spi_busy_timeout"

AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor"]
DEPENDENCIES = ["esp32", "spi"]
ns = cg.esphome_ns.namespace("zenner_nfc")
ZennerNfc = ns.class_("ZennerNfc", cg.PollingComponent, spi.SPIDevice)
SENSORS = (
    CONF_VOLUME,
    CONF_FORWARD_VOLUME,
    CONF_REVERSE_VOLUME,
    CONF_FLOW,
    CONF_WATER_TEMPERATURE,
    CONF_AMBIENT_TEMPERATURE,
)
TEXT_SENSORS = (CONF_STATUS, CONF_UID, CONF_RAW_RESPONSE)


def volume_schema(state_class):
    return sensor.sensor_schema(
        unit_of_measurement="m³",
        accuracy_decimals=3,
        device_class="water",
        state_class=state_class,
    )


def _validate_spi_settings(config):
    if config[CONF_DATA_RATE] != 2000000 or str(config[CONF_SPI_MODE]) != "MODE0":
        raise cv.Invalid("zenner_nfc requires data_rate: 2MHz and spi_mode: MODE0")
    if config.get(CONF_RELEASE_DEVICE, False):
        raise cv.Invalid("zenner_nfc requires release_device: false")
    for key in (CONF_CS_PIN, CONF_PN5180_BUSY_PIN, CONF_PN5180_RST_PIN):
        if config[key].get(CONF_INVERTED, False):
            raise cv.Invalid(f"{key} must not be inverted")
    return config


def _dedicated_bus(config):
    root = fv.full_config.get().get_config_for_path([])
    bus_id = str(config[CONF_SPI_ID])
    users = 0

    def walk(value):
        nonlocal users
        if isinstance(value, dict):
            if CONF_SPI_ID in value and str(value[CONF_SPI_ID]) == bus_id:
                users += 1
            for child in value.values():
                walk(child)
        elif isinstance(value, list):
            for child in value:
                walk(child)

    walk(root)
    if users != 1:
        raise cv.Invalid(
            "PN5180 requires a dedicated SPI bus: "
            "NSS may remain low while waiting for BUSY"
        )
    for bus in root[CONF_SPI]:
        if str(bus[CONF_ID]) == bus_id and CONF_INTERFACE_INDEX not in bus:
            raise cv.Invalid("PN5180 requires a hardware SPI interface")
    return config


def _final_validate(config):
    spi.final_validate_device_schema(
        "zenner_nfc", require_miso=True, require_mosi=True
    )(config)
    return _dedicated_bus(config)


FINAL_VALIDATE_SCHEMA = _final_validate

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ZennerNfc),
            cv.Required(CONF_PN5180_BUSY_PIN): pins.internal_gpio_input_pin_schema,
            cv.Required(CONF_PN5180_RST_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_SPI_BUSY_TIMEOUT, default="50ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=cv.TimePeriod(milliseconds=1),
                    max=cv.TimePeriod(milliseconds=100),
                ),
            ),
            cv.Optional(CONF_PN5180_DPC, default="preserve"): cv.one_of(
                "preserve", "disabled", "enabled", lower=True
            ),
            cv.Optional(CONF_CONSECUTIVE_ERRORS_LIMIT, default=3): cv.int_range(
                min=1, max=255
            ),
            cv.Optional(CONF_VOLUME): volume_schema("total"),
            cv.Optional(CONF_FORWARD_VOLUME): volume_schema("total_increasing"),
            cv.Optional(CONF_REVERSE_VOLUME): volume_schema("total_increasing"),
            cv.Optional(CONF_FLOW): sensor.sensor_schema(
                unit_of_measurement="m³/h",
                accuracy_decimals=3,
                device_class="volume_flow_rate",
                state_class="measurement",
            ).extend(
                {
                    cv.Optional(CONF_UNIT_OF_MEASUREMENT, default="m³/h"): cv.one_of(
                        "m³/h", "L/min", "L/s"
                    ),
                }
            ),
            **{
                cv.Optional(p): sensor.sensor_schema(
                    unit_of_measurement="°C",
                    accuracy_decimals=2,
                    device_class="temperature",
                    state_class="measurement",
                )
                for p in (CONF_WATER_TEMPERATURE, CONF_AMBIENT_TEMPERATURE)
            },
            cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_UID): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_RAW_RESPONSE): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_READ_SUCCESS): binary_sensor.binary_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(
        spi.spi_device_schema(
            cs_pin_required=True, default_data_rate="2MHz", default_mode="MODE0"
        )
    )
    .extend({cv.Required(CONF_CS_PIN): pins.internal_gpio_output_pin_schema}),
    _validate_spi_settings,
    cv.only_with_framework("esp-idf"),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await spi.register_spi_device(var, config)
    cg.add(var.set_busy_pin(await cg.gpio_pin_expression(config[CONF_PN5180_BUSY_PIN])))
    cg.add(var.set_reset_pin(await cg.gpio_pin_expression(config[CONF_PN5180_RST_PIN])))
    await cg.register_component(var, config)
    cg.add(
        var.set_dpc_mode(
            {"preserve": 0, "disabled": 1, "enabled": 2}[config[CONF_PN5180_DPC]]
        )
    )
    cg.add(var.set_busy_timeout(config[CONF_SPI_BUSY_TIMEOUT].total_milliseconds))
    cg.add(var.set_consecutive_errors_limit(config[CONF_CONSECUTIVE_ERRORS_LIMIT]))
    for key in SENSORS:
        if key in config:
            entity = await sensor.new_sensor(config[key])
            cg.add(getattr(var, f"set_{key}_sensor")(entity))
    for key in TEXT_SENSORS:
        if key in config:
            entity = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(var, f"set_{key}_sensor")(entity))
    if CONF_READ_SUCCESS in config:
        entity = await binary_sensor.new_binary_sensor(config[CONF_READ_SUCCESS])
        cg.add(var.set_read_success_sensor(entity))
    if CONF_FLOW in config:
        cg.add(
            var.set_flow_unit(
                {"m³/h": 0, "L/min": 1, "L/s": 2}[
                    config[CONF_FLOW][CONF_UNIT_OF_MEASUREMENT]
                ]
            )
        )
