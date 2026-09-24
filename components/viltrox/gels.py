"""The app's color chip (gel) presets, from ColorChipViewModel.

Each gel is sent with its brand, its position in that brand's list, and an
approximate hue/saturation. The app derives hue/saturation from the chip's
display color (Android colorToHSV, truncated), except for a few chips where it
uses hand-tuned values instead.
"""

import colorsys

# (code, name, display color as a Java ARGB int)
_ROSCO = [
    ("E128", "Bright Pink", -2736749),
    ("E152", "Pale Gold", -12105),
    ("E156", "Chocolate", -5536924),
    ("E322", "Soft Green", -16731737),
    ("E723", "Virgin Blue", -16764425),
    ("E724", "Ocean Blue", -13973505),
    ("G220", "Pink Magenta", -3014572),
    ("G342", "Light Honey", -671871),
    ("G720", "Light Steel Blue", -7614492),
    ("G910", "Alice Blue", -11969308),
    ("G985", "Ripe Plum", -11796357),
    ("R16", "Light Amber", -1981315),
    ("R17", "Light Flame", -27829),
    ("R37", "Pale Rose Pink", -544309),
    ("R47", "Light Rose Purple", -3387719),
    ("R69", "Brilliant Blue", -16735241),
    ("R92", "Turquoise", -15096841),
    ("R93", "Blue Green", -15686752),
    ("R388", "Gaslight Green", -3080255),
    ("R9211", "Soft Amber", -662834),
    ("P1002", "Bastard Amber", -16203),
    ("P3203", "3/4 Blue CTB", -4931841),
    ("P5590", "Cyan", -11765249),
    ("P4200", "Deep Purple", -14352272),
]
_LEE = [
    ("E002", "Rose Pink", -1803064),
    ("E007", "Pale Yellow", -332347),
    ("E008", "Dark Salmon", -30899),
    ("E036", "Medium Pink", -613445),
    ("E110", "Middle Rose", -610103),
    ("E115", "Peacock Blue", -16730165),
    ("E117", "Steel Blue", -12400149),
    ("E131", "Marine Blue", -16728889),
    ("E134", "Golden Amber", -26809),
    ("E148", "Bright Rose", -1816212),
    ("E153", "Pale Salmon", -81209),
    ("E165", "Daylight Blue", -16735770),
    ("E243", "Fluorescent", -7683416),
    ("E244", "Plus Green", -3152720),
    ("E327", "Forest Green", -16760009),
    ("E344", "Violet", -16744992),
]
# index -> (hue, saturation) the app uses instead of the display color
_ROSCO_OVERRIDES = {1: (19, 64), 7: (20, 77), 13: (10, 61), 19: (23, 63), 20: (14, 56)}
_LEE_OVERRIDES = {3: (358, 50), 4: (357, 45), 10: (5, 50), 13: (76, 68)}


def _hue_sat(argb):
    r, g, b = ((argb >> shift) & 0xFF for shift in (16, 8, 0))
    h, s, _ = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    return int(h * 360), int(s * 100)


def _gels(brand, brand_id, chips, overrides):
    return [
        {
            "label": f"{brand} {code} {name}",
            "brand": brand_id,
            "index": index,
            "hue_sat": overrides.get(index) or _hue_sat(argb),
        }
        for index, (code, name, argb) in enumerate(chips)
    ]


GELS = _gels("Rosco", 0, _ROSCO, _ROSCO_OVERRIDES) + _gels("Lee", 1, _LEE, _LEE_OVERRIDES)
