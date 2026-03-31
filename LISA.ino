
/*
  LISA (v0.0.1)

  Original work on VIJA:
  Copyright (c) 2025 Vadims Maksimovs

  Modifications:
  Copyright (c) 2026 Dr Schlange

  This file has been modified from the original version VIJA v1.0.2.
  Licensed under GNU GPLv3.

  Raspberry PICO polyphonic synthesizer based on Mutable Instruments Braids macro oscillator
  in semi-modular format.

  Features:
  - 40+ digital oscillator engines
  - Polyphonic, per-sample AR envelopes
  - USB or UART MIDI input
  - Filter (SVF)
  - OLED display with menu system & oscilloscope
  - Synth controls via potentiometers or MIDI CC

  Hardware:
  - RP2040 or RP2350 board, I2S PCM5102 DAC, SSD1306 OLED, rotary encoder with button, 2 pots, 2 cv jacks or 2 more pots
  - MIDI via USB or UART

  For this project I use RP2040 Zero model, so adjust GPIO numbers to your board.

  Compilation:

  RP2040: - Optimize: Optimize Even More (-O3)
          - CPU Speed: 200-240mhz (Overclock) depending on the sample rate and needed voice count
          - Sample rate: 32000 (4 voices) / 44100 (3 voices)
  RP2350:
         - Optimize: Optimize Even More (-O3)
         - Sample rate: 48000

  Software:
  - BRAIDS and STMLIB libraries ported by Mark Washeim:
    https://github.com/poetaster/arduinoMI
    MIT License

  - stmlib, braids source libs
    Copyright (c) 2020 (emilie.o.gillet@gmail.com)
    MIT License
*/
#include <Arduino.h>
#include <I2S.h>
#include <STMLIB.h>
#include <BRAIDS.h>
#include <pico/stdlib.h>
#include "constants_config.h"
#include "voices.h"
#include "encoder.h"
#include "global_state.h"
#include "midi.h"
#include "ui.h"
#include "controls.h"
#include "settings.h"


// Synth states & global vars
#if USE_SCREEN
static UIState ui_state = UIStateNew();
#endif

// static RuntimeState runtime_state = GlobalStateNew();
static RuntimeState runtime_state;
static Voice voices[MAX_VOICES];

static I2S i2s_output(OUTPUT);
static braids::Svf global_filter;

