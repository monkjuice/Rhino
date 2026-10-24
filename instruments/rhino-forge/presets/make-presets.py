#!/usr/bin/env python3
"""Write Forge's factory presets into presets/Factory.

Each preset is written as only what differs from a fresh Forge: the patch is a
handful of PARAM lines under a <RhinoForgeState>, and Forge fills in everything
else at its default when it opens the file (Processor::migrated). So a preset
here reads as a description of the sound rather than as three hundred numbers.

The bank in Documents/Rhino Forge/Presets is given a copy of this folder the
first time Forge makes it, and the browser's Restore puts back any that went
missing. Both copy Factory/ with its subfolders, so a preset written with a
folder (the Drum n Bass set) shows in the bank as Factory/<folder>. Run from
anywhere; the files land in Factory/ beside this script.

    python instruments/rhino-forge/presets/make-presets.py

Every value is the parameter's own plain value, the one a preset stores: Hz,
seconds, semitones, a level as linear gain, a choice as its index. A routing's
depth is in the destination's own 0..1, the space the matrix adds in. The
indices below mirror the lists in core/ (ForgeMatrix.h, ForgeWarp.h,
ForgeFilter.h, ForgeFx.h, ForgeNoise.h), which are append-only, so a number
written here keeps its meaning.
"""

import os
from xml.sax.saxutils import quoteattr

FORMAT_VERSION = 3
AUTHOR = "Rhino Forge"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "Factory")

# The built-in table's ten shapes, as a POSITION.
WAVE = {name: i / 9.0 for i, name in enumerate(
    ["sine", "tri", "trap", "sqr", "pulse", "thin", "saw", "hump", "organ", "vox"])}

# An OSC mode on oscillator A reads B ("next" round the ring A-B-C) and a
# "_prev" one reads C; on B they read C and A.
WARP = {"off": 0, "sync": 1, "bend+": 2, "bend-": 3, "bend+-": 4, "pwm": 5, "asym": 6, "flip": 7,
        "mirror": 8, "quantize": 9, "oddeven": 10, "lpf": 11, "hpf": 12, "bpf": 13, "tube": 14,
        "softclip": 15, "hardclip": 16, "diode": 17, "linfold": 18, "sinefold": 19, "rectify": 20,
        "tapesat": 21, "pd_osc": 22, "pd_sub": 23, "pd_noise": 24, "pd_self": 25, "fm_osc": 26,
        "fm_sub": 27, "fm_noise": 28, "fm_exp_osc": 29, "fm_exp_sub": 30, "fm_exp_noise": 31,
        "am_osc": 32, "am_sub": 33, "am_noise": 34, "rm_osc": 35, "rm_sub": 36, "rm_noise": 37,
        "pd_osc_prev": 38, "fm_osc_prev": 39, "fm_exp_osc_prev": 40, "am_osc_prev": 41,
        "rm_osc_prev": 42}

FILTER = {"low": 0, "high": 1, "band": 2, "notch": 3, "peak": 4, "lp+hp": 5, "lp+bp": 6, "lp+pk": 7,
          "lp+nt": 8, "hp+bp": 9, "hp+pk": 10, "hp+nt": 11, "bp+pk": 12, "bp+nt": 13, "pk+pk": 14,
          "pk+nt": 15, "nt+nt": 16, "lp-bp-hp": 17, "lp-pk-hp": 18, "lp-nt-hp": 19, "bp-pk-nt": 20,
          "ladder": 21, "acid": 22, "dirty": 23, "ems": 24, "comb": 25, "flanger": 26, "phaser": 27,
          "formant": 28, "ringmod": 29, "sh": 30, "diffusor": 31, "scream": 32, "reverb": 33}

FX = {"reverb": 1, "delay": 2, "chorus": 3, "dist": 4, "eq": 5, "filter": 6, "comp": 7, "phaser": 8,
      "flanger": 9}
NOISE = {"white": 0, "pink": 1, "brown": 2, "tape": 10, "vinyl": 16, "wind": 17}
SUB = {"sine": 0, "rect": 1, "tri": 2, "saw": 3, "sqr": 4, "pulse": 5}
LFO_SHAPE = {"sine": 0, "tri": 1, "saw": 2, "sqr": 3}
LFO_MODE = {"trig": 0, "env": 1, "free": 2}
LFO_TYPE = {"normal": 0, "path": 1, "sh": 2}
LFO_DIRECTION = {"forward": 0, "reverse": 1, "pingpong": 2}
LFO_DIVISION = {"1/1": 0, "1/2": 1, "1/4": 2, "1/4T": 3, "1/8": 4, "1/8T": 5, "1/16": 6}
ARP_DIVISION = {"1/1": 0, "1/2": 1, "1/4": 2, "1/8": 3, "1/16": 4, "1/32": 5}
FX_DIVISION = {"1/32": 0, "1/16": 1, "1/8T": 2, "1/8": 3, "1/4T": 4, "1/4": 5, "1/2": 6, "1/1": 7}

SOURCE = {"env1": 1, "env2": 2, "env3": 3, "env4": 4, "lfo1": 5, "lfo2": 6, "lfo3": 7, "lfo4": 8,
          "lfo5": 9, "lfo6": 10, "velocity": 11, "note": 12, "macro1": 13, "macro2": 14, "macro3": 15,
          "macro4": 16, "modwheel": 21}


