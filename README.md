# esphome-viltrox

An ESPHome component for controlling Viltrox LED light panels from Home
Assistant. It speaks the Bluetooth protocol of the **Weeylite** app
(WeeyliteII) that these panels use, so an ESP32 can stand in for your phone.
Each panel group shows up in Home Assistant as a normal light with on/off,
brightness, RGB color and color temperature. The panel's built-in scenes
appear as effects. Optional extras add effect speed, tint, gel presets and a
resend button.

The panels have no connection or pairing. The Weeylite app broadcasts each
command as an iBeacon advertisement, and any panel on the matching channel acts
on it. [PROTOCOL.md](PROTOCOL.md) has notes on the command format, based on
what the app sends and how the panels respond.

## Usage

```yaml
external_components:
  - source: github://dsluo/esphome-viltrox
    components: [viltrox]

viltrox:

light:
  - platform: viltrox
    name: Key Light
    channel: 1  # CH number in the app
    group: A    # group letter in the app
```

[example.yaml](example.yaml) is a complete device config.

## `viltrox:` options

| Option | Default | |
|---|---|---|
| `command_interval` | `300ms` | Minimum time each command stays on air before the next. Panels ignore commands under 100ms apart. |
| `major`, `minor`, `measured_power` | `10`, `110`, `-59` | iBeacon fields the app sends. |
| `tx_power` | unset | Advertising TX power, e.g. `9dBm` for more range. |

## `light: platform: viltrox` options

All the usual [light options](https://esphome.io/components/light/), plus:

| Option | Default | |
|---|---|---|
| `channel` | required | CH1-CH19 in the app. |
| `group` | `A` | A-F in the app. |
| `cold_white_color_temperature`, `warm_white_color_temperature` | `8500 K`, `2500 K` | Color temperature range shown in HA. The defaults match the app's slider. |
| `cct_type` | `0` | CCT type index (0-9) sent with color temperature commands. |
| `tint` | `0` | Red/green tint (-10 to 10) sent with color temperature commands. |
| `tint_control` | unset | Adds a number entity for `tint`, e.g. `tint_control: Tint`. `tint` becomes its initial value, and changes are kept across reboots. |
| `resend_button` | unset | Adds a button that sends the light's current state again, e.g. `resend_button: Resend`. Use it after the panel was changed with its own buttons or the app. |
| `gel_select` | unset | Adds a select with the app's 40 Rosco and Lee gel presets, e.g. `gel_select: Gel`. Picking a gel turns the light on and stops any scene. Changing the color or starting a scene sets it back to None, while brightness changes keep the gel. It isn't restored after a reboot. |
| `effect_speed` | unset | Adds a Slow/Medium/Fast select entity for the scene effects' speed, e.g. `effect_speed: Effect Speed`. Takes the usual [select options](https://esphome.io/components/select/). Its value is kept across reboots. |

`restore_mode` defaults to `RESTORE_DEFAULT_OFF` and `default_transition_length`
to `0s`: the panels can't be animated over BLE, so only a transition's end
state is sent.

The panel's 26 built-in scenes (Candlelight, Police, Club 1, RGB Loop, ...)
are added to every light as effects, named as in the app. They run at the
`effect_speed` select's speed (Medium without one). To choose your own set, or
fix a scene's speed, list them under `effects:`:

```yaml
    effects:
      - viltrox_scene:
          scene: Candlelight  # name or ID 0-25, see PROTOCOL.md
          speed: 1            # 1-3; omit to follow effect_speed
      - viltrox_scene:
          name: Party         # defaults to the scene's name
          scene: Club 1
          speed: 3
```

## How it behaves

- **One-way.** The panels never report back, so HA shows what was last sent.
  Changes made with the app or remote won't show up in HA.
- **Power is per channel.** The protocol's on/off commands address every group
  on a channel, so turning one group's light off turns off all panels on that
  channel. Give each light its own channel if you need them independent.
- **Commands are merged.** The hub sends at most one command per
  `command_interval` and only the latest state of each light, so dragging a
  slider doesn't queue up a backlog.
- **Last command stays on air**, like the app. After a reboot the restored
  state is sent again.

## Running it on a Bluetooth proxy

The component uses ESPHome's shared BLE stack, so it can sit in the same config
as `bluetooth_proxy:`. The radio then splits time between scanning and
advertising, so if the proxy is busy (many devices, active connections) or the
panels are far from it, use a separate ESP32 near the panels.

It sets advertising data directly, so it can't be combined with other
components that advertise: `esp32_ble_beacon`, `esp32_ble_server`, or
`esp32_improv`.

## Disclaimer

This is an independent project, not affiliated with or endorsed by Viltrox or
the makers of the Weeylite app. The protocol was reverse engineered from the
Android app for interoperability and may not cover every panel model.

## License

MIT. See [LICENSE](LICENSE).