// Audio engine
void __not_in_flash_func(update_audio)() {

  if (runtime_state.engine_idx != runtime_state.last_engine_idx) {
    braids::MacroOscillatorShape shape =
      (braids::MacroOscillatorShape)runtime_state.engine_idx;

    for (int v = 0; v < MAX_VOICES; v++)
      voices[v].osc.set_shape(shape);

    runtime_state.last_engine_idx = runtime_state.engine_idx;
  }

  static float attackCoef = 0.f;
  static float releaseCoef = 0.f;
  if (runtime_state.env_params_changed) {
    attackCoef = 1.0f - expf(-1.0f / (SAMPLE_RATE * runtime_state.env_attack.value));
    releaseCoef = 1.0f - expf(-1.0f / (SAMPLE_RATE * runtime_state.env_release.value));
    runtime_state.env_params_changed = false;
  }

  float mix[AUDIO_BLOCK] = { 0 };

  static float fm_slew = 0.0f;
  static float timb_slew = 0.0f;
  static float color_slew = 0.0f;
  static float fm_target = 0.f;

  if (runtime_state.midi_enabled) {
    fm_target = runtime_state.fm_mod.value;
  } else if (runtime_state.cv_mod1_enabled) {
    fm_target = 0.0f;
  } else if (runtime_state.filter_enabled) {
    fm_target = 0.0f;
  } else {
    fm_target = runtime_state.fm_mod.value;
  }

  // float timb_target = runtime_state.midi_enabled ? runtime_state.timbre_mod.value
  //                     : runtime_state.cv_mod1_enabled    ? runtime_state.timbre_mod.value
  //                                                : 0.0f;

  // float color_target = runtime_state.midi_enabled ? runtime_state.color_mod.value
  //                      : runtime_state.cv_mod1_enabled    ? runtime_state.color_mod.value
  //                                                 : 0.0f;
  float timb_target = runtime_state.timbre_mod.value;
  float color_target = runtime_state.color_mod.value;

  auto apply_stable_slew = [](float &current, float target, float coefficient) {
    float diff = target - current;
    float abs_diff = fabsf(diff);

    if (abs_diff < 0.005f) {
      if (target == 0.0f && abs_diff < 0.01f) current = 0.0f;
      return;
    }

    if (abs_diff < 0.001f) {
      current = target;
    } else {
      current += diff * coefficient;
    }
  };

  apply_stable_slew(fm_slew, fm_target, 0.05f);
  apply_stable_slew(timb_slew, timb_target, 0.01f);
  apply_stable_slew(color_slew, color_target, 0.01f);

  const float block_gain = runtime_state.master_volume.value * 0.25f;

  for (int v = 0; v < MAX_VOICES; v++) {
    Voice &voice = voices[v];

    if (!voice.active && !voice.sustained && voice.env < 0.0001f)
      continue;

    voice.vel_smoothed += (voice.velocity - voice.vel_smoothed) * 0.25f;

    float pitch = voice.pitch * 128.0f + fm_slew * 1536.0f;
    voice.osc.set_pitch(pitch);

    float t = constrain(runtime_state.timbre.value + timb_slew, 0.0f, 1.0f);
    float m = constrain(runtime_state.color.value + color_slew, 0.0f, 1.0f);
    voice.osc.set_parameters(t * 32767.0f, m * 32767.0f);

    if (voice.active && !voice.last_trig)
      voice.osc.Strike();

    voice.last_trig = voice.active;
    voice.osc.Render(voice.sync_buffer, voice.buffer, AUDIO_BLOCK);

    float envTarget = (voice.active || voice.sustained) ? 1.0f : 0.0f;
    float coef = envTarget ? attackCoef : releaseCoef;

    for (int i = 0; i < AUDIO_BLOCK; i++) {
      voice.env += (envTarget - voice.env) * coef;
      if (voice.env < 0.0001f) voice.env = 0.0f;

      mix[i] += (voice.buffer[i] * 0.000030517578125f) * (voice.env * voice.vel_smoothed * block_gain);
    }
  }

#if USE_SCREEN
  scope_fill(&ui_state, mix, runtime_state.oscilloscope_enabled);
#endif

  static float cut_slew = 0.0f;
  static float res_slew = 0.0f;
  static float mix_slew = 0.0f;

  float cut_t = runtime_state.cutoff.value * 32767;
  float res_t = runtime_state.resonance.value * 32767.f;
  float mix_t = runtime_state.filter_enabled ? 1.0f : 0.0f;

  cut_slew += (cut_t - cut_slew) * 0.05f;
  res_slew += (res_t - res_slew) * 0.05f;
  mix_slew += (mix_t - mix_slew) * 0.01f;

  global_filter.set_frequency((uint16_t)cut_slew);
  global_filter.set_resonance((uint16_t)res_slew);

  const float dry_scale = (1.0f - mix_slew) * 32767.0f;
  const float wet_scale = mix_slew;

  for (int i = 0; i < AUDIO_BLOCK; i++) {
    float dry_f = mix[i];
    int32_t dry_int = (int32_t)(dry_f * 32767.0f);
    float wet_f = global_filter.Process(dry_int);
    float mixed_signal = (dry_f * dry_scale) + (wet_f * wet_scale);
    int16_t s = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, mixed_signal));
    i2s_output.write16(s, s);
  }
}