def destination(name):
    """A matrix destination by parameter id: the named list, the racks' knobs
    and mixes, what was appended after them, the racks' levels, then FILTER 2,
    in ForgeMatrix.h's order."""
    named = ["", "oscAPosition", "oscALevel", "oscAPan", "oscADetune", "oscASemitone",
             "oscBPosition", "oscBLevel", "oscBPan", "oscBDetune", "oscBSemitone",
             "oscCPosition", "oscCLevel", "oscCPan", "oscCDetune", "oscCSemitone",
             "subLevel", "noiseLevel", "cutoff", "resonance", "drive", "subPan", "noisePan",
             "filterPan", "filterMix", "filterLevel"]
    if name in named:
        return named.index(name)
    base = len(named)
    # A rack slot's id is fx<rack>s<slot><control>: MAIN is rack 1.
    rack_slot = name.startswith("fx") and name[3] == "s"
    if rack_slot and not name.endswith("Level"):
        rack, slot = int(name[2]) - 1, int(name[4]) - 1
        control = 6 if name.endswith("Mix") else int(name[-1]) - 1
        return base + (rack * 8 + slot) * 7 + control
    base += 3 * 8 * 7
    appended = ["oscAWarp1", "oscAWarp2", "oscBWarp1", "oscBWarp2", "oscCWarp1", "oscCWarp2",
                "filterFreq", "noiseTone", "noiseStereo"]
    for env in range(1, 5):
        appended += [f"env{env}{part}" for part in ("Attack", "Decay", "Sustain", "Release")]
    appended += ["oscAOctave", "oscAFine", "oscBOctave", "oscBFine", "oscCOctave", "oscCFine"]
    if name in appended:
        return base + appended.index(name)
    base += len(appended)
    if rack_slot:
        return base + (int(name[2]) - 1) * 8 + int(name[4]) - 1
    base += 3 * 8
    filter2 = ["filter2Cutoff", "filter2Resonance", "filter2Drive", "filter2Freq", "filter2Pan",
               "filter2Mix", "filter2Level"]
    return base + filter2.index(name)


# What every factory preset starts from: one oscillator, no sub, an open
# filter, an empty matrix. The defaults a fresh Forge opens on are a demo of
# three oscillators; a preset should only have what it uses.
BASE = {
    "oscBEnable": 0, "oscCEnable": 0, "subEnable": 0, "noiseEnable": 0,
    "oscAUnison": 1, "oscADetune": 0.0, "oscBUnison": 1, "oscBDetune": 0.0, "oscBSemitone": 0,
    "oscCUnison": 1, "oscCDetune": 0.0, "oscALevel": 0.7,
    "cutoff": 18000.0, "resonance": 0.0, "drive": 0.0,
    "glide": 0.0, "output": 0.8,
}


def fx_mode(choice, count):
    return choice / (count - 1)


def preset(name, style, notes, params, routes=(), fx=(), macros=None, folder=None):
    """One preset, written into Factory/, or into Factory/<folder>."""
    values = dict(BASE)
    values.update(params)
    for slot in range(30):
        values.setdefault(f"mod{slot + 1}Source", 0)
        values.setdefault(f"mod{slot + 1}Dest", 0)
        values.setdefault(f"mod{slot + 1}Depth", 0.0)
    for slot, (source, dest, depth, *rest) in enumerate(routes):
        values[f"mod{slot + 1}Source"] = SOURCE[source]
        values[f"mod{slot + 1}Dest"] = destination(dest)
        values[f"mod{slot + 1}Depth"] = depth
        values[f"mod{slot + 1}Bipolar"] = 1 if rest and rest[0] else 0
    for slot, (kind, mode_a, mode_b, knobs, mix) in enumerate(fx):
        prefix = f"fx1s{slot + 1}"
        values[prefix + "Type"] = FX[kind]
        values[prefix + "ModeA"] = mode_a
        values[prefix + "ModeB"] = mode_b
        for i, knob in enumerate(knobs):
            values[f"{prefix}Knob{i + 1}"] = knob
        values[prefix + "Mix"] = mix

    properties = {"presetName": name, "presetAuthor": AUTHOR, "presetStyle": style, "presetNotes": notes}
    for i, label in enumerate(macros or []):
        if label:
            properties[f"macroName_{i + 1}"] = label.upper()
    attributes = " ".join(f"{key}={quoteattr(str(value))}" for key, value in properties.items())
    lines = [f'<?xml version="1.0" encoding="UTF-8"?>',
             f'<RhinoForgePreset formatVersion="{FORMAT_VERSION}" name={quoteattr(name)} '
             f'author={quoteattr(AUTHOR)} style={quoteattr(style)}>',
             f'  <RhinoForgeState {attributes}>']
    for key in sorted(values):
        lines.append(f'    <PARAM id="{key}" value="{float(values[key]):.6g}"/>')
    lines += ["  </RhinoForgeState>", "</RhinoForgePreset>", ""]
    out = OUT if folder is None else os.path.join(OUT, folder)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, name + ".forgepreset"), "w", encoding="utf-8", newline="\n") as file:
        file.write("\n".join(lines))


def env(n, attack, decay, sustain, release):
    return {f"env{n}Attack": attack, f"env{n}Decay": decay, f"env{n}Sustain": sustain, f"env{n}Release": release}


def lfo(n, shape="sine", rate=None, division=None, mode="trig", kind="normal", direction="forward",
        smooth=None):
    """One LFO. `kind` is its TYPE (normal, path or sh, a sample and hold whose
    steps are the table's eight columns per cycle) and `direction` how a shape
    is read; both are written only when they leave the default, as SMOOTH is."""
    values = {f"lfo{n}Shape": LFO_SHAPE[shape], f"lfo{n}Mode": LFO_MODE[mode]}
    if kind != "normal":
        values[f"lfo{n}Type"] = LFO_TYPE[kind]
    if direction != "forward":
        values[f"lfo{n}Direction"] = LFO_DIRECTION[direction]
    if smooth is not None:
        values[f"lfo{n}Smooth"] = smooth
    if division is not None:
        values.update({f"lfo{n}RateUnit": 1, f"lfo{n}Division": LFO_DIVISION[division]})
    else:
        values.update({f"lfo{n}RateUnit": 0, f"lfo{n}Rate": rate})
    return values


