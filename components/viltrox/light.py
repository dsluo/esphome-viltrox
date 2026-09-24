import re

import esphome.codegen as cg
from esphome.components import button, light, number, select
from esphome.components.light.effects import (
    RGB_EFFECTS,
    register_rgb_effect,
    validate_effects,
)
from esphome.components.light.types import LightEffect
import esphome.config_validation as cv
from esphome.const import (
    CONF_CHANNEL,
    CONF_COLD_WHITE_COLOR_TEMPERATURE,
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_EFFECTS,
    CONF_GROUP,
    CONF_NAME,
    ENTITY_CATEGORY_CONFIG,
    CONF_SPEED,
    CONF_WARM_WHITE_COLOR_TEMPERATURE,
)

from . import CONF_VILTROX_ID, ViltroxComponent, viltrox_ns
from .gels import GELS

DEPENDENCIES = ["viltrox"]
AUTO_LOAD = ["button", "number", "select"]

CONF_CCT_TYPE = "cct_type"
CONF_TINT = "tint"
CONF_SCENE = "scene"
CONF_EFFECT_SPEED = "effect_speed"
CONF_TINT_CONTROL = "tint_control"
CONF_RESEND_BUTTON = "resend_button"
CONF_GEL_SELECT = "gel_select"

# Options of the effect speed select; index + 1 is the wire speed (1-3).
SPEED_OPTIONS = ["Slow", "Medium", "Fast"]

ViltroxLight = viltrox_ns.class_("ViltroxLight", light.LightOutput)
ViltroxSceneEffect = viltrox_ns.class_("ViltroxSceneEffect", LightEffect)
ViltroxSpeedSelect = viltrox_ns.class_(
    "ViltroxSpeedSelect", select.Select, cg.Component
)
ViltroxTintNumber = viltrox_ns.class_(
    "ViltroxTintNumber", number.Number, cg.Component
)
ViltroxResendButton = viltrox_ns.class_("ViltroxResendButton", button.Button)
ViltroxGelSelect = viltrox_ns.class_(
    "ViltroxGelSelect", select.Select, cg.Component
)

# The app's FX list, in order. The app sends the list position as the scene ID
# (FXActivity -> BleLed.setSceneID(position)); BleLed.SCENE_ID is never used.
SCENES = [
    "Flash", "Burst", "Flash Lamp", "Blink", "Weld", "SOS", "Candlelight",
    "Flame", "CCT Loop", "TV", "Firework 1", "Firework 2", "Firework 3",
    "Police", "Ambulance", "Fire Truck", "RGB Loop", "Romantic", "Club 1",
    "Club 2", "Wave Red", "Wave Green", "Wave Blue", "Wave Cyan",
    "Wave Magenta", "Wave Yellow",
]


def _normalize(name):
    return re.sub(r"[^a-z0-9]", "", str(name).lower())


def validate_scene(value):
    """A scene ID (0-25) or name from the app's FX list; returns the ID."""
    if isinstance(value, int):
        return cv.int_range(min=0, max=len(SCENES) - 1)(value)
    wanted = _normalize(value)
    for scene_id, name in enumerate(SCENES):
        if _normalize(name) == wanted:
            return scene_id
    raise cv.Invalid(
        f"unknown scene {value!r}; expected 0-{len(SCENES) - 1} or one of: "
        + ", ".join(SCENES)
    )


def default_scene_name(config):
    if not config[CONF_NAME]:
        config[CONF_NAME] = SCENES[config[CONF_SCENE]]
    return config


def validate_group(value):
    """A-F as in the app (or 1-6); returns the wire value 1-6."""
    value = str(value).strip().upper()
    if len(value) == 1 and value in "ABCDEF":
        return ord(value) - ord("A") + 1
    if value.isdigit() and 1 <= int(value) <= 6:
        return int(value)
    raise cv.Invalid("group must be A-F (or 1-6)")


def validate_color_temperatures(config):
    if config[CONF_COLD_WHITE_COLOR_TEMPERATURE] >= config[CONF_WARM_WHITE_COLOR_TEMPERATURE]:
        raise cv.Invalid(
            f"{CONF_COLD_WHITE_COLOR_TEMPERATURE} must be cooler (higher Kelvin) "
            f"than {CONF_WARM_WHITE_COLOR_TEMPERATURE}"
        )
    return config