void handle_menu(RuntimeState *gstate) {
  Encoder *encoder = &(gstate->encoder);

  const int8_t step = encoder_decode_step(encoder);
  if (step) {
    // Encoder rotation reaction depending on display state
    switch (gstate->display_state) {
      case ENGINE_SELECT_MODE:
        gstate->engine_idx = (gstate->engine_idx + step + NUM_ENGINES) % NUM_ENGINES;
        SCHEDULE_REFRESH(gstate);
        break;

      case ENGINE_SETTINGS_CONFIG:
        switch (encoder->state) {
          case VOLUME_ADJUST:
            gstate->master_volume.value = constrain(gstate->master_volume.value + step * 0.01f, 0.f, 1.f);
            break;
          case ATTACK_ADJUST:
            gstate->env_attack.value = constrain(gstate->env_attack.value + step * 0.01f, 0.001f, 1.f);
            gstate->env_params_changed = true;
            break;
          case RELEASE_ADJUST:
            gstate->env_release.value = constrain(gstate->env_release.value + step * 0.01f, 0.01f, 2.f);
            gstate->env_params_changed = true;
            break;
          case FILTER_TOGGLE:
            gstate->filter_enabled = !gstate->filter_enabled;
            gstate->cv_mod1_enabled = false;
            gstate->cv_mod2_enabled = false;
            break;
          case MIDI_MOD:
            gstate->midi_enabled = !gstate->midi_enabled;
            break;
          case CV_MOD1:
            gstate->cv_mod1_enabled = !gstate->cv_mod1_enabled;
            gstate->filter_enabled = false;
            gstate->cv_mod2_enabled = false;
            break;
          case CV_MOD2:
            gstate->cv_mod2_enabled = !gstate->cv_mod2_enabled;
            gstate->cv_mod1_enabled = false;
            gstate->filter_enabled = false;
            break;
          case MIDI_CH:
            gstate->midi_ch = constrain(gstate->midi_ch + step, 1, 16);
            break;
          case SCOPE_TOGGLE:
            TOGGLE_OSCILLOSCOPE(gstate);
            if (IS_OSCILLOSCOPE_OFF(gstate) && IS_OSCILLOSCOPE_MODE(gstate)) {
              gstate->display_state = ENGINE_SELECT_MODE;
#if USE_SCREEN
              ui_state.scope_ready = false;
#endif
            }
            break;
          default:
            gstate->display_state = ENGINE_SELECT_MODE;
            gstate->midi_enabled = true;
            gstate->cv_mod1_enabled = false;
            gstate->cv_mod2_enabled = false;
            gstate->filter_enabled = false;
            SCHEDULE_REFRESH(gstate);
            break;
        }
        SCHEDULE_REFRESH(gstate);
        break;

      case OSCILLOSCOPE_MODE:
        gstate->display_state = ENGINE_SELECT_MODE;
        SCHEDULE_REFRESH(gstate);
        break;

      case ALL_PARAMS_MODE:
        // lock the already mapped pots
        lock_mapped_pots(gstate, true);
        if (gstate->pots_row_state == ROW_EDIT_ENGINE) {
          gstate->engine_idx = (gstate->engine_idx + step + NUM_ENGINES) % NUM_ENGINES;
          SCHEDULE_REFRESH(gstate);
        } else {
          int next_row = (int)(((uint8_t)gstate->pots_row_state) - step);
          if (next_row < 0) {
            gstate->pots_row_state = (PotsRow)(ROW_NUM - 1);
          } else {
            gstate->pots_row_state = (PotsRow)(next_row % ROW_NUM);
          }
          switch (gstate->pots_row_state) {
            case ROW_GENERAL:
              map_abc_pots(gstate, &(gstate->master_volume), &(gstate->b1), &(gstate->b2));
              break;
            case ROW_TIMBRE:
              map_abc_pots(gstate, &(gstate->timbre), &(gstate->timbre_mod), &(gstate->fm_mod));
              break;
            case ROW_COLOR:
              map_abc_pots(gstate, &(gstate->color), &(gstate->color_mod), &(gstate->b3));
              break;
            case ROW_FILTER:
              map_abc_pots(gstate,
                           gstate->filter_enabled ? &(gstate->cutoff) : NULL,
                           gstate->filter_enabled ? &(gstate->resonance) : NULL,
                           &(gstate->b4));
              break;
            case ROW_ENVELOPE:
              map_abc_pots(gstate, &(gstate->env_attack), &(gstate->env_release), &(gstate->b5));
              break;
          }
          // lock the new mapped pots
          lock_mapped_pots(gstate, true);
          SCHEDULE_REFRESH(gstate);
          break;
        }
    }
  }

  const EncoderStatus status = gstate->encoder_status;
  if (status == DBL_PRESSED) {
    switch (gstate->display_state) {
      case OSCILLOSCOPE_MODE:
        gstate->display_state = ALL_PARAMS_MODE;
        gstate->pots_row_state = ROW_GENERAL;
        lock_all_parameters(gstate, true);
        map_abc_pots(gstate, &(gstate->master_volume), &(gstate->b1), &(gstate->b2));
        SCHEDULE_REFRESH(gstate);
        break;
      case ENGINE_SETTINGS_CONFIG:
        gstate->display_state = GLOBAL_SETTINGS;
        SCHEDULE_REFRESH(gstate);
        break;
      case GLOBAL_SETTINGS:
        gstate->display_state = ENGINE_SETTINGS_CONFIG;
        SCHEDULE_REFRESH(gstate);
        break;
      case ALL_PARAMS_MODE:
        gstate->display_state = OSCILLOSCOPE_MODE;
        lock_all_parameters(gstate, false);
        map_abc_pots(gstate,
                     &(gstate->timbre),
                     &(gstate->color),
                     gstate->filter_enabled ? &(gstate->cutoff) : NULL);
        SCHEDULE_REFRESH(gstate);
        break;
    }
  }

  if (status == PRESSED) {
    // Encoder sw press reaction depending on the mode
    switch (gstate->display_state) {
      case ENGINE_SELECT_MODE:
        gstate->display_state = ENGINE_SETTINGS_CONFIG;
        gstate->encoder.state = VOLUME_ADJUST;
        SCHEDULE_REFRESH(gstate);
        break;

      case ENGINE_SETTINGS_CONFIG:
        gstate->encoder.state = (EncoderState)((gstate->encoder.state + 1) % ENCODER_STATE_NUM);
        SCHEDULE_REFRESH(gstate);
        break;

      case OSCILLOSCOPE_MODE:
        gstate->display_state = ENGINE_SETTINGS_CONFIG;
        gstate->encoder.state = (EncoderState)((gstate->encoder.state + 1) % ENCODER_STATE_NUM);
        SCHEDULE_REFRESH(gstate);
        break;

      case ALL_PARAMS_MODE:
        switch (gstate->pots_row_state) {
          case ROW_ENGINE_SELECT:
            gstate->pots_row_state = ROW_EDIT_ENGINE;
            break;
          case ROW_EDIT_ENGINE:
            gstate->pots_row_state = ROW_ENGINE_SELECT;
            break;
          default:
            lock_mapped_pots(gstate, !(gstate->A->screen_locked));
            break;
        }
        SCHEDULE_REFRESH(gstate);
        break;
    }
  }
}