def merged(*parts):
    out = {}
    for part in parts:
        out.update(part)
    return out


REVERB_PLATE = ("reverb", 0.0, 0.0, [0.5, 0.08, 0.5, 0.85, 0.25, 0.8], 0.22)
REVERB_HALL = ("reverb", 1.0, 0.0, [0.78, 0.12, 0.45, 1.0, 0.3, 0.75], 0.32)
CHORUS = ("chorus", 0.0, 0.0, [0.22, 0.3, 0.5, 0.35, 0.15, 0.85], 0.35)


def delay(division, feedback, mix, ping_pong=True):
    return ("delay", fx_mode(1 if ping_pong else 0, 2), fx_mode(1, 2),
            [FX_DIVISION[division] / 8.0, 0.5, feedback, 0.62, 0.4, 0.5], mix)


def write_all():
    os.makedirs(OUT, exist_ok=True)

    # --- Bass ---------------------------------------------------------------
    preset("Sub Foundation", "Bass",
           "A sine an octave down with the sub under it. Mono and gliding; macro 1 adds the drive.",
           merged({"oscAPosition": WAVE["sine"], "oscAOctave": -1, "oscALevel": 0.85,
                   "subEnable": 1, "subWave": SUB["sine"], "subOctave": 0, "subLevel": 0.45,
                   "cutoff": 1400.0, "drive": 0.0, "mono": 1, "legato": 1, "glide": 0.05},
                  env(1, 0.003, 0.4, 0.9, 0.12)),
           routes=[("macro1", "drive", 0.6), ("macro1", "cutoff", -0.12)],
           macros=["Drive"])

    preset("Wide Reese", "Bass",
           "Two stacks of detuned saws nine cents apart, through a ladder. Macro 1 opens the filter, "
           "macro 2 widens the stack.",
           merged({"oscAPosition": WAVE["saw"], "oscAOctave": -1, "oscAUnison": 5, "oscADetune": 0.35,
                   "oscABlend": 0.75, "oscALevel": 0.7,
                   "oscBEnable": 1, "oscBPosition": WAVE["saw"], "oscBOctave": -1, "oscBFine": 9,
                   "oscBUnison": 3, "oscBDetune": 0.25, "oscBLevel": 0.55,
                   "subEnable": 1, "subWave": SUB["sine"], "subLevel": 0.3,
                   "filterType": FILTER["ladder"], "cutoff": 900.0, "resonance": 0.2, "drive": 0.25,
                   "mono": 1, "legato": 1, "glide": 0.06},
                  env(1, 0.005, 0.5, 0.95, 0.18), lfo(1, "sine", rate=0.18, mode="free")),
           routes=[("lfo1", "cutoff", 0.05), ("macro1", "cutoff", 0.3), ("macro2", "oscADetune", 0.4),
                   ("macro2", "oscBDetune", 0.4)],
           fx=[("dist", fx_mode(1, 8), fx_mode(4, 5), [0.25, 0.75, 0.2, 0.5, 0.5, 0.5], 0.4)],
           macros=["Filter", "Width"])

    preset("Acid Line", "Bass",
           "A single saw through the acid filter, its cutoff struck by ENV 2 on every note. Play it legato "
           "to slide; macro 1 is the accent.",
           merged({"oscAPosition": WAVE["saw"], "oscAOctave": -1, "oscALevel": 0.75,
                   "filterType": FILTER["acid"], "cutoff": 380.0, "resonance": 0.72, "drive": 0.3,
                   "filterFreq": 0.45, "mono": 1, "legato": 1, "glide": 0.07},
                  env(1, 0.002, 0.3, 0.7, 0.08), env(2, 0.001, 0.22, 0.0, 0.1)),
           routes=[("env2", "cutoff", 0.32), ("macro1", "cutoff", 0.15), ("macro1", "resonance", 0.15),
                   ("velocity", "cutoff", 0.1)],
           fx=[delay("1/8", 0.35, 0.18)],
           macros=["Accent"])

    preset("Wobble Engine", "Bass",
           "A bent pulse whose filter and warp an eighth-note LFO keeps moving. Macro 1 is how hard it wobbles.",
           merged({"oscAPosition": WAVE["pulse"], "oscAOctave": -1, "oscAUnison": 3, "oscADetune": 0.15,
                   "oscAWarp1Mode": WARP["bend+-"], "oscAWarp1": 0.5,
                   "subEnable": 1, "subWave": SUB["sine"], "subLevel": 0.35,
                   "filterType": FILTER["lp-bp-hp"], "cutoff": 700.0, "resonance": 0.35, "filterFreq": 0.15,
                   "drive": 0.35, "mono": 1, "legato": 1},
                  env(1, 0.004, 0.3, 1.0, 0.12), lfo(1, "sine", division="1/8")),
           routes=[("lfo1", "cutoff", 0.2), ("lfo1", "oscAWarp1", 0.2), ("macro1", "cutoff", 0.15)],
           fx=[("dist", fx_mode(0, 8), 0.0, [0.35, 0.5, 0.3, 0.5, 0.5, 0.5], 0.5),
               ("comp", 0.0, 0.0, [0.55, 0.4, 0.2, 0.35, 0.15, 0.3], 1.0)],
           macros=["Wobble"])

    # --- Lead ---------------------------------------------------------------
    preset("Supersaw Lead", "Lead",
           "Nine saws spread wide with a second stack an octave up. Chorus, delay and a plate behind it.",
           merged({"oscAPosition": WAVE["saw"], "oscAUnison": 9, "oscADetune": 0.5, "oscABlend": 0.7,
                   "oscALevel": 0.6, "oscBEnable": 1, "oscBPosition": WAVE["saw"], "oscBOctave": 1,
                   "oscBUnison": 5, "oscBDetune": 0.4, "oscBLevel": 0.3,
                   "cutoff": 7500.0, "resonance": 0.1},
                  env(1, 0.008, 0.4, 0.85, 0.35), lfo(1, "sine", rate=5.2)),
           routes=[("velocity", "cutoff", 0.1)],
           fx=[CHORUS, delay("1/8", 0.3, 0.18), REVERB_PLATE])

    preset("Sync Lead", "Lead",
           "A hard-synced saw whose sync ratio ENV 2 sweeps down on every note. Mono with a quick glide.",
           merged({"oscAPosition": WAVE["saw"], "oscAWarp1Mode": WARP["sync"], "oscAWarp1": 0.2,
                   "oscAUnison": 2, "oscADetune": 0.12, "oscALevel": 0.7,
                   "cutoff": 9000.0, "mono": 1, "legato": 1, "glide": 0.035, "output": 1.0},
                  env(1, 0.003, 0.3, 0.9, 0.2), env(2, 0.001, 0.45, 0.25, 0.3)),
           routes=[("env2", "oscAWarp1", 0.45), ("modwheel", "oscAWarp1", 0.3)],
           fx=[delay("1/4", 0.3, 0.2), REVERB_PLATE])

    preset("Square Lead", "Lead",
           "Two squares a few cents apart, a little resonance and a gentle vibrato.",
           merged({"oscAPosition": WAVE["sqr"], "oscALevel": 0.6,
                   "oscBEnable": 1, "oscBPosition": WAVE["sqr"], "oscBFine": 7, "oscBLevel": 0.45,
                   "cutoff": 3600.0, "resonance": 0.22, "mono": 1, "legato": 1, "glide": 0.03},
                  env(1, 0.005, 0.25, 0.85, 0.2), lfo(1, "sine", rate=5.5)),
           routes=[("lfo1", "oscASemitone", 0.004), ("lfo1", "oscBSemitone", 0.004)],
           fx=[delay("1/8", 0.35, 0.2)])

    # --- Keys ---------------------------------------------------------------
    preset("Glass Keys", "Keys",
           "A sine phase-modulated by its octave, the modulation dying away as the note does. Harder notes "
           "ring brighter.",
           merged({"oscAPosition": WAVE["sine"], "oscALevel": 0.75,
                   "oscAWarp1Mode": WARP["pd_osc"], "oscAWarp1": 0.25,
                   "oscBEnable": 1, "oscBPosition": WAVE["sine"], "oscBOctave": 1, "oscBLevel": 0.12},
                  env(1, 0.002, 1.4, 0.25, 0.7), env(2, 0.001, 0.6, 0.1, 0.5)),
           routes=[("env2", "oscAWarp1", 0.4), ("velocity", "oscAWarp1", 0.25)],
           fx=[CHORUS, REVERB_PLATE])

    preset("Drawbar Organ", "Keys",
           "The organ frame with an octave sine over it, no envelope to speak of, and a chorus for the "
           "spinning speaker.",
           merged({"oscAPosition": WAVE["organ"], "oscALevel": 0.6,
                   "oscBEnable": 1, "oscBPosition": WAVE["sine"], "oscBOctave": 1, "oscBLevel": 0.3},
                  env(1, 0.004, 0.1, 1.0, 0.07)),
           fx=[("chorus", 0.0, 0.0, [0.45, 0.2, 0.4, 0.55, 0.1, 0.9], 0.45), REVERB_PLATE])

    preset("Soft Pluck", "Keys",
           "A saw and a triangle plucked by a closing filter. Macro 1 lengthens the pluck.",
           merged({"oscAPosition": WAVE["saw"], "oscALevel": 0.6, "oscAUnison": 3, "oscADetune": 0.15,
                   "oscBEnable": 1, "oscBPosition": WAVE["tri"], "oscBOctave": 1, "oscBLevel": 0.35,
                   "cutoff": 500.0, "resonance": 0.2},
                  env(1, 0.001, 0.7, 0.0, 0.35), env(2, 0.001, 0.32, 0.0, 0.3)),
           routes=[("env2", "cutoff", 0.38), ("velocity", "cutoff", 0.08), ("macro1", "env2Decay", 0.3),
                   ("macro1", "env1Decay", 0.3)],
           fx=[delay("1/8", 0.3, 0.15), REVERB_PLATE],
           macros=["Length"])

    # --- Pad ----------------------------------------------------------------
    preset("Warm Pad", "Pad",
           "Detuned saws an octave apart through a slowly breathing ladder, into chorus and a hall.",
           merged({"oscAPosition": WAVE["saw"], "oscAUnison": 5, "oscADetune": 0.3, "oscALevel": 0.55,
                   "oscBEnable": 1, "oscBPosition": WAVE["saw"], "oscBOctave": -1, "oscBUnison": 3,
                   "oscBDetune": 0.25, "oscBLevel": 0.35,
                   "filterType": FILTER["ladder"], "cutoff": 1700.0, "resonance": 0.15},
                  env(1, 0.9, 1.5, 0.8, 1.8), lfo(1, "sine", rate=0.15, mode="free")),
           routes=[("lfo1", "cutoff", 0.06), ("modwheel", "cutoff", 0.25)],
           fx=[CHORUS, REVERB_HALL])

    preset("Glass Pad", "Pad",
           "Two LFOs drift the table position and the pan; the voice formant underneath gives it a mouth.",
           merged({"oscAPosition": 0.75, "oscAUnison": 4, "oscADetune": 0.2, "oscALevel": 0.55,
                   "oscBEnable": 1, "oscBPosition": WAVE["sine"], "oscBOctave": 1, "oscBLevel": 0.25,
                   "cutoff": 6000.0},
                  env(1, 1.2, 2.0, 0.7, 2.5), lfo(1, "tri", rate=0.09, mode="free"),
                  lfo(2, "sine", rate=0.13, mode="free")),
           routes=[("lfo1", "oscAPosition", 0.2), ("lfo2", "oscAPan", 0.35)],
           fx=[delay("1/4", 0.4, 0.2), REVERB_HALL])

    preset("Dark Drone", "Pad",
           "A thin pulse whose width an LFO slowly works, over wind noise, under a low ladder.",
           merged({"oscAPosition": WAVE["thin"], "oscAOctave": -1, "oscAUnison": 3, "oscADetune": 0.2,
                   "oscAWarp1Mode": WARP["pwm"], "oscAWarp1": 0.4, "oscALevel": 0.6,
                   "noiseEnable": 1, "noiseSource": NOISE["wind"], "noiseLevel": 0.25,
                   "filterType": FILTER["ladder"], "cutoff": 650.0, "resonance": 0.3},
                  env(1, 2.0, 2.5, 0.85, 3.5), lfo(1, "tri", rate=0.07, mode="free")),
           routes=[("lfo1", "oscAWarp1", 0.35), ("lfo1", "cutoff", 0.05)],
           fx=[REVERB_HALL])

    # --- Percussion ---------------------------------------------------------
    preset("Deep Kick", "Percussion",
           "A sine whose pitch drops an octave in a few milliseconds, with a click of noise on the front. "
           "Play it around C1.",
           merged({"oscAPosition": WAVE["sine"], "oscALevel": 0.95,
                   "noiseEnable": 1, "noiseSource": NOISE["white"], "noiseLevel": 0.0},
                  env(1, 0.001, 0.38, 0.0, 0.12), env(2, 0.001, 0.07, 0.0, 0.05), env(3, 0.001, 0.012, 0.0, 0.01)),
           routes=[("env2", "oscASemitone", 0.5), ("env3", "noiseLevel", 0.6)],
           fx=[("comp", 0.0, 0.0, [0.5, 0.45, 0.25, 0.3, 0.2, 0.3], 1.0)])

    preset("Snare Snap", "Percussion",
           "White noise through a band, over a triangle that falls in pitch. The plate gives it a room.",
           merged({"oscAPosition": WAVE["tri"], "oscALevel": 0.55,
                   "noiseEnable": 1, "noiseSource": NOISE["white"], "noiseLevel": 0.7,
                   "filterType": FILTER["band"], "cutoff": 2600.0, "resonance": 0.15},
                  env(1, 0.001, 0.2, 0.0, 0.14), env(2, 0.001, 0.05, 0.0, 0.05)),
           routes=[("env2", "oscASemitone", 0.35)],
           fx=[REVERB_PLATE])

    # --- Sequence -----------------------------------------------------------
    preset("Arp Pulse", "Sequence",
           "Hold a chord: the arpeggiator walks it up two octaves in sixteenths, a narrow pulse plucked by "
           "the filter, into a dotted-feeling delay.",
           merged({"oscAPosition": WAVE["pulse"], "oscALevel": 0.6, "oscAUnison": 2, "oscADetune": 0.1,
                   "cutoff": 900.0, "resonance": 0.35,
                   "arpEnable": 1, "arpShape": 0, "arpRateUnit": 1, "arpDivision": ARP_DIVISION["1/16"],
                   "arpRange": 2, "arpGate": 0.6},
                  env(1, 0.001, 0.25, 0.0, 0.15), env(2, 0.001, 0.18, 0.0, 0.1)),
           routes=[("env2", "cutoff", 0.3), ("velocity", "cutoff", 0.1)],
           fx=[delay("1/8T", 0.4, 0.25), REVERB_PLATE])

    # --- SFX ----------------------------------------------------------------
    preset("Noise Riser", "SFX",
           "Hold a note: white noise and a saw stack rise through a resonant band for four seconds.",
           merged({"oscAPosition": WAVE["saw"], "oscAUnison": 7, "oscADetune": 0.6, "oscALevel": 0.35,
                   "noiseEnable": 1, "noiseSource": NOISE["white"], "noiseLevel": 0.6,
                   "filterType": FILTER["band"], "cutoff": 300.0, "resonance": 0.55},
                  env(1, 0.5, 0.5, 1.0, 1.5), env(2, 4.0, 0.1, 1.0, 1.0)),
           routes=[("env2", "cutoff", 0.55), ("env2", "oscASemitone", 0.5)],
           fx=[REVERB_HALL])

    # --- Template -----------------------------------------------------------
    preset("Init Saw", "Template",
           "One saw, the filter open, a plain envelope: somewhere to start that is not silence.",
           merged({"oscAPosition": WAVE["saw"], "oscALevel": 0.7}, env(1, 0.005, 0.3, 0.8, 0.25)))

    write_drum_and_bass()


