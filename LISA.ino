
/*
  LISA (v0.3.0)

  Original work on VIJA:
  Copyright (c) 2025 Vadims Maksimovs

  Modifications:
  Copyright (c) 2026 Dr Schlange

  This file has been modified from the original version VIJA v1.0.2.
  Licensed under GNU GPLv3.

  Raspberry PICO polyphonic synthesizer based on Mutable Instruments Braids
  macro oscillator in semi-modular format.

  Features:
  - 40+ digital oscillator engines
  - Polyphonic, per-sample AR envelopes
  - USB or UART MIDI input
  - Filter (SVF)
  - OLED display with menu system & oscilloscope
  - Synth controls via potentiometers or MIDI CC

  Hardware:
  - RP2040 or RP2350 board, I2S PCM5102 DAC, SSD1306 OLED, rotary encoder with
  button, 2 pots, 2 cv jacks or 2 more pots
  - MIDI via USB or UART

  For this project I use RP2040 Zero model, so adjust GPIO numbers to your
  board.

  Compilation:

  RP2040: - Optimize: Optimize Even More (-O3)
          - CPU Speed: 200-240mhz (Overclock) depending on the sample rate and
  needed voice count
          - Sample rate: 32000 (up to 8 voice depending on the engine) / 44100
  (up to 6 voices depending on the engine)
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
// clang-format off
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
#include "features.h"
#include "ui.h"
#include "controls.h"
#include "settings.h"
// clang-format on

// Synth states & global vars
#if USE_SCREEN
static UIState ui_state = UIStateNew();
#endif

// static RuntimeState runtime_state = GlobalStateNew();
static RuntimeState runtime_state;
static VoiceAllocator voices(&runtime_state);

static I2S i2s_output(OUTPUT);
static braids::Svf global_filter;
static braids::SvfMode previous_filter_mode;

// Audio engine
void __not_in_flash_func(update_audio)() {

  if (runtime_state.engine_idx != runtime_state.last_engine_idx) {
    bool use_streaming =
        (runtime_state.engine_idx >= braids::MACRO_OSC_SHAPE_LAST);
    if (use_streaming) {
      voices.setLiveMode(use_streaming);
    } else {
      voices.setEngine((braids::MacroOscillatorShape)runtime_state.engine_idx);
    }
    runtime_state.last_engine_idx = runtime_state.engine_idx;
  }

  int32_t mix[AUDIO_BLOCK] = {0};

  voices.renderAllVoices(mix);

#if USE_SCREEN
  scope_fill(&ui_state, mix, runtime_state.oscilloscope_enabled);
#endif
  // features_compute_peak(mix, AUDIO_BLOCK);

  static int32_t cut_slew = 0, res_slew = 0, mix_slew = 0;

  const int32_t cut_t = (int32_t)(runtime_state.cutoff.value * 32767.f);
  const int32_t res_t = (int32_t)(runtime_state.resonance.value * 32767.f);
  const int32_t mix_t = runtime_state.filter_enabled
                            ? 32767
                            : 0; // 32767 stands for 1: ((1 << 15) - 1)

  cut_slew +=
      ((cut_t - cut_slew) * 1638) >> 15; // 1638 =  (int32_t)(0.05f * 32767.f)
  res_slew += ((res_t - res_slew) * 1638) >> 15;
  mix_slew +=
      ((mix_t - mix_slew) * 327) >> 15; // 327 =  (int32_t)(0.01f * 32767.f)

  global_filter.set_frequency((uint16_t)cut_slew);
  global_filter.set_resonance((uint16_t)res_slew);

  braids::SvfMode filter_type = (braids::SvfMode)midi_get_group(
      runtime_state.filter_type.value * 127.f, 3);
  if (filter_type != previous_filter_mode) {
    global_filter.set_mode(filter_type);
    previous_filter_mode = filter_type;
  }

  const int32_t dry_scale = 32767 - mix_slew;
  const int32_t wet_scale = mix_slew;

  static float pan_current = runtime_state.panning.value;
  pan_current += (runtime_state.panning.value - pan_current) * (0.0625f);
  const uint8_t idx = (uint8_t)(pan_current * 63.f);

  for (int i = 0; i < AUDIO_BLOCK; i++) {
    int32_t dry_int = mix[i];
    int16_t wet_filter = global_filter.Process(dry_int);
    int32_t mixed_signal =
        ((dry_int * dry_scale) >> 15) + ((wet_filter * wet_scale) >> 15);
    int16_t s = constrain(mixed_signal, -32767, 32767);
    int16_t s_left = (int16_t)(((int32_t)s * braids::wav_sine[64 - idx]) >> 15);
    int16_t s_right = (int16_t)(((int32_t)s * braids::wav_sine[idx]) >> 15);
    i2s_output.write16(s_left, s_right);
  }
}

void handle_menu(RuntimeState *gstate) {
  Encoder *encoder = &(gstate->encoder);

  const int8_t step = encoder_decode_step(encoder);
  if (step) {
    // Encoder rotation reaction depending on display state
    switch (gstate->display_state) {
    case ENGINE_SELECT_MODE:
      gstate->engine_idx =
          (gstate->engine_idx + step + NUM_ENGINES) % NUM_ENGINES;
      send_midi_cc(MIDI_ENGINE_SEL,
                   (uint8_t)((gstate->engine_idx * 127.f) / (NUM_ENGINES - 1)),
                   gstate->midi_ch);
      SCHEDULE_REFRESH(gstate);
      break;

    case ENGINE_SETTINGS_CONFIG:
      switch (encoder->state) {
      case VOLUME_ADJUST:
        gstate->master_volume.value =
            constrain(gstate->master_volume.value + step * 0.01f, 0.f, 1.f);
        break;
      case ATTACK_ADJUST:
        gstate->env_attack.value =
            constrain(gstate->env_attack.value + step * 0.01f, 0.001f, 1.f);
        break;
      case RELEASE_ADJUST:
        gstate->env_release.value =
            constrain(gstate->env_release.value + step * 0.01f, 0.0f, 1.f);
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
        gstate->engine_idx =
            (gstate->engine_idx + step + NUM_ENGINES) % NUM_ENGINES;
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
          map_abc_pots(gstate, (Parameter *)&(gstate->master_volume),
                       (Parameter *)&(gstate->b1), (Parameter *)&(gstate->b2));
          break;
        case ROW_TIMBRE:
          map_abc_pots(gstate, (Parameter *)&(gstate->timbre),
                       (Parameter *)&(gstate->timbre_mod),
                       (Parameter *)&(gstate->fm_mod));
          break;
        case ROW_COLOR:
          map_abc_pots(gstate, (Parameter *)&(gstate->color),
                       (Parameter *)&(gstate->color_mod),
                       (Parameter *)&(gstate->b3));
          break;
        case ROW_FILTER:
          map_abc_pots(
              gstate,
              gstate->filter_enabled ? (Parameter *)&(gstate->cutoff) : NULL,
              gstate->filter_enabled ? (Parameter *)&(gstate->resonance) : NULL,
              gstate->filter_enabled ? &(gstate->filter_type) : NULL);
          break;
        case ROW_ENVELOPE:
          map_abc_pots(gstate, (Parameter *)&(gstate->env_attack),
                       (Parameter *)&(gstate->env_release),
                       (Parameter *)&(gstate->b4));
          break;
        }
        // lock the new mapped pots
        lock_mapped_pots(gstate, true);
        SCHEDULE_REFRESH(gstate);
        break;
      }
    case GLOBAL_SETTINGS:
      ExtParameter *main_parameter = gstate->glob_settings_edit_param;
      const PotMode pot_mode = glob_get_pot_mode(gstate);
      const ResolutionMode res_mode = glob_get_res_mode(gstate);
      const int8_t param_offset =
          main_parameter == NULL ? -1 : main_parameter - &(gstate->timbre);
      switch (gstate->glob_settings_state) {
      case SETTING_EDIT_RES:
        if (res_mode == RES_UNKNOWN) {
          set_resolution_mode(gstate, RES_CATCHUP);
        } else {
          set_resolution_mode(
              gstate, (ResolutionMode)((((uint8_t)res_mode) + step + RES_NUM) %
                                       RES_NUM));
        }
        break;
      case SETTING_EDIT_MODE:
        if (pot_mode == POT_UNKNOWN) {
          set_pot_mode(gstate, POT_NORMAL);
        } else {
          set_pot_mode(gstate,
                       (PotMode)((((uint8_t)pot_mode) + step + POT_MODE_NUM) %
                                 POT_MODE_NUM));
        }
        break;
      case SETTING_EDIT_PARAMETER:
        if (param_offset + step >= ALL_PARAMETERS_NUM) {
          gstate->glob_settings_edit_param =
              (&(gstate->timbre)) + (ALL_PARAMETERS_NUM - 1);
        } else if (param_offset + step < 0) {
          gstate->glob_settings_edit_param = NULL;
        } else {
          gstate->glob_settings_edit_param =
              (main_parameter == NULL ? &(gstate->timbre) - 1
                                      : main_parameter) +
              step;
        }
        break;
      default:
        gstate->glob_settings_state =
            (GlobalSettings)((((uint8_t)gstate->glob_settings_state) +
                              SETTING_NUM - step) %
                             SETTING_NUM);
        break;
      }
      if (glob_get_pot_mode(gstate) == POT_KINETIC) {
        if (gstate->glob_settings_edit_param == NULL) {
          map_abc_pots(gstate, &(gstate->timbre.kinetic.mass),
                       &(gstate->timbre.kinetic.damping),
                       &(gstate->timbre.kinetic.stiffness));
          lock_mapped_pots(gstate, true);
        } else {
          map_abc_pots(gstate,
                       &(gstate->glob_settings_edit_param->kinetic.mass),
                       &(gstate->glob_settings_edit_param->kinetic.damping),
                       &(gstate->glob_settings_edit_param->kinetic.stiffness));
          lock_mapped_pots(gstate, true);
        }
      }
      break;
    }
  }

  const EncoderStatus status = gstate->encoder_status;
  if (status == DBL_PRESSED) {
    switch (gstate->display_state) {
    case OSCILLOSCOPE_MODE:
      gstate->display_state = ALL_PARAMS_MODE;
      gstate->pots_row_state = ROW_GENERAL;
      lock_all_parameters(gstate, true);
      map_abc_pots(gstate, (Parameter *)&(gstate->master_volume),
                   (Parameter *)&(gstate->b1), (Parameter *)&(gstate->b2));
      SCHEDULE_REFRESH(gstate);
      break;
    case ENGINE_SETTINGS_CONFIG:
      gstate->display_state = GLOBAL_SETTINGS;
      if (glob_get_pot_mode(gstate) == POT_KINETIC) {
        if (gstate->glob_settings_edit_param == NULL) {
          map_abc_pots(gstate, &(gstate->timbre.kinetic.mass),
                       &(gstate->timbre.kinetic.damping),
                       &(gstate->timbre.kinetic.stiffness));
          lock_mapped_pots(gstate, true);
        } else {
          map_abc_pots(gstate,
                       &(gstate->glob_settings_edit_param->kinetic.mass),
                       &(gstate->glob_settings_edit_param->kinetic.damping),
                       &(gstate->glob_settings_edit_param->kinetic.stiffness));
          lock_mapped_pots(gstate, true);
        }
      }
      SCHEDULE_REFRESH(gstate);
      break;
    case GLOBAL_SETTINGS:
      gstate->display_state = ENGINE_SETTINGS_CONFIG;
      gstate->glob_settings_state = SETTING_PARAMETER;
      lock_all_parameters(gstate, glob_get_res_mode(gstate) == RES_CATCHUP);
      map_abc_pots(
          gstate, (Parameter *)&(gstate->timbre), (Parameter *)&(gstate->color),
          gstate->filter_enabled ? (Parameter *)&(gstate->cutoff) : NULL);
      SCHEDULE_REFRESH(gstate);
      break;
    case ALL_PARAMS_MODE:
      gstate->display_state = OSCILLOSCOPE_MODE;
      lock_all_parameters(gstate, glob_get_res_mode(gstate) == RES_CATCHUP);
      map_abc_pots(
          gstate, (Parameter *)&(gstate->timbre), (Parameter *)&(gstate->color),
          gstate->filter_enabled ? (Parameter *)&(gstate->cutoff) : NULL);
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
      gstate->encoder.state =
          (EncoderState)((gstate->encoder.state + 1) % ENCODER_STATE_NUM);
      SCHEDULE_REFRESH(gstate);
      break;

    case OSCILLOSCOPE_MODE:
      gstate->display_state = ENGINE_SETTINGS_CONFIG;
      gstate->encoder.state =
          (EncoderState)((gstate->encoder.state + 1) % ENCODER_STATE_NUM);
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
    case GLOBAL_SETTINGS:
      if (gstate->glob_settings_state > SETTING_NUM) {
        gstate->glob_settings_state =
            (GlobalSettings)(((uint8_t)gstate->glob_settings_state) -
                             (SETTING_NUM + 1));
      } else {
        gstate->glob_settings_state =
            (GlobalSettings)(((uint8_t)gstate->glob_settings_state) +
                             (SETTING_NUM +
                              1)); // switch to the edit hidden state
      }
      break;
    }
  }
}

// ===============================
// Setup and main loop for Core0
// ==============================
#if DEBUG
static inline void setup_debug_serial() { Serial.begin(115200); }
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
    yield(); // Wait for Core 1 to finish DSP initialisation & init global state
    return;
  }

  // We need to update first the state for SW
  runtime_state.encoder_status = encoder_sw_status(&(runtime_state.encoder));
  handle_save(&runtime_state);
  handle_control(&runtime_state);
  handle_menu(&runtime_state);
  handle_MIDI(&runtime_state, &voices);
  // features_send(runtime_state.midi_ch);
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

static inline void setup_global_filter() {
  global_filter.Init();
  global_filter.set_mode(braids::SVF_MODE_LP);
  global_filter.set_frequency(INIT_CUTOFF);
  global_filter.set_resonance(INIT_RESONANCE);
  previous_filter_mode = braids::SVF_MODE_LP;
}

void setup1() {
  setup_soundcard();
  init_global_state(&runtime_state);
  setup_global_filter();
}

void loop1() {
  if (i2s_output.availableForWrite() >= AUDIO_BLOCK * 4) {
    update_audio();
  }
  SET_SYSTEM_READY(&runtime_state);
}
