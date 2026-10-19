#!/usr/bin/env python3
"""Write Forge's factory presets into presets/Factory.

Each preset is written as only what differs from a fresh Forge: the patch is a
handful of PARAM lines under a <RhinoForgeState>, and Forge fills in everything
else at its default when it opens the file (Processor::migrated). So a preset
here reads as a description of the sound rather than as three hundred numbers.

The bank in Documents/Rhino Forge/Presets is given a copy of this folder the
first time Forge makes it, and the browser's Restore puts back any that went
missing. Run from anywhere; the files land in Factory/ beside this script.

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

WARP = {"off": 0, "sync": 1, "bend+": 2, "bend-": 3, "bend+-": 4, "pwm": 5, "asym": 6, "flip": 7,
        "mirror": 8, "quantize": 9, "oddeven": 10, "lpf": 11, "hpf": 12, "bpf": 13, "tube": 14,
        "softclip": 15, "hardclip": 16, "diode": 17, "linfold": 18, "sinefold": 19, "rectify": 20,
        "tapesat": 21, "pd_osc": 22, "pd_sub": 23, "pd_noise": 24, "pd_self": 25, "fm_osc": 26}

FILTER = {"low": 0, "high": 1, "band": 2, "notch": 3, "peak": 4, "lp-bp-hp": 17, "lp-nt-hp": 19,
          "ladder": 21, "acid": 22, "dirty": 23, "ems": 24, "comb": 25, "flanger": 26, "phaser": 27,
          "formant": 28}

FX = {"reverb": 1, "delay": 2, "chorus": 3, "dist": 4, "eq": 5, "filter": 6, "comp": 7, "phaser": 8}
NOISE = {"white": 0, "pink": 1, "brown": 2, "tape": 10, "vinyl": 16, "wind": 17}
SUB = {"sine": 0, "rect": 1, "tri": 2, "saw": 3, "sqr": 4, "pulse": 5}
LFO_SHAPE = {"sine": 0, "tri": 1, "saw": 2, "sqr": 3}
LFO_MODE = {"trig": 0, "env": 1, "free": 2}
LFO_DIVISION = {"1/1": 0, "1/2": 1, "1/4": 2, "1/4T": 3, "1/8": 4, "1/8T": 5, "1/16": 6}
ARP_DIVISION = {"1/1": 0, "1/2": 1, "1/4": 2, "1/8": 3, "1/16": 4, "1/32": 5}
FX_DIVISION = {"1/32": 0, "1/16": 1, "1/8T": 2, "1/8": 3, "1/4T": 4, "1/4": 5, "1/2": 6, "1/1": 7}

SOURCE = {"env1": 1, "env2": 2, "env3": 3, "env4": 4, "lfo1": 5, "lfo2": 6, "lfo3": 7, "lfo4": 8,
          "lfo5": 9, "lfo6": 10, "velocity": 11, "note": 12, "macro1": 13, "macro2": 14, "macro3": 15,
          "macro4": 16, "modwheel": 21}


def destination(name):
    """A matrix destination by parameter id: the named list, the racks, then
    what was appended after them, in ForgeMatrix.h's order."""
    named = ["", "oscAPosition", "oscALevel", "oscAPan", "oscADetune", "oscASemitone",
             "oscBPosition", "oscBLevel", "oscBPan", "oscBDetune", "oscBSemitone",
             "oscCPosition", "oscCLevel", "oscCPan", "oscCDetune", "oscCSemitone",
             "subLevel", "noiseLevel", "cutoff", "resonance", "drive", "subPan", "noisePan",
             "filterPan", "filterMix", "filterLevel"]
    if name in named:
        return named.index(name)
    base = len(named)
    if name.startswith("fx1s"):
        slot = int(name[4]) - 1
        control = 6 if name.endswith("Mix") else int(name[-1]) - 1
        return base + slot * 7 + control
    base += 3 * 8 * 7
    appended = ["oscAWarp1", "oscAWarp2", "oscBWarp1", "oscBWarp2", "oscCWarp1", "oscCWarp2",
                "filterFreq", "noiseTone", "noiseStereo"]
    for env in range(1, 5):
        appended += [f"env{env}{part}" for part in ("Attack", "Decay", "Sustain", "Release")]
    appended += ["oscAOctave", "oscAFine", "oscBOctave", "oscBFine", "oscCOctave", "oscCFine"]
    return base + appended.index(name)


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


def preset(name, style, notes, params, routes=(), fx=(), macros=None):
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
    with open(os.path.join(OUT, name + ".forgepreset"), "w", encoding="utf-8", newline="\n") as file:
        file.write("\n".join(lines))


def env(n, attack, decay, sustain, release):
    return {f"env{n}Attack": attack, f"env{n}Decay": decay, f"env{n}Sustain": sustain, f"env{n}Release": release}


def lfo(n, shape="sine", rate=None, division=None, mode="trig"):
    values = {f"lfo{n}Shape": LFO_SHAPE[shape], f"lfo{n}Mode": LFO_MODE[mode]}
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


if __name__ == "__main__":
    write_all()
    print("wrote", len([f for f in os.listdir(OUT) if f.endswith(".forgepreset")]), "presets to", OUT)