# --- Drum n Bass --------------------------------------------------------------
#
# A folder of its own inside the factory set: dubstep growls, leads and subs.
# The growls are mid-range patches meant to sit over one of the subs, the way a
# drum and bass mix layers the two, so each keeps only a little sub of its own
# and the sub oscillator mostly serves as the modulator of a PD or RM warp.
DNB = "Drum n Bass"

# The two pans mark a patch as written since the mixer: Processor::migrated
# reads a state that names neither as being in the older, 3 dB hotter scale and
# raises SUB and NOISE by that, which a sub patch cannot afford.
MIXED = {"subPan": 0.0, "noisePan": 0.0}

# The main rack runs on everything, the sub included, so the distortion keeps
# a low pass after the shaper and leaves part of the signal dry; and a
# compressor with automatic make-up (5:1 from -27 dB) holds the growls at one
# level. The formant and acid filters pass a narrow band of what they are fed,
# so the growls run their oscillator, their filter drive and the output hot to
# come out as loud as the other basses.
COMP_GLUE = ("comp", 0.0, fx_mode(1, 2), [0.55, 0.4, 0.3, 0.4, 0.0, 0.3], 1.0)


def write_drum_and_bass():
    # --- Growls -------------------------------------------------------------
    preset("Talking Growl", "Bass",
           "The organ frame chewed towards the voice frame, phase-distorted by a sub an octave down, "
           "then spoken through the formant filter: LFO 1 moves the mouth between E and O every "
           "beat, LFO 2 works the table and the grit in eighths. Layer a sub under it. Macro 1 is "
           "the grit, macro 2 the vowel.",
           merged(MIXED,
                  {"oscAPosition": WAVE["organ"], "oscALevel": 1.0,
                   "oscAWarp1Mode": WARP["pd_sub"], "oscAWarp1": 0.3,
                   "oscAWarp2Mode": WARP["tube"], "oscAWarp2": 0.35,
                   "subEnable": 1, "subWave": SUB["sine"], "subOctave": 0, "subLevel": 0.1, "routeSub": 0,
                   "filterType": FILTER["formant"], "cutoff": 735.0, "resonance": 0.5, "drive": 0.6,
                   "filterFreq": 0.42, "filterMix": 0.75,
                   "mono": 1, "legato": 1, "glide": 0.03, "output": 1.0},
                  env(1, 0.002, 0.3, 1.0, 0.08),
                  lfo(1, "sine", division="1/4"), lfo(2, "sine", division="1/8")),
           routes=[("lfo1", "cutoff", 0.22), ("lfo2", "oscAPosition", 0.11), ("lfo2", "oscAWarp1", 0.15),
                   ("macro1", "oscAWarp1", 0.4), ("macro2", "cutoff", 0.3), ("velocity", "drive", 0.2)],
           fx=[("dist", fx_mode(0, 8), fx_mode(4, 5), [0.5, 0.75, 0.3, 0.5, 0.5, 0.5], 0.75), COMP_GLUE],
           macros=["Grit", "Vowel"], folder=DNB)

    preset("Yoi Growl", "Bass",
           "A saw fed back into itself through the acid filter, whose cutoff a falling ramp drops "
           "every beat: the yoi. The formant filter sits in series and moves its mouth with it, and "
           "a phaser turns slowly behind. Macro 1 lifts the sweep, macro 2 the feedback.",
           merged(MIXED,
                  {"oscAPosition": WAVE["saw"], "oscAUnison": 2, "oscADetune": 0.08, "oscALevel": 1.0,
                   "oscAWarp1Mode": WARP["pd_self"], "oscAWarp1": 0.45,
                   "subEnable": 1, "subWave": SUB["sine"], "subOctave": 0, "subLevel": 0.1, "routeSub": 0,
                   "filterType": FILTER["acid"], "cutoff": 770.0, "resonance": 0.68, "drive": 0.5,
                   "filterFreq": 0.5, "filterSerial": 1,
                   "filter2Type": FILTER["formant"], "filter2Cutoff": 735.0, "filter2Resonance": 0.35,
                   "filter2Freq": 0.5, "filter2Mix": 0.6,
                   "mono": 1, "legato": 1, "glide": 0.04, "output": 1.0},
                  env(1, 0.002, 0.3, 1.0, 0.1),
                  lfo(1, "saw", division="1/4", direction="reverse")),
           routes=[("lfo1", "cutoff", 0.2), ("lfo1", "oscAWarp1", 0.2), ("lfo1", "filter2Cutoff", 0.15),
                   ("macro1", "cutoff", 0.25), ("macro2", "oscAWarp1", 0.4)],
           fx=[("dist", fx_mode(2, 8), fx_mode(4, 5), [0.4, 0.7, 0.3, 0.5, 0.5, 0.5], 0.65),
               ("phaser", 0.0, fx_mode(2, 4), [0.15, 0.5, 0.45, 0.5, 0.5, 0.6], 0.35), COMP_GLUE],
           macros=["Yoi", "Feedback"], folder=DNB)

    preset("Tearout Growl", "Bass",
           "A pulse ring-modulated by the sub and folded, over a synced saw, through a filter that "
           "LFO 1 morphs from low pass to high pass and back in eighths while a sample-and-hold "
           "steps the cutoff eight times a bar. Fold distortion on the way out. Mid-range by design, "
           "with almost nothing under 120 Hz: put a sub from this folder beneath it. Macro 1 is the "
           "tear, macro 2 the sync.",
           merged(MIXED,
                  {"oscAPosition": WAVE["pulse"], "oscALevel": 0.85,
                   "oscAWarp1Mode": WARP["rm_sub"], "oscAWarp1": 0.4,
                   "oscAWarp2Mode": WARP["linfold"], "oscAWarp2": 0.3,
                   "oscBEnable": 1, "oscBPosition": WAVE["saw"], "oscBFine": -7, "oscBLevel": 0.45,
                   "oscBWarp1Mode": WARP["sync"], "oscBWarp1": 0.25,
                   "subEnable": 1, "subWave": SUB["sine"], "subOctave": 0, "subLevel": 0.15, "routeSub": 0,
                   "filterType": FILTER["lp-bp-hp"], "cutoff": 600.0, "resonance": 0.55, "drive": 0.5,
                   "filterFreq": 0.35, "mono": 1, "legato": 1, "glide": 0.03, "output": 1.0},
                  env(1, 0.002, 0.3, 1.0, 0.1),
                  lfo(1, "tri", division="1/8"), lfo(2, kind="sh", division="1/1", smooth=0.2)),
           routes=[("lfo1", "filterFreq", 0.35), ("lfo1", "oscAWarp1", 0.3), ("lfo1", "oscBWarp1", 0.25),
                   ("lfo2", "cutoff", 0.15), ("lfo2", "oscAPosition", 0.1),
                   ("macro1", "oscAWarp2", 0.5), ("macro2", "oscBWarp1", 0.5)],
           fx=[("dist", fx_mode(4, 8), fx_mode(4, 5), [0.5, 0.72, 0.3, 0.5, 0.5, 0.5], 0.7), COMP_GLUE],
           macros=["Tear", "Sync"], folder=DNB)

    preset("Robot Growl", "Bass",
           "The voice frame, its table and its vowel both stepped at random eight times a bar by two "
           "sample-and-holds, so every bar says something else. Phase distortion from the sub puts "
           "the rasp in. Macro 1 is the rasp, macro 2 how far the mouth opens.",
           merged(MIXED,
                  {"oscAPosition": WAVE["vox"], "oscALevel": 1.0,
                   "oscAWarp1Mode": WARP["pd_sub"], "oscAWarp1": 0.28,
                   "subEnable": 1, "subWave": SUB["sine"], "subOctave": 0, "subLevel": 0.1, "routeSub": 0,
                   "filterType": FILTER["formant"], "cutoff": 735.0, "resonance": 0.6, "drive": 0.55,
                   "filterFreq": 0.45, "filterMix": 0.75,
                   "mono": 1, "legato": 1, "glide": 0.02, "output": 1.0},
                  env(1, 0.002, 0.3, 1.0, 0.08),
                  lfo(1, kind="sh", division="1/1", smooth=0.1), lfo(2, kind="sh", division="1/1", smooth=0.35)),
           routes=[("lfo1", "oscAPosition", 0.12), ("lfo2", "cutoff", 0.25), ("lfo2", "resonance", 0.1),
                   ("macro1", "oscAWarp1", 0.4), ("macro2", "cutoff", 0.3)],
           fx=[("dist", fx_mode(0, 8), fx_mode(4, 5), [0.4, 0.7, 0.3, 0.5, 0.5, 0.5], 0.65),
               ("chorus", 0.0, 0.0, [0.2, 0.3, 0.5, 0.25, 0.1, 0.85], 0.25), COMP_GLUE],
           macros=["Rasp", "Mouth"], folder=DNB)

    # --- Leads --------------------------------------------------------------
    preset("Neuro Lead", "Lead",
           "Four saws frequency-modulated by a sine an octave up, the modulation struck by ENV 2 on "
           "every note, through the dirty ladder into diode distortion, a phaser, a ping-pong eighth "
           "and a plate. Mono with a short glide. Macro 1 is the bite, macro 2 the space.",
           merged(MIXED,
                  {"oscAPosition": WAVE["saw"], "oscAUnison": 4, "oscADetune": 0.22, "oscABlend": 0.6,
                   "oscALevel": 0.65, "oscAWarp1Mode": WARP["fm_osc"], "oscAWarp1": 0.12,
                   "oscAWarp2Mode": WARP["diode"], "oscAWarp2": 0.3,
                   "oscBEnable": 1, "oscBPosition": WAVE["sine"], "oscBOctave": 1, "oscBLevel": 0.15,
                   "filterType": FILTER["dirty"], "cutoff": 2400.0, "resonance": 0.3, "drive": 0.3,
                   "filterFreq": 0.5, "mono": 1, "legato": 1, "glide": 0.03},
                  env(1, 0.004, 0.3, 0.85, 0.25), env(2, 0.001, 0.35, 0.2, 0.2),
                  lfo(1, "sine", division="1/8")),
           routes=[("env2", "oscAWarp1", 0.3), ("lfo1", "cutoff", 0.06), ("velocity", "cutoff", 0.12),
                   ("macro1", "oscAWarp1", 0.4), ("macro2", "fx1s4Mix", 0.4)],
           fx=[("dist", fx_mode(3, 8), 0.0, [0.3, 0.5, 0.3, 0.5, 0.5, 0.5], 0.5),
               ("phaser", 0.0, fx_mode(2, 4), [0.2, 0.55, 0.45, 0.55, 0.5, 0.7], 0.4),
               delay("1/8", 0.35, 0.22), REVERB_PLATE],
           macros=["Bite", "Space"], folder=DNB)

    preset("Hoover Lead", "Lead",
           "Seven saws spread wide over a second stack an octave down, both pulse-width worked by a "
           "slow triangle, and ENV 2 drags every note up from an octave below as it starts: the "
           "hoover. Chorus, a quarter-note delay and a hall. Macro 1 lengthens the rise.",
           merged(MIXED,
                  {"oscAPosition": WAVE["saw"], "oscAUnison": 7, "oscADetune": 0.45, "oscABlend": 0.7,
                   "oscALevel": 0.6, "oscAWarp1Mode": WARP["pwm"], "oscAWarp1": 0.35,
                   "oscBEnable": 1, "oscBPosition": WAVE["saw"], "oscBOctave": -1, "oscBUnison": 3,
                   "oscBDetune": 0.3, "oscBLevel": 0.4, "oscBWarp1Mode": WARP["pwm"], "oscBWarp1": 0.3,
                   "cutoff": 7000.0, "resonance": 0.1, "output": 0.95},
                  env(1, 0.01, 0.4, 0.85, 0.5), env(2, 0.001, 0.28, 0.0, 0.1),
                  lfo(1, "tri", rate=0.8)),
           routes=[("env2", "oscASemitone", -0.5), ("env2", "oscBSemitone", -0.5),
                   ("lfo1", "oscAWarp1", 0.3), ("lfo1", "oscBWarp1", 0.25), ("macro1", "env2Decay", 0.3)],
           fx=[("chorus", 0.0, 0.0, [0.3, 0.3, 0.5, 0.4, 0.15, 0.85], 0.4), delay("1/4", 0.25, 0.15),
               REVERB_HALL],
           macros=["Rise"], folder=DNB)

    preset("Liquid Lead", "Lead",
           "A soft square with a sine an octave over it, plucked by the filter and warmed by a slow "
           "vibrato, into chorus, a ping-pong eighth and a hall. Macro 1 lengthens the pluck, "
           "macro 2 is the space.",
           merged(MIXED,
                  {"oscAPosition": WAVE["sqr"], "oscAUnison": 2, "oscADetune": 0.1, "oscALevel": 0.55,
                   "oscBEnable": 1, "oscBPosition": WAVE["sine"], "oscBOctave": 1, "oscBLevel": 0.3,
                   "cutoff": 1800.0, "resonance": 0.25},
                  env(1, 0.008, 0.5, 0.7, 0.45), env(2, 0.001, 0.4, 0.0, 0.3),
                  lfo(1, "sine", rate=5.5)),
           routes=[("env2", "cutoff", 0.3), ("velocity", "cutoff", 0.1),
                   ("lfo1", "oscASemitone", 0.004), ("lfo1", "oscBSemitone", 0.004),
                   ("macro1", "env2Decay", 0.3), ("macro2", "fx1s3Mix", 0.3)],
           fx=[CHORUS, delay("1/8", 0.42, 0.25), REVERB_HALL],
           macros=["Pluck", "Space"], folder=DNB)

    # --- Subs ---------------------------------------------------------------
    preset("Clean Sub", "Bass",
           "One sine at the note, mono and gliding, with nothing above it. Macro 1 warms it with a "
           "little tube, for speakers that cannot reach the fundamental.",
           merged(MIXED,
                  {"oscAPosition": WAVE["sine"], "oscALevel": 1.0,
                   "oscAWarp1Mode": WARP["tube"], "oscAWarp1": 0.0,
                   "cutoff": 400.0, "mono": 1, "legato": 1, "glide": 0.04, "output": 1.25},
                  env(1, 0.004, 0.3, 1.0, 0.12)),
           routes=[("macro1", "oscAWarp1", 0.5)],
           macros=["Warmth"], folder=DNB)

    preset("Warm Sub", "Bass",
           "A triangle through a touch of tube and a low ladder, so the fundamental carries a second "
           "harmonic the small speakers can hear. Mono and gliding. Macro 1 is the drive.",
           merged(MIXED,
                  {"oscAPosition": WAVE["tri"], "oscALevel": 0.9,
                   "oscAWarp1Mode": WARP["tube"], "oscAWarp1": 0.7,
                   "filterType": FILTER["ladder"], "cutoff": 260.0, "resonance": 0.1, "drive": 0.5,
                   "filterFreq": 0.35, "mono": 1, "legato": 1, "glide": 0.05, "output": 1.15},
                  env(1, 0.005, 0.35, 1.0, 0.15)),
           routes=[("macro1", "drive", 0.5)],
           fx=[("comp", 0.0, fx_mode(1, 2), [0.5, 0.4, 0.3, 0.4, 0.0, 0.3], 1.0)],
           macros=["Drive"], folder=DNB)

    preset("808 Sub", "Bass",
           "A sine that drops an octave in seventy milliseconds, soft-clipped and left to boom: an "
           "808 played from the keys. Every note restarts it. Macro 1 lengthens the punch, macro 2 "
           "drives it.",
           merged(MIXED,
                  {"oscAPosition": WAVE["sine"], "oscALevel": 1.0,
                   "oscAWarp1Mode": WARP["softclip"], "oscAWarp1": 0.3,
                   "cutoff": 300.0, "mono": 1, "legato": 0, "glide": 0.0, "output": 1.25},
                  env(1, 0.001, 1.2, 0.6, 0.5), env(2, 0.001, 0.07, 0.0, 0.05)),
           routes=[("env2", "oscASemitone", 0.5), ("macro1", "env2Decay", 0.3), ("macro2", "oscAWarp1", 0.5)],
           fx=[("comp", 0.0, fx_mode(1, 2), [0.5, 0.45, 0.25, 0.3, 0.0, 0.3], 1.0)],
           macros=["Punch", "Drive"], folder=DNB)

    preset("Reese Sub", "Bass",
           "Two saws nine cents apart under a ladder closed to the fundamental, so the beating is "
           "felt rather than heard: the sub layer of a reese. Mono and gliding; macro 1 opens the "
           "filter towards the mid-range.",
           merged(MIXED,
                  {"oscAPosition": WAVE["saw"], "oscAUnison": 2, "oscADetune": 0.12, "oscABlend": 0.5,
                   "oscALevel": 0.7,
                   "oscBEnable": 1, "oscBPosition": WAVE["saw"], "oscBFine": 9, "oscBLevel": 0.6,
                   "filterType": FILTER["ladder"], "cutoff": 150.0, "resonance": 0.15, "drive": 0.2,
                   "filterFreq": 0.4, "mono": 1, "legato": 1, "glide": 0.06, "output": 1.25},
                  env(1, 0.006, 0.4, 1.0, 0.18), lfo(1, "sine", rate=0.12, mode="free")),
           routes=[("lfo1", "cutoff", 0.03), ("macro1", "cutoff", 0.3)],
           fx=[("comp", 0.0, fx_mode(1, 2), [0.5, 0.4, 0.3, 0.4, 0.0, 0.3], 1.0)],
           macros=["Open"], folder=DNB)


if __name__ == "__main__":
    write_all()
    written = sum(1 for _, _, files in os.walk(OUT) for f in files if f.endswith(".forgepreset"))
    print("wrote", written, "presets to", OUT)
