# LISA synthesizer

LISA is a small semi-modular polyphonic wavetable synthesizer and configurable kinetic MIDI controller for Raspberry PICO with live dynamic wavetable synthesis.
The synthesizer and MIDI controller are standalone, but gain extended capabilities and flexibility when paired with [Nallely](https://github.com/dr-schlange/nallely-midi) (e.g: LFOs, sequencers, sensors interactions, ...).

LISA is a hard-fork of the [VIJA synthesizer v1.0.2](https://github.com/ledlaux/vija-pico-synth/releases/tag/v.1.0.2) for Raspberry PICO created by [Vadims Maksimovs](https://github.com/ledlaux). The original version is based on the port of the [Mutable Instruments Braids](https://github.com/poetaster/BRAIDS) macro oscillator.

[![LISA prototype YouTube video](https://img.youtube.com/vi/1RY8hzp2-kY/0.jpg)](https://www.youtube.com/watch?v=1RY8hzp2-kY;start=11)

## Features

Features inherited from **VIJA**:

- **40+ Oscillator Engines:** Includes VA, FM, Additive, Wavetable, Physical Modeling and Drums.
- **4-Voices Polyphony:** Per-sample AR (Attack-Release) envelopes.
- **OLED Interface:** Real-time feedback with a menu system and a oscilloscope.
- **Modulation:** 2 pots, CV input and midi cc.
- **Internal Filter:** Integrated State Variable Filter (SVF) with Low-Pass and Resonance.
- **Dual MIDI:** Support for both USB MIDI and classic UART MIDI.

Features added in **LISA**:

- **Up to 8 Voices Polyphony** at 44.1 kHz (depending on the engine, some will handle only 5 or 7 properly, unison mode is up to 4 voices only). The `LIVE` engine runs all 8 voices, in poly (e.g. 2 chords of 4 notes) or in unison (4 notes, 2 voices each). The voices are independent: each one has its own oscillator phase, its own envelope and its own filter, so notes keep their own attack, release and filter state when others start or stop.
- **LIVE engine: Live Dynamic Wavetable Scanner** 4 wavetables filled in real time by streams from Nallely or any source that can send pitchwheel data (one channel per table), mixed with vector synthesis-style XY mixing. The technique of filling a wavetable from a slow signal comes from [Scanned Synthesis](https://ccrma.stanford.edu/~verplank/S2S/ScannedSynthesis.PDF), without any physical model bound to it: bring your own from a Nallely patch or module. See [LIVE engine: Live Dynamic Wavetable Scanner](#live-engine-live-dynamic-wavetable-scanner).
- **Stereo spread** Each voice gets a position in the stereo field (constant-power pan), chosen by one of 4 placement modes, see [Stereo spread](#stereo-spread).
- **USB audio output** LISA is also view by the host system as an USB audio microphone (44.1 kHz, 16-bit stereo), so the host can record or listen to it without the I2S DAC, see [USB audio](#usb-audio).
- **HR protocol** Optional 16-bit resolution input (second USB serial port) for continuous parameters and wavetable streams, used by Nallely, see [HR protocol](#hr-protocol-high-resolution). Regular MIDI stays fully supported.
- **Envelope Adaptation** Envelope attack/release can go from nothing to quite long (up to 5s)
- **New internal parameters exposed** Gain, FM slew (slew between FM jumps if FM is modulated)
- **MIDI Controller** Support to send MIDI controls via USB.
- **MIDI Controller extended configuration** Allows to either repeat the value set by the buttons, or to only send values without interacting with the synth part.
- **Advanced configuration** Access all synth parameter configuration and more with the physical buttons.
- **Multiple MIDI/Physical button interaction** Support for 2 different modes of interaction between arriving MIDI messages and manually touching the knobs, configurable by knob:
  - `raw mode`: the button and the MIDI CC value fights to make their value prioritary (first arrived first served).
  - `catch-up mode`: the button stays "innactive" until the moment it reaches the value set by the MIDI CC value.
- **Kinetic Controls** Support for a "friction" and "elasticity" on buttons to send MIDI values that oscillate and damp themselves when huge movements are operated.
- **Attenuator Mode** Select the min, center, max of a range with the buttons, restricting the range of values that will be received by MIDI. MIDI CC value targetting a button is then scaled on this new range, allowing for finer grain definition (planned).
- **Nallely deep integration** Send/receive messages via websocket to a running Nallely session to route the signal to whatever target you want with float precision in the message sent/received (planned).

## LIVE engine: Live Dynamic Wavetable Scanner

The `LIVE` engine plays 4 wavetables of 256 points each.
Nothing is stored in the firmware: the tables are filled in real time by a host (Nallely or anything that sends pitchwheel messages), so the sound is whatever the 4 incoming signals draw.

### How it works

1. **Writing.** Each table has its own stream (pitchwheel channels 1-4). Every received value is written at the write position of its table, which advances by one point per value, and wraps after 256 points (one "lap"). A 1 Hz LFO sent at about 260 values/s fills one table in about 1 s, so the table shows one cycle of the LFO. Sub-1 Hz signals prefers a lower send rate, otherwise the lap is shorter than the signal. A sub-1 Hz signal, with the sluggish mode (on by default, see below) activated, the signal will spread over multiple frames which will morph from one to the other. With the sluggish mode disabled, the played wavetable will be rewritten while it plays. This brings interesting timbral movement depending on what you're looking for.
2. **Reading.** Each voice reads the 4 tables with their own levels at the played pitch (linear interpolation) and mixes them with 4 weights. The tables are shared by all the voices.
3. **Mixing.** The weights come from **timbre** (X axis) and **color** (Y axis), and from the 4 per-table levels.

The sound therefore moves when the stream changes (the table shape changes), and when timbre/color move (the weights change), and both can be driven by LFOs, sequencers, sensors, etc. Basically, anything that can send a stream of floats at any rate.

### Mixing: timbre, color and levels

Timbre and color form an XY pad, each table sits in a corner:

```
          color
            ^
   table 3  |  table 4
  ----------+---------> timbre
   table 1  |  table 2
```

- **Timbre** crossfades between the left tables (1, 3) and the right ones (2, 4).
- **Color** crossfades between the bottom tables (1, 2) and the top ones (3, 4).
- The weights are bilinear (a table weighs `x * y`, `(1-x) * y`, etc.), then compressed toward the center with a curve: the range of the pad is reduced, and even at a corner the other tables stay slightly audible. The engine is a blend, not a table selector. The timbre/color modulation amounts (CC#16/17) work as for the other engines.
- **Levels** (CC#96-99) are a per-table gain applied after the XY weights (default 127). Use them to silence a table (0), to rebalance, or to modulate a table's presence independently of the XY position. As any CC# or pitcheel in LISA, they are fully modulable from external sources.

### Writing modes and write index

Each table has a mode (CC#112-115, groups of 32: 0-31, 32-63, 64-95, 96-127):

| Mode                  | Behavior                                                                                                  |
| :-------------------- | :-------------------------------------------------------------------------------------------------------- |
| `circular` (default)  | Values are written one after another, the write position wraps.                                           |
| `scroll`              | The table scrolls: each value enters at the end and shifts the others, like an oscilloscope.              |
| `manual`              | A value is written at the write index (CC#108-111, 2 points per step), the index does not move by itself. |
| `manual_interpolated` | Same, with a linear interpolation from the previous index to the new one.                                 |

With the manual modes, the write index is the "pen position": a second signal moves the index while the first one gives the value, which draws the table like a scanner. `reset write index` (CC#118) puts the write index of every table back to 0, `freeze` (CC#119-123) stops the writing of one or all tables and keep their shape to be played/heard, `reset all wavetables` (CC#117) clears them.

### Phase ratio, phase offset and hard sync

Each table can read at its own speed and position (pitchwheel channels 6-13, see the chart and the notes under it):

- **Ratio** changes the reading speed of a table relative to the played pitch. 1.0 is the default, 2.0 plays an octave higher, negative values read backward. When tables have different ratios, they drift apart in phase and the result sounds more like FM than like a harmonic mix.
- **Offset** shifts the reading position of a table. It only matters relative to the other tables (PWM-like phasing), or at note-on.
- **Hard sync** (CC#91): each table restarts when a master phase at the played pitch wraps. Together with ratios different from 1.0 it gives the classic sync-sweep timbre.
- **Retrigger** (CC#124) resets the phases at each note-on, otherwise the phases run free. **Phase reset** (CC#126) resets them manually.

### Sluggish mode

Without sluggish mode, a table is read while it is written: every new value is heard immediately, and the table is the signal as it is right now. In **sluggish mode** (CC#94, `ON` is a value < 64 and `OFF` a value >= 64, on by default in the firmware), the signal is spread over several laps (frames) instead. Each lap of the stream is kept as a frame, and the sound reads a slow crossfade between those frames, so the table is never only the present: it carries a memory of its past self, and the timbre drifts from what the signal was a few laps ago toward what it is now.

- **Depth** (CC#93): number of laps kept, from 2 to 16. A deeper memory is a longer trail of past table shapes.
- **Factor** (CC#106): how long a crossfade between two laps takes, counted in received values (from 10 to 1500 per fade). A high factor is very sluggish: the sound evolves slowly even if the stream is fast. At the max, the sound lags behind the signal.
- **Direction** (CC#92): the laps are crossfaded from the oldest toward the newest (default), or from the newest back toward the oldest (value >= 64).
- **Automatic** (CC#105, value >= 64): the crossfade runs by itself at the speed given by the factor. If it is off, the position in the memory is controlled by CC#107 (0 is the oldest lap, 127 the newest), so you can scrub the past of the tables with a knob or an LFO.

Sluggish mode needs at least 2 completed laps before it does anything. It turns fast or jittery sources into smooth, evolving timbres, where the non-sluggish mode follows every detail of the source.

### A typical configuration

This section presents a tipycal configuration in Python (the configuration doesn't have to be written in Python and can be patched live from Nallely's GUI, a mobile and touch friendly webui).
A slow, evolving pad, with Nallely (`lisa` is the device, `lfo1`...`lfo4` are LFOs at different low speeds, sine, triangle, saw and square for example):

```python
from nallely.lisa.lisa_pico import Lisa

lisa = Lisa()
lisa.general.engine_select = 127          # LIVE engine

# LFOs configurations are not written here to focus on the LISA part

# 4 tables, fed by 4 different sources, filled in circular mode
for i, lfo in enumerate((lfo1, lfo2, lfo3, lfo4), start=1):
    setattr(lisa.wavetable, f"mode_wt{i}", "circular")
    setattr(lisa.wavetable, f"stream_table{i}", lfo.scale(-8192, 8192))

# move the XY mix slowly
lisa.modulation.timbre = lfo5.scale(0, 127)
lisa.modulation.color = lfo6.scale(0, 127)

# a bit of FM-like movement: slightly different table speeds
lisa.wavetable.phase_ratio_table2 = lfo7.scale(-300, 300)
lisa.wavetable.phase_ratio_table3 = lfo8.scale(-300, 300)

# smooth the result with the sluggish mode
# most of those values are default and could have been omited here
lisa.wavetable.sluggish_mode = "ON"
lisa.wavetable.slug_depth = 64            # 8-9 laps
lisa.wavetable.sluggish_factor = 20
lisa.wavetable.auto_slug_factor = "ON"
```

Starting points:

- **Pad/drone:** LFOs below 1 Hz in the tables, sluggish mode on, a long release, a slow timbre/color LFO.
- **Evolving lead:** circular or scroll mode, faster LFOs (1-5 Hz) on 2 tables and slow ones on the others, sluggish mode off, levels 3 and 4 modulated.
- **Scanned drawing:** `manual` mode on a table, one signal on the stream (value), another one on the write index (position).
- **Metallic/FM-like:** hard sync on, different phase ratios (e.g. 1.0, 1.5, 2.0, 3.0) for the 4 tables, a slow LFO on one of them.

Note that this configuration relies on Nallely's Python API for LISA.
This API is generated from a [YAML description](https://github.com/dr-schlange/nallely-midi/blob/main/nallely/lisa/lisa-pico.yaml), if you have another MIDI chart or any other device, you can write your own YAML description and generate your Python API from it.

The [simulator](#live-engine-live-dynamic-wavetable-scanning-simulator) reproduces this engine on a computer if you want to try the ideas without the hardware. The sluggish mode is not implemented though, only a subset of LISA features are there, but that gives you a first appreciation about what the LIVE engine offers to you.

## Stereo spread

The spread (CC#13) opens the voices in the stereo field. It is controlled by MIDI (or HR) only, there is no knob or menu entry for it.
At 0 every voice is centered and the output is the same as the mono mix.
At 127 the voices go from full left to full right. The pan (CC#14) stays a global balance applied after the spread.
The [HR protocol](#hr-protocol-high-resolution) also accepts CC#13 with 16-bit values.

The spread mode (CC#19) decides where a voice is placed:

| Value | Mode          | Position of a voice                                                                         |
| :---- | :------------ | :------------------------------------------------------------------------------------------ |
| 0     | `voice_index` | Fixed by the voice number, the 8 voices are spread evenly left to right.                    |
| 32    | `note`        | Fixed by the pitch at note-on, low notes left, high notes right (+-24 semitones around C4). |
| 64    | `random`      | Random at each note-on.                                                                     |
| 96    | `ping_pong`   | Alternates left/right at each note-on.                                                      |

Notes:

- The position is chosen at note-on and the voice keeps it until it is reused. Moving the spread or changing the mode moves the voices with a short slew (about 7 ms), without clicks.
- Voices are reused by pitch when none is free: a melody that loops on N <= 8 distinct notes brings every pitch back on its own voice, so in `voice_index` mode every note keeps its place.
- In `unison` mode the secondary voice copies the position of its primary voice.
- The scope displays the left channel only.

In Nallely, `general.spread` and `general.spread_mode` are available in the generated `lisa_pico.py`.

## HR protocol (high resolution)

MIDI is coarse: a CC has 7 bits (128 steps) and a pitchwheel 14 bits.
128 steps is audible when a CC sweeps a parameter that is sensitive to small changes (cutoff, resonance, timbre, color, FM), and it shows up somtimes as a zipper noise when an LFO drives it.
It also limits the wavetable streams to 14-bit samples and the per-table ratio and offset to coarse steps.

The HR protocol gives LISA a second input with 16-bit values (65536 steps), so a signal produced as a float by Nallely reaches LISA almost untouched.

### Overview

- LISA exposes a second USB serial port (CDC), named `LISA HighResolution`, next to the USB MIDI port. It is only used when a host opens it. Nothing changes for MIDI, both inputs can be used at the same time.
- Handshake: the host initiate the handshake and LISA answers when it's ready. Messages are ignored before the handshake, and the handshake restarts if the port is closed.
- A frame is 1 byte with the message count, followed by 4 bytes per message (a message fits on 32bits, a frame is then 1 byte + 4 * #msg bytes):

  | Byte | Content                                                                                     |
  | :--- | :------------------------------------------------------------------------------------------ |
  | 1    | CC number (or pitchwheel channel)                                                           |
  | 2-3  | 16-bit value, little endian                                                                 |
  | 4    | Flags: bit 0 = bipolar range (-1..1) instead of unipolar (0..1), bit 1 = pitchwheel message |

  Several messages can be sent in one frame, which is how Nallely sends all its updates of one tick.

- The CC numbers are the same as in the chart above, the HR value replaces the 7-bit one.

### Impacted parameters

Only continuous parameters: gain, master volume, timbre, color, their modulation amounts, attack, release, filter cutoff and resonance, FM mod and slew, unison detune, pan, spread, the sluggish factor and blend level, the 4 table levels and the global phase offset. For the pitchwheel family: the 4 wavetable streams (16-bit samples), the pitch bend, and the ratio and offset of each table (4x finer steps than with 14-bit).

Everything else (voice mode, engine select, spread mode, switches like hard sync, freeze or sluggish on/off, notes) is not sent by HR, and keeps going through regular MIDI, as having a higher resolution for those parameters wouldn't bring much on the table.

### Implementation by the host

At the moment, only Nallely does (`lisa_pico_hr.py`, the Python API generated from `lisa-pico.yaml`, uses it for the parameters marked as HR).
Nothing else needs to be done for a DAW, a hardware controller or any other software: all the parameters stay available through the regular MIDI messages of the chart above, with their 7-bit and 14-bit resolution.
HR is an addition, not a requirement.

## USB audio

When plugged via USB, LISA shows up on the host as an audio input device (`LISA Synth`): UAC2 microphone, 44.1 kHz, 16-bit, stereo, the same signal as the I2S output (spread and pan included). You can record it or route it to your speakers/headphones with the host's audio system, which removes the need for the PCM5102 DAC and avoids ground loops when recording. The I2S DAC is therefore optional: you can build LISA without it and use USB as the only audio output, or keep both, the I2S output and the USB audio run at the same time.

- Tested on Linux (PipeWire/PulseAudio). It should work on macOS, it is untested on Windows.
- The USB audio sample rate is fixed to 44.1 kHz. For hackers/developers, exactly, it must equal `SAMPLE_RATE` (a `static_assert` checks it).

- The audio is sent from the second core in blocks of 32 frames, through a small FIFO (about 8 ms) drained by the USB interrupt. Nothing is sent while the host does not stream. If the FIFO is full, a block is skipped rather than blocking the synth.
- The device exposes no volume or mute control, and there is no feedback endpoint: the host's clock and LISA's clock drift slowly apart, which the host absorbs.
- The audio class is enabled with a flag passed to the compiler: TinyUSB reads its configuration while the core is compiled, so `src/usb_audio.h` is force-included with `-include`. The `Makefile` already does it:

  ```
  arduino-cli compile --build-property "build.extra_flags=-include \"$(CURDIR)/src/usb_audio.h\""
  ```

  With the Arduino IDE, the same flag has to go in `build.extra_flags` (untested). Without the flag LISA builds without USB audio (about 2.2 KB less flash and 2.5 KB less RAM). After changing the flag, rebuild everything (`make -B`).

## Menu System & UI

The synthesizer operates in two primary display modes:

1.  **ENGINE SELECT + SETTINGS:**:
    1. **ENGINE SELECT** Rotate the encoder to scroll through engines
    2. **SETTINGS:** Click the encoder button to cycle through parameters
       - **VOLUME:** Global gain control
       - **A/R ENVELOPES:** Adjust the "snappiness" (Attack) or "fade" (Release) of notes
       - **FILTER:** Enable/Disable the Filter
       - **CV:** CV modulation mode
       - **MIDI:** Enable MIDI CC and disable CV
       - **MIDI CH:** Set the input channel (1-16)
       - **OSCILLOSCOPE Toggle:** On / Off
       - **SAVE SETTINGS:** Long press button to save menu settings
       - **EXIT MENU**
2.  **OSCILLOSCOPE:** Automatically engages after 10 seconds to visualize the current waveform

To go back to the **ENGINE SELECT + SETTINGS** screen from the **OSCILLOSCOPE** screen, you need to do a double press of the encoder.

There is 2 additional screens which are accessible with a single or double press of the encoder:

1. **ALL PARAMETERS** display (single press from the **OSCILLOSCOPE** display): that brings you to a page with all the parameter of the synth and a quick access to modify them.
   1. Rotate the encoder to select a parameter row. Each parameter row is automatically mapped on A, B, C, representing the 3 main pots of LISA.
   2. Click the encoder to activate the modification on the row \*.
   3. Rotate A, B or C to set the desired parameter level.
   4. Rotate the encoder again to pass to another row (no need to press again, but you can if you want to).
   5. Double press the encoder to come back to the **OSCILLOSCOPE** display.
   6. [**ENGINE SELECT**] When on the name of the synth engine
      1. Press the encoder to pass in engine select mode.
      1. Rotate the encoder to select your engine.
      1. Press the encoder again to go back to the row selection.
2. **GLOBAL SETTINGS** display (double press from the **ENGINE SELECT** or **SETTINGS** display): that brings you to the general synth settings where you can configure the behavior of some buttons (type of conflict resolution between pots and MIDI messages, etc).
   1. Rotate the encoder to select a parameter row:
   1. `param` is the synth parameter to edit. `ALL` represents modifications that are applied to all parameters. If a single parameter is selected, this parameter will be impacted by the other rows, but not all the parameters.
   1. `resol` is the resolution policy on arriving messages. By default `catchup` is selected, can be set to `raw`.
   1. `mode` is the mode for the parameter, it can be `kinetic` or `normal` (default is `normal`).
   1. Press the encoder to modify the selected parameter.
   1. Double press the encoder to come back to the **SETTINGS** of the engine.
   1. [**KINETIC MODE**] When `kinetic` mode is activated, 3 new parameters appears in the bottom on the screen. Those parameters are automatically mapped to the 3 potentiometers and represent:
   - `mas`: the **mass** of the potentiometer cursor (going towards 100% means higher amplitude oscillation),
   - `dmp`: the **damping** of the damping factor of the sprint (going towards 100% damps the oscillation),
   - `stf`: the **stiffness** of the spring (going towards 100% oscillation become like a pendulum, almost LFO),

\* At this point, if you are in `CATCHUP` resolution mode (default mode), the levels will stay put until you move the related pot close from the parameter recorded value.
\* At this point, if you are in `RAW` resolution mode, the levels will adujst with the values of the pots! Use this feature to quickly prepare new values for your row and jump to the values when pressing the encoder.

### Filter Mode (Default)

- Timbre & Color (default)
- CV1 & CV2 → Filter cutoff & resonance

### CV Modulation Mode1

- Timbre & Color modulation
- POTS → modulation depth
- CV1 & CV2 → Modulation source

### CV Modulation Mode2

- CV1 → Engine selection
- CV2 → FM MOD

### MIDI Modulation Mode

- Timbre & Color (Soft takeover mode)

  Align coresponding MIDI CC value with Timbre or Color pot value to release or vice versa (screen indicator)

- CV1 & CV2 → Free for future functions

### All Modes OFF

- Timbre & Color (default)

## Software Setup

1.  **Arduino IDE:** Install the [Earle Philhower Pico Core](https://github.com/earlephilhower/arduino-pico)
2.  **Libraries:**

- arduinoMI project (ported Mutable Instruments libraries)
  - STMLIB https://github.com/poetaster/STMLIB
  - BRAIDS https://github.com/poetaster/BRAIDS
- I2S
- Adafruit TinyUSB
- Adafruit SSD1306 or SH110X display
- LittleFS & ArduinoJson for saving settings

3.  **Compilation Settings:**

- Enable flash file system for saving menu settings:  
  Flash size: "2MB (Sketch:1984KB, FS:64KB)"
- Enable USB Stack: Adafruit TinyUSB

* **RP2040:** - Optimize: Fast (-O3)
  - CPU Speed: 200-240mhz (Overclock) depending on the sample rate and needed voice count
  - Sample rate:
    - 32000 (up to 10 voices depending on the engine, 5 voices in unison mode)
    - 44100 (up to 8 voices depending on the engine, 4 voices in unison mode, definitely the recommended setting)
    - 48000 (up to 6 voices depending on the engine, 3 voices in unison mode, but might have some unpleasant click sometimes depending on the amount of modulation you set on the phase ratio or offset)
    - 96000 (up to 4 voices depending on the engine, 2 voices in unison mode, but might have some unpleasant click sometimes depending on the amount of modulation you set on the phase ratio or offset)
* **RP2350:** - Optimize: Fast (-Ofast)
  - Sample rate: 48000

### Quick deployment and testing using arduino-cli and Nallely

If you have Nallely installed as a library somewhere on your system (i.e: accessible from Python when doing `import nallely`) and `arduino-cli` installed and configured properly (i.e: configuration pointing to the shared folder where all the third-party libraries are located), you can use the `Makefile` to have a quick update or dev workflow:

```
make           # builds the firmware in `build`
make upload    # builds the firmware if necessary and uploads it to your LISA synth
make fulltest  # builds the firmware if necessary, uploads it to your LISA synth and runs the tests (interactive audio test)
make test      # runs the all the defined tests
make testX     # runs the test X (e.g: "make test3" runs the test3)
```

## Schematic & Wiring

For this project I use RP2040 Zero model, so adjust GPIO numbers for your board.

### 1. Audio Output (I2S DAC)

This part is optional if you only use LISA through its [USB audio](#usb-audio) output.

Connect your **PCM5102** or similar I2S DAC:

- **VCC/VIN** -> Pico 3.3V (Pin 36)
- **GND** -> Pico GND
- **LCK (LRCK)** -> Pico GP11
- **BCK (BCLK)** -> Pico GP10
- **DIN (DATA)** -> Pico GP9

### 2. Control & Display

- **OLED SDA** -> Pico GP0
- **OLED SCL** -> Pico GP1
- **Encoder CLK** -> Pico GP2
- **Encoder DT** -> Pico GP3
- **Encoder SW** -> Pico GP4

### 3. Potentiometers (ADC)

Connect the outer pins to 3.3V and GND, and the center wiper to:

- **Pot 1 (Timbre)** -> GP26
- **Pot 2 (Color)** -> GP27
- **Pot 3 (CV1)** -> GP28
- **Pot 4 (CV2)** -> GP29 (Raspberry Pico & Pico W don't have GP29!)

### 4. MIDI Input (UART)

Connect your MIDI Jack via a 6N138 optocoupler circuit to **GP13**.

## LIVE engine (Live Dynamic Wavetable Scanning) simulator

This repo includes a Python/numpy implementation of LISA's LIVE engine. This allows you to be able to play with the LIVE engine from any computer without having to build LISA.

The simulator is built to be loaded directly in Nallely as a MIDI device. It has been tested successfuly on a Linux laptop, as well as Android using termux.

### Installation

One extra Python package is necessary to make it work: `sounddevice`, install it using pip in the venv that runs Nallely.

Just copy the `lisa_sim.py` file somewhere in your machine where Nallely is installed/deployed.

#### Installation in Termux

With Termux on your Android device, you can run Nallely and the LIVE engine anywhere. That's handy to make some tests on the fly wherever you are.

To install it, first install `portaudio` and the `sounddevice` python package:

```bash
# in your termux installation
pkg install portaudio
pip install sounddevice
```

Once those packages installed copy the `lisa_sim.py` file copied somewhere in your termux env.

### Run it

To include the LIVE engine simulator in Nallely, just pass it on the command line:

```bash
nallely run --with-trevor --serve-ui -l PATH/TOWARD/lisa_sim.py --experimental
```

This will run the server with the Trevor protocol, serve the UI on `http://localhost:3000` and activates the experimental devices (if you don't want them, just omit the `--experimental` flag).

### Differences with the actual LIVE engine

Currently not all the CCs/parameters for LISA are implemented in this simulator. Currently, FM/FM-slew CC is not implemented, and obviously, the only available engine is the LIVE engine. The sluggish mode is also not implemented yet.

## MIDI Implementation (CC Chart)

LISA responds to the following Control Change (CC) messages on the selected MIDI Channel:

| CC #                          | Parameter                                                         |
| :---------------------------- | :---------------------------------------------------------------- |
| **2**                         | Voice mode (0: poly, 42: unison, 84: mono) from v0.1.0            |
| **3**                         | Gain                                                              |
| **4**                         | Detune cents for unison mode (0: -50cents, 12: 50cents)           |
| **7**                         | Master Volume                                                     |
| **8**                         | Engine Select                                                     |
| **9**                         | Timbre                                                            |
| **10**                        | Color                                                             |
| **11**                        | Envelope Attack                                                   |
| **12**                        | Envelope Release                                                  |
| **13**                        | Stereo spread amount (0: all voices centered)                     |
| **14**                        | Pan                                                               |
| **15**                        | FM Modulation                                                     |
| **16**                        | Timbre Modulation Amount                                          |
| **17**                        | Color Modulation Amount                                           |
| **18**                        | FM slew                                                           |
| **19**                        | Spread mode (0: voice index, 32: note, 64: random, 96: ping-pong) |
| **64**                        | Sustain (Hold notes)                                              |
| **71**                        | Filter Resonance                                                  |
| **74**                        | Filter Cutoff                                                     |
| **75**                        | Filter Type                                                       |
| **91**                        | Hard sync of the wavetable phases (>= 64: on, default off) (\*\*) |
| **92**                        | Slug blending direction (\*\*\*)                                  |
| **93**                        | Slug Depth (temporal memory) (\*\*\*)                             |
| **94**                        | Activates Sluggish Mode (< 64: on, default; >= 64: off) (\*\*)    |
| **95**                        | Sends the peak envelope of the sound (per block)                  |
| **96**                        | Wavetable 1 mix level (\*\*)                                      |
| **97**                        | Wavetable 2 mix level (\*\*)                                      |
| **98**                        | Wavetable 3 mix level (\*\*)                                      |
| **99**                        | Wavetable 4 mix level (\*\*)                                      |
| **100**                       | b1 (send CC only)                                                 |
| **101**                       | b2 (send CC only)                                                 |
| **102**                       | b3 (send CC only)                                                 |
| **103**                       | b4 (send CC only)                                                 |
| **104**                       | b5 (send CC only)                                                 |
| **105**                       | Automatic sluggish factor (\*\*\*)                                |
| **106**                       | Sluggish factor (\*\*\*)                                          |
| **107**                       | Sluggish level external control (\*\*\*)                          |
| **108**                       | Wavetable 1 write index (\*\*)                                    |
| **109**                       | Wavetable 2 write index (\*\*)                                    |
| **110**                       | Wavetable 3 write index (\*\*)                                    |
| **111**                       | Wavetable 4 write index (\*\*)                                    |
| **112**                       | Wavetable 1 mode - default is circular (\*\*)                     |
| **113**                       | Wavetable 2 mode - default is circular (\*\*)                     |
| **114**                       | Wavetable 3 mode - default is circular (\*\*)                     |
| **115**                       | Wavetable 4 mode - default is circular (\*\*)                     |
| **117**                       | Reset all wavetables to 0 (\*\*)                                  |
| **118**                       | Reset write index on all wavetables (\*\*)                        |
| **119**                       | Freezes wavetable 1 (\*\*)                                        |
| **120**                       | Freezes wavetable 2 (\*\*)                                        |
| **121**                       | Freezes wavetable 3 (\*\*)                                        |
| **122**                       | Freezes wavetable 4 (\*\*)                                        |
| **123**                       | Freezes all the wavetables (\*\*)                                 |
| **124**                       | Retrigger (reset phase on strike) (\*\*)                          |
| **125**                       | Add an offset to the phase (\*\*)                                 |
| **126**                       | Reset the wavetable phase (\*\*)                                  |
| **pitchwheel** channel 1      | Streams a waveform for wavetable 1 (\*\*)                         |
| **pitchwheel** channel 2      | Streams a waveform for wavetable 2 (\*\*)                         |
| **pitchwheel** channel 3      | Streams a waveform for wavetable 3 (\*\*)                         |
| **pitchwheel** channel 4      | Streams a waveform for wavetable 4 (\*\*)                         |
| **pitchwheel** channel 5      | Pitch bend, +-2 semitones, all engines                            |
| **pitchwheel** channels 6-9   | Phase ratio of wavetable 1-4 (\*\*\*\*)                           |
| **pitchwheel** channels 10-13 | Phase offset of wavetable 1-4 (\*\*\*\*)                          |
| **127**                       | Reset USB to upload from IDE (WARNING: for dev mode)\*            |

CC#100-104 are sent by the button B or C depending on the row you are located in the **ALL PARAMETERS** mode.
They are sent CCs that are here mainly to provide extra actions to LISA used as a MIDI controller in general, and can achieve internal rewiring if used with Nallely.

In Nallely, you can remap each parameter `b1` to `b5` to any port of Nallely session, and you can reroute them also on other LISA's parameters (you can map `b1` on the `cutoff` for example).

\* Note: in `dev mode`, CC#127 have 2 specific values it can use:

- 127 -> resets the USB stack and sets the device in "receiving" mode to flash another firmware
- 126 -> resets LISA (app reset)
- 1 -> pulls LISA current configuration (sends values of all known CC parameters)

\*\* Note: those CCs are only used by the live wavetable engine (`LIVE`).

\*\*\* Note: those CCs are only used by the live wavetable engine (`LIVE`) set in `Sluggish Mode`.

\*\*\*\* Note: in the live wavetable engine (`LIVE`), each wavetable has its own phase. Values are the raw 14-bit pitchwheel value (-8192..8191), and the default (centered, 0) keeps the previous behavior, all the wavetables on the same phase.

- **Ratio:** the wavetable reads at `base increment * (1 + raw / 1024)`, range -7.0 to 9.0, 0 is 1.0 (the HR protocol covers the same range with 16-bit values, `1 + raw / 4096`). A negative ratio reads the wavetable backward, `-1024` freezes it. Different ratios make the wavetables drift apart.
- **Offset:** shifts the wavetable read position by `raw / 16384` cycle (-0.5 to +0.5 cycle), added to the global offset of CC#125. It is a static shift, it only matters relative to the other wavetables (PWM-like), or at note-on.
- **Hard sync:** a master phase runs at the played pitch, and when it wraps, each wavetable restarts at `master remainder * its ratio`. At ratio 1.0 it changes nothing, with ratios different from 1.0 it gives a classic sync-sweep timbre. Without sync, the phases are free.

## License

This project is licensed under the GNU General Public License v3.0 (GPLv3).

### Copyright

- Original work (VIJA synthesizer v1.0.2)
  Copyright (c) 2025 Vadims Maksimovs
- Modifications and additional features (LISA)
  Copyright (c) 2026 Dr Schlange

All modifications and newly added features are original work by Dr Schlange and are distributed under the GNU GPLv3 as part of this derivative project.

### Third-Party Components

This project depends on the following external components:

- Mutable Instruments Braids / stmlib\
  Copyright (c) 2020 Emilie Gillet\
  Licensed under the MIT License
- Braids port for Raspberry Pico\
  Copyright (c) 2025 Mark Washeim\
  Licensed under the MIT License

These components are not distributed as part of this repository. They are separate projects, each licensed under their respective terms.

### Attribution

LISA is a hard fork of the VIJA synthesizer v1.0.2, originally created by Vadims Maksimovs.
This project is an independent fork and is not affiliated with or endorsed by the original authors.

## Version history

- 2026-xx-xx - LISA v0.3.0 Architecture changes, new mode, HR protocol, new features (panning, stereo), independent voices
- 2026-07-01 - LISA v0.2.0 New features and additions on the LIVE engine
- 2026-05-02 - LISA v0.0.1 first version with LIVE engine
- 2026-03-24 - LISA v0.0.0 hard-fork from VIJA
- 2026-02-26 - VIJA v1.0.2
- 2026-02-03 - VIJA v1.0.1
- 2026-02-02 - VIJA First release v1.0
