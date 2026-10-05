/*
   LISA (v0.3.0)

   Copyright (c) 2026 Dr Schlange
   Licensed under GNU GPLv3
 */
#pragma once
#include "constants_config.h"

// Freeze debugging
//
// Core 0 feeds a watchdog once per loop iteration
//
// If core 0 stops (blocked call, infinite loop or hard fault),
// the watchdog reboots the board and the state saved in the watchdog scratch
// registers is shown on the screen at the next boot

enum FreezeStage : uint8_t {
  FZ_NONE,
  FZ_ENCODER,
  FZ_SAVE,
  FZ_CONTROL,
  FZ_MENU,
  FZ_MIDI,
  FZ_HR_HANDSHAKE,
  FZ_HR_READ,
  FZ_HR_ROUTE,
  FZ_UI,
  FZ_YIELD,
  FZ_MIDI_READ,
  FZ_MIDI_PITCHWHEEL,
  FZ_MIDI_NOTE_ON,
  FZ_MIDI_NOTE_OFF,
  FZ_MIDI_CC,
  FZ_QUEUE_ADD,
  FZ_NUM_STAGES
};

#if DEBUG_FREEZE
#include <hardware/watchdog.h>

#define FREEZE_WATCHDOG_MS 3000

static const char *const freeze_stage_names[] = {
    "none",     "encoder", "save",     "control", "menu",     "midi",
    "hr hello", "hr read", "hr route", "ui",      "yield",    "midi read",
    "midi pb",  "note on", "note off", "midi cc", "queue add"};

// scratch[0]: stage | last MIDI status << 8 | data1 << 16 | data2 << 24
// scratch[1]: last HR message (cc | value << 8 | flags << 24)
// scratch[2]: core 1 audio block counter (written by core 1 only)
// scratch[3]: scratch[2] as seen by core 0 at its last loop iteration
// After a freeze, scratch[2] > scratch[3] means core 1 kept running.
static uint32_t freeze_word0 = 0; // core 0 only

#define FREEZE_STAGE(stage)                                                    \
  (freeze_word0 = (freeze_word0 & 0xFFFFFF00u) | (uint32_t)(stage),            \
   watchdog_hw->scratch[0] = freeze_word0)
#define FREEZE_LAST_MIDI(status, data1, data2)                                 \
  (freeze_word0 = (freeze_word0 & 0xFFu) | ((uint32_t)(status) << 8) |         \
                  ((uint32_t)(data1) << 16) | ((uint32_t)(data2) << 24),       \
   watchdog_hw->scratch[0] = freeze_word0)
#define FREEZE_LAST_HR(msg)                                                    \
  (watchdog_hw->scratch[1] = (uint32_t)(msg)->cc |                             \
                             ((uint32_t)(msg)->value << 8) |                   \
                             ((uint32_t)(msg)->flags << 24))
#define FREEZE_CORE1_TICK()                                                    \
  (watchdog_hw->scratch[2] = watchdog_hw->scratch[2] + 1)
#define FREEZE_FEED()                                                          \
  (watchdog_hw->scratch[3] = watchdog_hw->scratch[2], watchdog_update())
#define FREEZE_START() watchdog_enable(FREEZE_WATCHDOG_MS, 1)
#define FREEZE_WAS_WATCHDOG() watchdog_enable_caused_reboot()
#define FREEZE_READ_WORD0() (watchdog_hw->scratch[0])
#define FREEZE_READ_LAST_HR() (watchdog_hw->scratch[1])
#define FREEZE_READ_CORE1_DELTA()                                              \
  ((uint32_t)(watchdog_hw->scratch[2] - watchdog_hw->scratch[3]))
#else
#define FREEZE_STAGE(stage)
#define FREEZE_LAST_MIDI(status, data1, data2)
#define FREEZE_LAST_HR(msg)
#define FREEZE_CORE1_TICK()
#define FREEZE_FEED()
#define FREEZE_START()
#endif
