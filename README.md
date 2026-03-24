# LISA synthesizer

LISA is a small semi-modular synthesizer and configurable kinetic MIDI controller for Raspberry PICO. The synthesizer and MIDI controller are standalone, but gain extended capabilities and flexibility when paired with [Nallely](https://github.com/dr-schlange/nallely-midi) (e.g: LFOs, sequencers, sensors interactions, ...).

LISA is a hard-fork of the [VIJA synthesizer v1.0.2](https://github.com/ledlaux/vija-pico-synth/releases/tag/v.1.0.2) for Raspberry PICO created by [Vadims Maksimovs](https://github.com/ledlaux). The original version is based on the port of the [Mutable Instruments Braids](https://github.com/poetaster/BRAIDS) macro oscillator.

## Features

Features inherited from **VIJA**:

- **40+ Oscillator Engines:** Includes VA, FM, Additive, Wavetable, Physical Modeling and Drums.
- **4-Voice Polyphony:** Per-sample AR (Attack-Release) envelopes.
- **OLED Interface:** Real-time feedback with a menu system and a oscilloscope.
- **Modulation:** 2 pots, CV input and midi cc.
- **Internal Filter:** Integrated State Variable Filter (SVF) with Low-Pass and Resonance.
- **Dual MIDI:** Support for both USB MIDI and classic UART MIDI.

Features added in **LISA** (currently planned):

- **MIDI Controller** Support to send MIDI controls via USB.
- **MIDI Controller extended configuration** Allows to either repeat the value set by the buttons, or to only send values without interacting with the synth part.
- **Multiple MIDI/Physical button interaction** Support for 3 different modes of interaction between arriving MIDI messages and manually touching the knobs, configurable by knob:
  - `raw mode`: the button and the MIDI CC value fights to make their value prioritary (first arrived first served).
  - `catch-up mode`: the button stays "innactive" until the moment it reaches the value set by the MIDI CC value.
  - `attenuator-mode`: select the min, center, max of a range with the buttons, restricting the range of values that will be received by MIDI. MIDI CC value targetting a button is then scaled on this new range, allowing for finer grain definition.
- **Kinetic Controls** Support for a "friction" and "elasticity" on buttons to send MIDI values that oscillate and damp themselves when huge movements are operated.
- **Advanced configuration** Access all synth parameter configuration and more with the physical buttons.
- **Nallely deep integration** Send/receive messages via websocket to a running Nallely session to route the signal to whatever target you want.

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
       - **OSCILOSCOPE Toggle:** On / Off
       - **SAVE SETTINGS:** Long press button to save menu settings
       - **EXIT MENU**
2.  **OSCILOSCOPE:** Automatically engages after 10 seconds to visualize the current waveform

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

## MIDI Implementation (CC Chart)

LISA responds to the following Control Change (CC) messages on the selected MIDI Channel:

| CC #    | Parameter                                              |
| :------ | :----------------------------------------------------- |
| **7**   | Master Volume                                          |
| **8**   | Engine Select                                          |
| **9**   | Timbre                                                 |
| **10**  | Color                                                  |
| **11**  | Envelope Attack                                        |
| **12**  | Envelope Release                                       |
| **15**  | FM Modulation                                          |
| **16**  | Timbre Modulation Amount                               |
| **17**  | Color Modulation Amount                                |
| **64**  | Sustain (Hold notes)                                   |
| **71**  | Filter Resonance                                       |
| **74**  | Filter Cutoff                                          |
| **127** | Reset USB to upload from IDE (WARNING: for dev mode)\* |

\* Note: in `dev mode`, CC#127 have 2 specific values it can use:

- 127 -> resets the USB stack and sets the device in "receiving" mode to flash another firmware
- 126 -> resets LISA (app reset)

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
  - Sample rate: 32000 (4 voices) / 44100 (3 voices)
* **RP2350:** - Optimize: Fast (-Ofast)
  - Sample rate: 48000

## Schematic & Wiring

For this project I use RP2040 Zero model, so adjust GPIO numbers for your board.

### 1. Audio Output (I2S DAC)

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

## License

This project is licensed under the GNU General Public License v3.0 (GPLv3).

### Copyright

* Original work (VIJA synthesizer v1.0.2)
  Copyright (c) 2025 Vadims Maksimovs
* Modifications and additional features (LISA)
  Copyright (c) 2026 Dr Schlange

All modifications and newly added features are original work by Dr Schlange and are distributed under the GNU GPLv3 as part of this derivative project.

### Third-Party Components

This project depends on the following external components:

* Mutable Instruments Braids / stmlib\
  Copyright (c) 2020 Emilie Gillet\
  Licensed under the MIT License
* Braids port for Raspberry Pico\
  Copyright (c) 2025 Mark Washeim\
  Licensed under the MIT License

These components are not distributed as part of this repository. They are separate projects, each licensed under their respective terms.

### Attribution

LISA is a hard fork of the VIJA synthesizer v1.0.2, originally created by Vadims Maksimovs.
This project is an independent fork and is not affiliated with or endorsed by the original authors.

## Version history

- 2026-03-24 - LISA v0.0.0 hard-fork from VIJA
- 2026-02-26 - VIJA v1.0.2
- 2026-02-03 - VIJA v1.0.1
- 2026-02-02 - VIJA First release v1.0
