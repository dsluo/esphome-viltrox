"""Viltrox LED panels (Weeylite app protocol), driven as iBeacon advertisements.

The `viltrox:` hub owns the BLE radio: it drives the advertising calls
directly instead of going through esp32_ble's shared advertiser, which
rotates every advertisement on a 10s cycle and would delay commands.
Lights (`light: platform: viltrox`) hand it frames to send; see
PROTOCOL.md for the encoding.
"""

import esphome.codegen as cg
from esphome.components import esp32_ble
from esphome.components.esp32 import request_bluetooth
from esphome.components.esp32_ble import CONF_BLE_ID
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TX_POWER
from esphome.core import TimePeriod

AUTO_LOAD = ["esp32_ble"]
DEPENDENCIES = ["esp32"]
CODEOWNERS = ["@dsluo"]

CONF_VILTROX_ID = "viltrox_id"
CONF_MAJOR = "major"
CONF_MINOR = "minor"
CONF_MEASURED_POWER = "measured_power"
CONF_COMMAND_INTERVAL = "command_interval"

viltrox_ns = cg.esphome_ns.namespace("viltrox")
ViltroxComponent = viltrox_ns.class_("ViltroxComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ViltroxComponent),
        cv.GenerateID(CONF_BLE_ID): cv.use_id(esp32_ble.ESP32BLE),
        # iBeacon fields the WeeyliteII app sends alongside every command
        cv.Optional(CONF_MAJOR, default=10): cv.uint16_t,
        cv.Optional(CONF_MINOR, default=110): cv.uint16_t,
        cv.Optional(CONF_MEASURED_POWER, default=-59): cv.int_range(
            min=-128, max=0
        ),
        # Panels ignore commands less than 100ms apart; each command is held
        # on air for at least this long so the panel has time to catch it.
        cv.Optional(CONF_COMMAND_INTERVAL, default="300ms"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=TimePeriod(milliseconds=100)),
        ),
        cv.Optional(CONF_TX_POWER): cv.All(
            cv.conflicts_with_component("esp32_hosted"),
            cv.decibel,
            cv.enum(esp32_ble.TX_POWER_LEVELS, int=True),
        ),
    }
).extend(cv.COMPONENT_SCHEMA)

FINAL_VALIDATE_SCHEMA = esp32_ble.validate_variant


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_BLE_ID])
    esp32_ble.register_gap_event_handler(parent, var)

    cg.add(var.set_major(config[CONF_MAJOR]))
    cg.add(var.set_minor(config[CONF_MINOR]))
    cg.add(var.set_measured_power(config[CONF_MEASURED_POWER]))
    cg.add(var.set_command_interval(config[CONF_COMMAND_INTERVAL]))
    if CONF_TX_POWER in config:
        cg.add(var.set_tx_power(config[CONF_TX_POWER]))

    request_bluetooth()