CONFIG_SCHEMA = cv.All(
    light.light_schema(
        ViltroxLight,
        light.LightType.RGB,
        # Restore the last state on reboot instead of broadcasting "off".
        default_restore_mode="RESTORE_DEFAULT_OFF",
    ).extend(
        {
            cv.GenerateID(CONF_VILTROX_ID): cv.use_id(ViltroxComponent),
            cv.Required(CONF_CHANNEL): cv.int_range(min=1, max=19),
            cv.Optional(CONF_GROUP, default="A"): validate_group,
            # Only the end state of a transition is sent, so a default
            # transition just delays the state HA shows.
            cv.Optional(
                CONF_DEFAULT_TRANSITION_LENGTH, default="0s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_COLD_WHITE_COLOR_TEMPERATURE, default="8500 K"
            ): cv.color_temperature,
            cv.Optional(
                CONF_WARM_WHITE_COLOR_TEMPERATURE, default="2500 K"
            ): cv.color_temperature,
            cv.Optional(CONF_CCT_TYPE, default=0): cv.int_range(min=0, max=9),
            cv.Optional(CONF_TINT, default=0): cv.int_range(min=-10, max=10),
            # Select entity setting the speed of scene effects that don't set
            # their own; `effect_speed: Effect Speed` is enough.
            cv.Optional(CONF_EFFECT_SPEED): cv.maybe_simple_value(
                select.select_schema(
                    ViltroxSpeedSelect,
                    entity_category=ENTITY_CATEGORY_CONFIG,
                    icon="mdi:speedometer",
                ).extend(cv.COMPONENT_SCHEMA),
                key=CONF_NAME,
            ),
            # Number entity for the tint; `tint` becomes its initial value.
            cv.Optional(CONF_TINT_CONTROL): cv.maybe_simple_value(
                number.number_schema(
                    ViltroxTintNumber,
                    entity_category=ENTITY_CATEGORY_CONFIG,
                    icon="mdi:tune-vertical-variant",
                ).extend(cv.COMPONENT_SCHEMA),
                key=CONF_NAME,
            ),
            # Button that sends the light's current state again.
            cv.Optional(CONF_RESEND_BUTTON): cv.maybe_simple_value(
                button.button_schema(ViltroxResendButton, icon="mdi:sync"),
                key=CONF_NAME,
            ),
            # Select entity with the app's Rosco and Lee gel presets.
            cv.Optional(CONF_GEL_SELECT): cv.maybe_simple_value(
                select.select_schema(
                    ViltroxGelSelect, icon="mdi:palette-swatch"
                ).extend(cv.COMPONENT_SCHEMA),
                key=CONF_NAME,
            ),
            # Every scene from the app unless the config lists its own effects.
            cv.Optional(
                CONF_EFFECTS,
                default=[{"viltrox_scene": {CONF_SCENE: i}} for i in range(len(SCENES))],
            ): validate_effects(RGB_EFFECTS),
        }
    ),
    validate_color_temperatures,
)


async def to_code(config):
    var = await light.new_light(config)
    cg.add(var.set_channel(config[CONF_CHANNEL]))
    cg.add(var.set_group(config[CONF_GROUP]))
    cg.add(var.set_cold_white_temperature(config[CONF_COLD_WHITE_COLOR_TEMPERATURE]))
    cg.add(var.set_warm_white_temperature(config[CONF_WARM_WHITE_COLOR_TEMPERATURE]))
    cg.add(var.set_cct_type(config[CONF_CCT_TYPE]))
    cg.add(var.set_tint(config[CONF_TINT]))

    if speed_config := config.get(CONF_EFFECT_SPEED):
        speed_select = await select.new_select(speed_config, options=SPEED_OPTIONS)
        await cg.register_component(speed_select, speed_config)
        cg.add(var.set_speed_select(speed_select))

    if tint_config := config.get(CONF_TINT_CONTROL):
        tint_number = await number.new_number(
            tint_config, min_value=-10, max_value=10, step=1
        )
        await cg.register_component(tint_number, tint_config)
        cg.add(tint_number.set_initial_value(config[CONF_TINT]))
        cg.add(var.set_tint_number(tint_number))

    if gel_config := config.get(CONF_GEL_SELECT):
        gel_select = await select.new_select(
            gel_config, options=["None"] + [gel["label"] for gel in GELS]
        )
        await cg.register_component(gel_select, gel_config)
        for gel in GELS:
            hue, sat = gel["hue_sat"]
            cg.add(gel_select.add_gel(gel["brand"], gel["index"], hue, sat))
        cg.add(var.set_gel_select(gel_select))

    if resend_config := config.get(CONF_RESEND_BUTTON):
        await button.new_button(resend_config, var)

    hub = await cg.get_variable(config[CONF_VILTROX_ID])
    cg.add(hub.register_light(var))


@register_rgb_effect(
    "viltrox_scene",
    ViltroxSceneEffect,
    "",  # defaults to the scene's name
    {
        cv.Required(CONF_SCENE): validate_scene,
        # The app's speed slider has three steps. Unset, the scene follows
        # the light's effect_speed select (or Medium without one).
        cv.Optional(CONF_SPEED): cv.int_range(min=1, max=3),
    },
    default_scene_name,
)
async def viltrox_scene_effect_to_code(config, effect_id):
    var = cg.new_Pvariable(effect_id, config[CONF_NAME])
    cg.add(var.set_scene(config[CONF_SCENE], config.get(CONF_SPEED, 0)))
    return var
