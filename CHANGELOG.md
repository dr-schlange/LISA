# CHANGELOG

## 0.1.0

## Features

## Changes

* Voices pitch is now float instead of an int

## 0.0.1 -- Initial Version

First LISA release since fork from Vija v1.0.2

### Features

* Refactoring code to isolate each control part
* Add Kinetic parameters
* Add all parameters quick access from oscilloscope display
* Add parameter/potentiometer configuration
* Add new config parameter save & load
* Add MIDI controller mode
* Add LIVE engine: live dynamic wavetables with bilinear interpolation using timbre/color
* Add dedicated oscilloscope for LIVE engine
* Add gain parameter
* Add fm slew parameter (control slew between FM jumps if FM is modulated)
* Adapt original VIJA attack/release time and computation
* Pass most of computation in the audio loop to fixed point
* USB MIDI, UI, Controls on core0, audio on core1
