# CHANGELOG

## 0.3.0

### Features

- Add peak envelope computation as feature on the rendered sound (transmitted via MIDI to listeners)

## 0.2.0

### Features

- Add level-mix for each wavetable
- Add detune parameter and change the way detune is applied for unison mode
- Add special command on CC 126 to pull LISA current configuration
- Add new mode for writing in the wavetables: scroll, manual, and manual interpolated. Scroll pushes the new values at the end of the wavetable all the time, manual lets you target a specific index where to write in the wavetable, and manual interpolated let's you target a specific index, but interpolate values between the old and new position.
- Add Python/numpy simulator for the LISA live engine that you can load direcly in Nallely

## 0.1.0

### Features

- Add unison mode
- Add mono mode

### Changes

- Voices pitch is now float instead of an int

### Fixes

- Fix sneaky bug on filter type selection

## 0.0.1 -- Initial Version

First LISA release since fork from Vija v1.0.2

### Features

- Refactoring code to isolate each control part
- Add Kinetic parameters
- Add all parameters quick access from oscilloscope display
- Add parameter/potentiometer configuration
- Add new config parameter save & load
- Add MIDI controller mode
- Add LIVE engine: live dynamic wavetables with bilinear interpolation using timbre/color
- Add dedicated oscilloscope for LIVE engine
- Add gain parameter
- Add fm slew parameter (control slew between FM jumps if FM is modulated)
- Adapt original VIJA attack/release time and computation
- Pass most of computation in the audio loop to fixed point
- USB MIDI, UI, Controls on core0, audio on core1
