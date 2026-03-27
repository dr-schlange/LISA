/*
  LISA (v0.0.1)

  Copyright (c) 2025 Dr Schlange
  Licensed under GNU GPLv3
*/
#pragma once

#define LISA_VERSION "v0.0.1"
#define SETTINGS_FILE "/lisa_settings.json"


#define USE_UART_MIDI 0  // 0 = USB MIDI, 1 = UART MIDI

// Voices config (audio block and sample rate are related)
#define MAX_VOICES 4
#define AUDIO_BLOCK 32
#define SAMPLE_RATE 32000

// Screen
#define SSD1306 1  //ssd1306 display
// #define SH110X 1// sh110x display
#define USE_SCREEN 1
#define OLED_SDA 0
#define OLED_SCL 1
#define SCOPE_WIDTH 128
#define SCREEN_REFRESH_TIME 60
#define IDLETIME_BEFORE_ENGINE_SELECT_MS 5000
#define IDLETIME_BEFORE_SCOPE_DISPLAY_MS 10000
#define SAVED_DISPLAY_MS 800

// Buttons, Encoders
#define BUTTON_DEBOUNCE_MS 200
#define ENCODER_DEBOUNCE_COUNT 4


// I2S Config
#define I2S_DATA_PIN 9
#define I2S_BCLK_PIN 10

// Init values for some parameters
#define INIT_CUTOFF (32767 / 4)
#define INIT_RESONANCE (32767 / 2)

// -----------
// GPIO config
// -----------

// Encoder pins
#define ENCODER_CLK 2
#define ENCODER_DT 3
#define ENCODER_SW 4
#define LONG_PRESS_MS 1000

// MIDI pins
#define MIDI_UART_RX 13

#define POT_TIMBRE A0      // GPIO26
#define POT_COLOR A1       // GPIO27
#define POT_TIMBRE_MOD A2  // GPIO28
#define POT_COLOR_MOD A3   // GPIO29
// RPI Pico W doen't have GPIO29
#if defined(ARDUINO_RASPBERRY_PI_PICO) || defined(ARDUINO_RASPBERRY_PI_PICO_W)
#define HAS_4_POTS 0
#else
#define HAS_4_POTS 1
#endif