// ===============================
// Setup and main loop for Core0
// ==============================
#if DEBUG
static inline void setup_debug_serial() {
  Serial.begin(115200);
}
#endif

static inline void setup_serial() {
  Serial1.setRX(MIDI_UART_RX);
  Serial1.begin(31250);
}

static inline void setup_pins() {
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
}

void setup() {
#if DEBUG
  setup_debug_serial();
#endif
  setup_LittleFS();
  setup_USB();
  setup_serial();
  setup_pins();
#if USE_SCREEN
  setup_display();
#endif
  load_settings(&runtime_state);
  SET_SYSTEM_READY(&runtime_state);
}

void loop() {
  if (!runtime_state.system_ready) {
    yield();  // Wait for Core 1 to finish DSP initialisation & init global state
    return;
  }

  // We need to update first the state for SW
  runtime_state.encoder_status = encoder_sw_status(&(runtime_state.encoder));
  handle_save(&runtime_state);
  handle_control(&runtime_state);
  handle_menu(&runtime_state);
  handle_MIDI(&runtime_state, voices);
#if USE_SCREEN
  draw_ui(&runtime_state, &ui_state);
#endif

  yield();
}

//===============================
// Setup and main loop for Core1
//===============================

static inline void setup_soundcard() {
  i2s_output.setFrequency(SAMPLE_RATE);
  i2s_output.setDATA(I2S_DATA_PIN);
  i2s_output.setBCLK(I2S_BCLK_PIN);
  i2s_output.begin();
}

static inline void setup_voices() {
  for (int v = 0; v < MAX_VOICES; v++) {
    voices[v].osc.Init(SAMPLE_RATE);
    voices[v].active = false;
  }
}

static inline void setup_global_filter() {
  global_filter.Init();
  global_filter.set_mode(braids::SVF_MODE_LP);
  global_filter.set_frequency(INIT_CUTOFF);
  global_filter.set_resonance(INIT_RESONANCE);
}

void setup1() {
  setup_soundcard();
  init_global_state(&runtime_state);
  setup_voices();
  setup_global_filter();
}

void loop1() {
  if (i2s_output.availableForWrite() >= AUDIO_BLOCK * 4) {
    update_audio();
  }
  SET_SYSTEM_READY(&runtime_state);
}
