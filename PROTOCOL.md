# Viltrox / Weeylite BLE Protocol Notes

These are notes on the Bluetooth Low Energy (BLE) protocol that the Weeylite (WeeyliteII) app uses to control Viltrox LED light panels. They are not an official specification. They describe what the Android app sends, worked out by decompiling it, and how panels were observed to respond. Field meanings are inferred, and anything not tested against a real panel may be wrong.

## Overview

The protocol uses one-way BLE advertising (no connection required) with commands encoded as iBeacon advertisements.

## Transport Layer

### BLE Configuration

- **Method**: BLE Advertising (Peripheral mode)
- **Format**: iBeacon (Apple manufacturer data, ID: 76)
- **Service UUID**: `fda50693-a4e2-4fb1-afcf-c6eb07647825`
- **Advertising Interval**: 100ms
- **Connection**: Not required (broadcast only)

### iBeacon Parameters

- **Major**: 10 (default)
- **Minor**: 110 (default)
- **TX Power**: -59 dBm (default)
- **UUID**: Dynamically generated from command data

## Protocol Structure

### Command Format

All commands follow this structure:

```txt
[HEAD][CHANNEL_GROUP][MODE][DATA...]-[MORE_DATA...]-[SUFFIX][TAIL][ID][POWER]
```

Every command is exactly 16 bytes and is transmitted as the iBeacon
proximity UUID. The final byte of every command (including power on/off)
is the current power/brightness value.

### Header Fields

- **HEAD**: `0xEF` (fixed header byte)
- **CHANNEL_GROUP**: `(channel << 3) | (group + 1)`
  - Bits 7-3: Channel (1-19, shown as CH1-CH19 in the app)
  - Bits 2-0: Group index + 1 (wire value 1-6, shown as A-F in the app); 0 in power on/off commands (addresses all groups)
- **MODE**: `0x04` for power on; `0x03` for power off; otherwise the lighting mode (0, 1, 2, 5)
- **SUFFIX**: `0x02` immediately before TAIL in lighting commands (`0x22` in power commands)
- **TAIL**: `0xFE` (fixed tail byte)
- **ID**: Rolling packet counter (0-222, wraps back to 0 at 223)
- **POWER**: Brightness percent (0-100), appended as the final byte of every
  command. The app's brightness slider is 0-100 and the value is sent
  unmodified; the panel clamps anything above 100 to 100.

### Rate Limiting

- Minimum 100ms between command transmissions
- Commands sent more frequently are ignored

## Command Types

### Power Control Commands

Power commands set the group bits of CHANNEL_GROUP to 0 (i.e. the byte is
just `channel << 3`) rather than addressing a specific group.

#### Power On

```txt
Format: EF[CH]0400-64FF-FFFF-0000-001122FE[ID][PWR]
Example: EF080400-64FF-FFFF-0000-001122FE0164
```

#### Power Off

```txt
Format: EF[CH]0300-64FF-FFFF-0000-001122FE[ID][PWR]
Example: EF080300-64FF-FFFF-0000-001122FE0264
```

#### Observed Power-On Behavior

Seen on one set of panels:

- Power on restores the panel's last lighting state, including brightness.
  Sending a lighting command only after power on makes the panel show its old
  brightness until that command arrives.
- The brightness bytes in the power-on command don't change this. Sending the
  target brightness in byte 4 (where the app always sends `0x64`) and in the
  final POWER byte still brought the panel up at its old brightness.
- Sending the lighting command first, then power on, brings the panel up at
  the new brightness. It's not confirmed whether a panel that's off stores the
  lighting command until power on, or turns on from the lighting command
  itself.

### Lighting Control Commands

#### HSI Mode (Mode 1)

Hue/Saturation/Intensity control

```txt
Format: EF[CH]0100-[PWR]00-0000-[HUE][SAT]-0102FE[ID][PWR]
Fields:
  - PWR: Brightness percent (0-100)
  - HUE: Hue value (0-359, big-endian 16-bit)
  - SAT: Saturation (0-100)
Example: EF090100-9600-0000-00B4-500102FE0396
```

#### CCT Mode (Mode 0)

Color temperature control

```txt
Format: EF[CH]00[CCT]-[PWR][TYPE]-00[RG]-0000-000002FE[ID][PWR]
Fields:
  - CCT: Color temperature value
  - PWR: Brightness percent (0-100)
  - TYPE: CCT type index (0-9)
  - RG: Red/green tint, offset-encoded as tint + 10: 0x00 = -10,
    0x0A = neutral, 0x14 = +10. The app binds a raw 0-20 SeekBar
    (activity_c_c_t.xml, sb_un_know) directly to this byte, so a wire
    value of 0 is NOT neutral: it applies a full -10 tint.
Example: EF090037-C803-000A-0000-000002FE04C8 (neutral tint)
```

#### RGBW Mode (Mode 5)

Red/Green/Blue/White/Yellow control

```txt
Format: EF[CH]0500-[PWR][R]-[G][B]-[W][Y]-000502FE[ID][PWR]
Fields:
  - PWR: Overall brightness percent (0-100)
  - R,G,B,W,Y: Individual color values (0-100)
Example: EF090500-9664-3219-4B00-000502FE0596
```

#### Scene Mode (Mode 2)

Predefined lighting scenes

```txt
Format: EF[CH]02[SCENE]-[PWR][SPEED]-[SCENE]00-0000-000202FE[ID][PWR]
Fields:
  - SCENE: Scene ID 0-25 (see Scene IDs table)
  - PWR: Brightness percent (0-100)
  - SPEED: Animation speed 1-3 (the app's 3-step slider, 0-2, plus 1)
Example: EF090206-6402-0600-0000-000202FE0664 (Candlelight, speed 2, 100%)

Note: The mode value before the SUFFIX is a big-endian 16-bit field
(e.g. `0002` for scene mode, `0005` for RGBW mode).
```

