
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
#include "global_state.h"
#include "ui.h"
#include "buttons.h"
#include "settings.h"
#include "midi.h"
#include "voices.h"
#include "constants_config.h"

#define DEBUG false
#if DEBUG
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define DEBUG_PRINTLN(...)
#endif

// Synth states & global vars
#if USE_SCREEN
static UIState ui_state = UIStateNew();
#endif

static RuntimeState runtime_state = GlobalStateNew();
static Voice voices[MAX_VOICES];

static I2S i2s_output(OUTPUT);
static braids::Svf global_filter;

// Audio engine
void __not_in_flash_func(updateAudio)() {

  if (runtime_state.engine_idx != runtime_state.last_engine_idx) {
    braids::MacroOscillatorShape shape =
      (braids::MacroOscillatorShape)runtime_state.engine_idx;

    for (int v = 0; v < MAX_VOICES; v++)
      voices[v].osc.set_shape(shape);

    runtime_state.last_engine_idx = runtime_state.engine_idx;
  }

  if (runtime_state.env_params_changed) {
    runtime_state.attackCoef = 1.0f - expf(-1.0f / (SAMPLE_RATE * runtime_state.env_attack_s));
    runtime_state.releaseCoef = 1.0f - expf(-1.0f / (SAMPLE_RATE * runtime_state.env_release_s));
    runtime_state.env_params_changed = false;
  }

  float mix[AUDIO_BLOCK] = { 0 };

  static float fm_slew = 0.0f;
  static float timb_slew = 0.0f;
  static float color_slew = 0.0f;

  if (runtime_state.midi_mod) {
    runtime_state.fm_target = runtime_state.fm_mod;
  } else if (runtime_state.cv_mod1) {
    runtime_state.fm_target = 0.0f;
  } else if (runtime_state.filter_enabled) {
    runtime_state.fm_target = 0.0f;
  } else {
    runtime_state.fm_target = runtime_state.fm_mod;
  }

  float timb_target = runtime_state.midi_mod  ? runtime_state.timb_mod_midi
                      : runtime_state.cv_mod1 ? runtime_state.timb_mod_cv
                                              : 0.0f;

  float color_target = runtime_state.midi_mod  ? runtime_state.color_mod_midi
                       : runtime_state.cv_mod1 ? runtime_state.color_mod_cv
                                               : 0.0f;

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

  apply_stable_slew(fm_slew, runtime_state.fm_target, 0.05f);
  apply_stable_slew(timb_slew, timb_target, 0.01f);
  apply_stable_slew(color_slew, color_target, 0.01f);

  const float block_gain = runtime_state.master_volume * 0.25f;

  for (int v = 0; v < MAX_VOICES; v++) {
    Voice &voice = voices[v];

    if (!voice.active && !voice.sustained && voice.env < 0.0001f)
      continue;

    voice.vel_smoothed += (voice.velocity - voice.vel_smoothed) * 0.25f;

    float pitch = voice.pitch * 128.0f + fm_slew * 1536.0f;
    voice.osc.set_pitch(pitch);

    float t = constrain(runtime_state.timbre_in + timb_slew, 0.0f, 1.0f);
    float m = constrain(runtime_state.color_in + color_slew, 0.0f, 1.0f);
    voice.osc.set_parameters(t * 32767.0f, m * 32767.0f);

    if (voice.active && !voice.last_trig)
      voice.osc.Strike();

    voice.last_trig = voice.active;
    voice.osc.Render(voice.sync_buffer, voice.buffer, AUDIO_BLOCK);

    float envTarget = (voice.active || voice.sustained) ? 1.0f : 0.0f;
    float coef = envTarget ? runtime_state.attackCoef : runtime_state.releaseCoef;

    for (int i = 0; i < AUDIO_BLOCK; i++) {
      voice.env += (envTarget - voice.env) * coef;
      if (voice.env < 0.0001f) voice.env = 0.0f;

      mix[i] += (voice.buffer[i] * 0.000030517578125f) * (voice.env * voice.vel_smoothed * block_gain);
    }
  }


#if USE_SCREEN
  static int scope_idx = 0;
  static float scopeSmooth = 0.0f;
  if (runtime_state.oscilloscope_enabled && !ui_state.scope_ready) {
    for (int i = 0; i < AUDIO_BLOCK; i += 4) {
      scopeSmooth += (mix[i] - scopeSmooth) * 0.25f;
      ui_state.scope_buffer_front[scope_idx++] = scopeSmooth;
      if (scope_idx >= SCOPE_WIDTH) {
        memcpy((void *)ui_state.scope_buffer_back,
               (const void *)ui_state.scope_buffer_front,
               sizeof(ui_state.scope_buffer_back));
        ui_state.scope_ready = true;
        scope_idx = 0;
        break;
      }
    }
  }
#endif

  static float cut_slew = 0.0f;
  static float res_slew = 0.0f;
  static float mix_slew = 0.0f;

  float cut_t = runtime_state.filter_cutoff_cc * (32767.0f / 127.0f);
  float res_t = runtime_state.filter_resonance_cc * (32767.0f / 127.0f);
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


void handle_control(RuntimeState *gstate) {
  static float smoothT = 0.5f;
  static float smoothC = 0.5f;
  static float smoothTMod = 0.0f;
  static float smoothCMod = 0.0f;
  static float smoothCut = 0.5f;
  static float smoothRes = 0.25f;
  static unsigned long last_pot_read = 0;

  if (millis() - last_pot_read > 4) {
    last_pot_read = millis();

    float rT = analogRead(POT_TIMBRE) / 1023.0f;
    float rC = analogRead(POT_COLOR) / 1023.0f;
    float srcT = analogRead(POT_TIMBRE_MOD) / 1023.0f;
#if HAS_4_POTS
    float srcC = analogRead(POT_COLOR_MOD) / 1023.0f;
#else
    float srcC = 0.5f;
#endif

    const float SMOOTH_POT = 0.06f;
    gstate->pot_timbre += (rT - gstate->pot_timbre) * SMOOTH_POT;
    gstate->pot_color += (rC - gstate->pot_color) * SMOOTH_POT;

    if (gstate->pot_timbre > 0.999f) gstate->pot_timbre = 1.0f;
    if (gstate->pot_timbre < 0.001f) gstate->pot_timbre = 0.0f;

    if (gstate->pot_color > 0.999f) gstate->pot_color = 1.0f;
    if (gstate->pot_color < 0.001f) gstate->pot_color = 0.0f;

    int valT = (int)(gstate->pot_timbre * 127.0f + 0.5f);
    int valC = (int)(gstate->pot_color * 127.0f + 0.5f);

    if (!gstate->midi_mod) {
      gstate->timbre_locked = false;
      gstate->color_locked = false;
      SCHEDULE_REFRESH(gstate);
    }

    if (gstate->cv_mod1) {

      // --- Smooth the potentiometer inputs (depth controls) ---
      smoothT += (rT - smoothT) * 0.15f;
      smoothC += (rC - smoothC) * 0.15f;

      // --- Smooth the modulation sources ---
      smoothTMod += (srcT - smoothTMod) * 0.1f;  // slower smoothing
      smoothCMod += (srcC - smoothCMod) * 0.1f;

      // --- Apply modulation depth with soft scaling ---
      gstate->timb_mod_cv += ((smoothT * smoothTMod) - gstate->timb_mod_cv) * 0.05f;
      gstate->color_mod_cv += ((smoothC * smoothCMod) - gstate->color_mod_cv) * 0.05f;

      // --- Set base values for other modes ---
      gstate->timbre_in = 0.5f;
      gstate->color_in = 0.5f;
      SCHEDULE_REFRESH(gstate);

    } else if (gstate->midi_mod) {
      smoothT += (rT - smoothT) * 0.15f;
      smoothC += (rC - smoothC) * 0.15f;

      // TIMBRE
      if (gstate->timbre_locked) {
        // catchup!
        // if (fabsf(smoothT - gstate->timbre_in) < 0.02f) {
        //   gstate->timbre_locked = false;
        // }
        // raw!
        static uint8_t last_pot_timbre = 0;
        uint8_t new_timbre = (uint8_t)(smoothT * 127.0f);
        if (new_timbre != last_pot_timbre) {
          gstate->timbre_locked = false;
          last_pot_timbre = new_timbre;
        }
      }
      if (!gstate->timbre_locked) {
        gstate->timbre_in = smoothT;
      }

      // COLOR
      if (gstate->color_locked) {
        // catchup!
        // if (fabsf(smoothC - gstate->color_in) < 0.02f) {
        //   gstate->color_locked = false;
        // }
        // raw!
        static uint8_t last_pot_color = 0;
        uint8_t new_color = (uint8_t)(smoothC * 127.0f);
        if (new_color != last_pot_color) {
          gstate->color_locked = false;
          last_pot_color = new_color;
        }
      }
      if (!gstate->color_locked) {
        gstate->color_in = smoothC;
      }
      SCHEDULE_REFRESH(gstate);
    }

    if (gstate->filter_enabled) {
      // --- Update filter CVs from modulation pots ---
      smoothCut += (srcT - smoothCut) * 0.1f;
      smoothRes += (srcC - smoothRes) * 0.1f;

      static uint8_t last_pot_cut = 0;
      static uint8_t last_pot_res = 0;
      uint8_t new_cut = (uint8_t)(smoothCut * 127.0f);
      if (new_cut != last_pot_cut) {
        gstate->filter_cutoff_cc = new_cut;
        gstate->filter_midi_owned = false;
        last_pot_cut = new_cut;
      }
      uint8_t new_res = (uint8_t)(smoothRes * 127.0f);
      if (new_res != last_pot_res) {
        gstate->filter_resonance_cc = new_res;
        gstate->filter_midi_owned = false;
        last_pot_res = new_res;
      }

      // --- Keep Timbre and Color pots working as default ---
      smoothT += (rT - smoothT) * 0.08f;
      smoothC += (rC - smoothC) * 0.08f;

      // gstate->timbre_in = smoothT;
      // gstate->color_in = smoothC;

      // --- Decay any modulation CV influence smoothly ---
      // gstate->timb_mod_cv *= 0.9f;
      // gstate->color_mod_cv *= 0.9f;

      // --- FM is inactive in filter mode ---  <-- not sure why
      // fm_target = 0.0f;
      SCHEDULE_REFRESH(gstate);
    }

    else if (gstate->cv_mod2) {
      smoothT += (rT - smoothT) * 0.08f;
      smoothC += (rC - smoothC) * 0.08f;
      gstate->timbre_in = smoothT;
      gstate->color_in = smoothC;

      // --- 2. Rolling Average Filter ---
      static float historyT[16];
      static float historyC[16];
      static int histIdx = 0;

      historyT[histIdx] = srcT;
      historyC[histIdx] = srcC;
      histIdx = (histIdx + 1) % 16;

      float avgT = 0, avgC = 0;
      for (int i = 0; i < 16; i++) {
        avgT += historyT[i];
        avgC += historyC[i];
      }
      avgT /= 16.0f;
      avgC /= 16.0f;

      smoothTMod += (avgT - smoothTMod) * 0.05f;
      smoothCMod += (avgC - smoothCMod) * 0.05f;

      // --- 3. Strict Deadzone ---
      const float CV_DEADZONE = 0.15f;
      bool cvT_active = (smoothTMod > CV_DEADZONE);
      bool cvC_active = (smoothCMod > CV_DEADZONE);

      // --- 4. Large-Band Hysteresis for Engines ---
      static float lockT = -1.0f;
      const float ENG_HYST = 0.10f;

      if (cvT_active) {
        if (fabsf(smoothTMod - lockT) > ENG_HYST) {
          float norm = (smoothTMod - CV_DEADZONE) / (1.0f - CV_DEADZONE);
          int new_idx = (int)(norm * (float)NUM_ENGINES);
          new_idx = constrain(new_idx, 0, NUM_ENGINES - 1);

          if (new_idx != gstate->engine_idx) {
            gstate->engine_idx = new_idx;
            lockT = smoothTMod;
            SCHEDULE_REFRESH(gstate);
          }
        }
      } else {
        lockT = -1.0f;
      }

      // --- 5. Large-Band Hysteresis for FM ---
      static float lockC = 0.0f;
      const float FM_HYST = 0.1f;

      if (cvC_active) {
        if (fabsf(smoothCMod - lockC) > FM_HYST) {
          float target_fm = (smoothCMod - CV_DEADZONE) / (1.0f - CV_DEADZONE);
          gstate->fm_mod = constrain(target_fm, 0.0f, 1.0f);
          lockC = smoothCMod;
        }
      } else {
        gstate->fm_mod *= 0.5f;
        if (gstate->fm_mod < 0.01f) {
          gstate->fm_mod = 0.0f;
          lockC = 0.0f;
        }
      }

      gstate->timbre_locked = false;
      gstate->color_locked = false;
    } else if (!gstate->midi_mod) {
      smoothT += (rT - smoothT) * 0.08f;
      smoothC += (rC - smoothC) * 0.08f;
      gstate->timbre_in = smoothT;
      gstate->color_in = smoothC;

      // Zero out all CV-related variables
      gstate->timb_mod_cv = 0.0f;
      gstate->color_mod_cv = 0.0f;
      gstate->fm_mod = 0.0f;

      gstate->timbre_locked = false;
      gstate->color_locked = false;
      SCHEDULE_REFRESH(gstate);
    }
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
        encoder->last_encoder_activity = millis();
        break;

      case ENGINE_SETTINGS_CONFIG:
        switch (encoder->state) {
          case VOLUME_ADJUST:
            gstate->master_volume = constrain(gstate->master_volume + step * 0.01f, 0.f, 1.f);
            break;
          case ATTACK_ADJUST:
            gstate->env_attack_s = constrain(gstate->env_attack_s + step * 0.01f, 0.001f, 1.f);
            gstate->env_params_changed = true;
            break;
          case RELEASE_ADJUST:
            gstate->env_release_s = constrain(gstate->env_release_s + step * 0.01f, 0.01f, 2.f);
            gstate->env_params_changed = true;
            break;
          case FILTER_TOGGLE:
            gstate->filter_enabled = !gstate->filter_enabled;
            // midi_mod = false;
            gstate->cv_mod1 = false;
            gstate->cv_mod2 = false;
            break;
          case MIDI_MOD:
            gstate->midi_mod = !gstate->midi_mod;
            // if (midi_mod) {
            //   cv_mod1 = false;
            //   cv_mod2 = false;
            // }
            break;
          case CV_MOD1:
            gstate->cv_mod1 = !gstate->cv_mod1;
            gstate->filter_enabled = false;
            gstate->cv_mod2 = false;
            // if (cv_mod1) midi_mod = false;
            break;
          case CV_MOD2:
            gstate->cv_mod2 = !gstate->cv_mod2;
            gstate->cv_mod1 = false;
            gstate->filter_enabled = false;
            // if (cv_mod2) midi_mod = false;
            break;
          case MIDI_CH:
            gstate->midi_ch = constrain(gstate->midi_ch + step, 1, 16);
            break;
          case SCOPE_TOGGLE:
            TOGGLE_OSCILLOSCOPE(gstate);
            if (IS_OSCILLOSCOPE_OFF(gstate) && IS_OSCILLOSCOPE_MODE(gstate)) {
              SWITCHTO_ENGINE_SELECT_MODE(gstate);
#if USE_SCREEN
              ui_state.scope_ready = false;
#endif
            }
            break;
          default:
            SWITCHTO_ENGINE_SELECT_MODE(gstate);
            gstate->midi_mod = true;
            gstate->cv_mod1 = false;
            gstate->cv_mod2 = false;
            gstate->filter_enabled = false;
            SCHEDULE_REFRESH(gstate);
            break;
        }
        encoder->last_encoder_activity = millis();
        SCHEDULE_REFRESH(gstate);
        break;

      case OSCILLOSCOPE_MODE:
        SWITCHTO_ENGINE_SELECT_MODE(gstate);
        SCHEDULE_REFRESH(gstate);
        encoder->last_encoder_activity = millis();
        break;
    }
  }

  if (encoder_sw_pressed(encoder)) {
    // Encoder sw press reaction depending on the mode
    switch (gstate->display_state) {
      case ENGINE_SELECT_MODE:
        SWITCHTO_ENGINE_SETTINGS_CONFIG(gstate);
        gstate->encoder.state = ENGINE_SELECT;
        SCHEDULE_REFRESH(gstate);
        break;

      case ENGINE_SETTINGS_CONFIG:
        gstate->encoder.state = (EncoderState)((gstate->encoder.state + 1) % (ENCODER_STATE_NUM - 1));
        SCHEDULE_REFRESH(gstate);
        break;

      case OSCILLOSCOPE_MODE:
        SWITCHTO_ENGINE_SETTINGS_CONFIG(gstate);
        gstate->encoder.state = (EncoderState)((gstate->encoder.state + 1) % (ENCODER_STATE_NUM - 1));
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


static inline bool setup_LittleFS() {
  bool fs_ready = false;
  LittleFS.format();
  if (!LittleFS.begin()) {
    DEBUG_PRINTLN("LittleFS Mount Failed. Attempting to format...");
    LittleFS.format();
    if (LittleFS.begin()) {
      DEBUG_PRINTLN("LittleFS Formatted and Mounted successfully.");
      return true;
    } else {
      DEBUG_PRINTLN("LittleFS Critical Error: Hardware issue or Flash size not set!");
      return false;
    }
  }
  DEBUG_PRINTLN("LittleFS Mounted.");
  return true;
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
    yield();  // Wait for Core 1 to finish DSP initialisation
    return;
  }

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
  setup_voices();
  setup_global_filter();
}

void loop1() {
  if (i2s_output.availableForWrite() >= AUDIO_BLOCK * 4) {
    updateAudio();
  }
  SET_SYSTEM_READY(&runtime_state);
}