#### XY Color Mode (Mode 6)

CIE color space positioning

```txt
Format: EF[CH]01[X_HIGH]-[PWR][X_LOW]-[Y_HIGH][Y_LOW]-[HUE][SAT]-0602FE[ID][PWR]
Fields:
  - X_POS: X coordinate (16-bit, split into high/low bytes)
  - Y_POS: Y coordinate (16-bit, split into high/low bytes)
  - PWR: Brightness percent (0-100)
  - HUE: Calculated hue from XY color
  - SAT: Calculated saturation from XY color
```

#### Color Chip / Gel Mode (Mode 7)

Gel presets from the app's "Color Chip" screen (24 Rosco and 16 Lee gels)

```txt
Format: EF[CH][TYPE][CCT]-[PWR]00-[CHIP_TYPE][CHIP_ID]-[HUE][SAT]-0702FE[ID][PWR]
Fields:
  - TYPE: 1 normally; 0 when the app's "No Color" switch is on
  - CCT: The current color temperature value (Kelvin / 100)
  - PWR: Brightness percent (0-100)
  - CHIP_TYPE: Gel brand: 0 = Rosco, 1 = Lee
  - CHIP_ID: The gel's position in that brand's list (or the RG tint
    value when "No Color" is on)
  - HUE, SAT: The gel's approximate color (hue 0-359 big-endian 16-bit,
    saturation 0-100). The app derives it from the chip's display color,
    except for a few chips with hand-tuned values.
Example: EF090137-6400-000B-002A-2C0702FE0864 (Rosco R16 Light Amber at 5500 K, 100%)
```

The full gel list, with display colors and hand-tuned values, is in
`components/viltrox/gels.py`.

#### HSI Alternative Mode (Mode 8)

Alternative HSI implementation

```txt
Format: EF[CH]0199-[PWR]00-0000-[HUE][SAT]-0802FE[ID][PWR]
Fields: Same as HSI Mode but with 0x99 in third position
```

## Data Tables

### Scene IDs

The scene ID is the scene's position in the app's FX list
(`FXActivity` passes the tapped position straight to `BleLed.setSceneID`).
`BleLed` also declares a `SCENE_ID` remapping array, but nothing uses it.

```txt
ID | Scene         | ID | Scene        | ID | Scene
---|---------------|----|--------------|----|-------------
 0 | Flash         |  9 | TV           | 18 | Club 1
 1 | Burst         | 10 | Firework 1   | 19 | Club 2
 2 | Flash Lamp    | 11 | Firework 2   | 20 | Wave Red
 3 | Blink         | 12 | Firework 3   | 21 | Wave Green
 4 | Weld          | 13 | Police       | 22 | Wave Blue
 5 | SOS           | 14 | Ambulance    | 23 | Wave Cyan
 6 | Candlelight   | 15 | Fire Truck   | 24 | Wave Magenta
 7 | Flame         | 16 | RGB Loop     | 25 | Wave Yellow
 8 | CCT Loop      | 17 | Romantic     |    |
```

### CCT Type Values

```txt
Type Index | CCT Value | Description
-----------|-----------|-------------
    0      |    30     | Cool white
    1      |    40     |
    2      |    52     |
    3      |    55     | Daylight
    4      |    60     |
    5      |    70     | Warm white
    6      |    60     |
    7      |    65     |
    8      |    70     |
    9      |    75     | Very warm
```

## Group Management

### Group System

- **Total Groups**: 6 (indexed 0-5, labeled A-F in the app UI)
- **Active Group**: Only one group active per controller instance
- **Independence**: Each group maintains separate state
- **Switching**: Change active group to modify different LED sets

### Group State

Each group maintains:

- Power state (on/off)
- Current mode
- Mode-specific parameters (HSI, RGB, CCT, etc.)
- Scene settings
- Hardware chip configuration

## Channel System

### Purpose

- **Isolation**: Multiple controllers can operate independently
- **Range**: Channels 1-19 (CH1-CH19 in the app UI)
- **Encoding**: Channel number shifted left 3 bits in command header
- **Usage**: Prevents interference between different controller instances

## Implementation Notes

### Timing

- Commands transmitted every 100ms when state changes
- Rate limiting prevents command flooding
- State persistence recommended between sessions

### Error Handling

- No acknowledgment mechanism (one-way communication)
- Retry logic should be implemented at application level
- Invalid commands are ignored by LED panels

### Compatibility

- Protocol based on WeeyliteII app (com.ruitianzhixin.weeylite2)
- Compatible with various LED light panel models
- May require hardware-specific scene ID mappings

## Security Considerations

- **No Authentication**: Protocol has no security mechanisms
- **Open Protocol**: Any device can send commands
- **Interference**: Multiple controllers can interfere with each other
- **Recommendation**: Use in controlled environments

## Example Implementation

See `components/viltrox` for an ESPHome component that implements this protocol on an ESP32 and exposes panels to Home Assistant as lights.

**macOS cannot transmit this protocol directly**: modern macOS silently strips manufacturer data (including iBeacon frames via the private `kCBAdvDataAppleBeaconKey`) from userspace BLE advertisements. CoreBluetooth and `bluetoothd` report success, but nothing is emitted over the air (verified with an Android BLE scanner, 2026-07).

---

**Note**: These notes are based on reverse engineering the WeeyliteII Android app and testing against a small number of panels. They may not cover all edge cases, other panel models, or vendor-specific extensions.
