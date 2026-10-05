/*
   LISA (v0.3.0)

   Copyright (c) 2026 Dr Schlange
   Licensed under GNU GPLv3
 */
#pragma once
#include <pico/mutex.h>

// TinyUSB on the RP2040 runs tud_task() in a soft IRQ on core 0,
// triggered by every USB hardware IRQ and guarded by __usb_mutex with
// mutex_try_enter. The tud_* calls we make
// from loop() for reading/writing MIDI or CDC from USB needs to take that
// mutex, otherwhise the IRQ can preempt one of the reads/writes. Then
// tud_tasks() waits forever for that mutex inside the IRQ, and loop() in core 0
// can never resume. By holding __usb_mutex before the read/write calls and
// releasing it after, the IRQ skips its run and triggers on the next tick.
//
// Usage: acquire_usb_lock(); <one short USB call>; release_usb_lock();
// - Every acquire needs its release on every path, an early return in between
//   leaves the mutex taken (obviously).
// - The lock is not recursive => never call acquire again before the release.
// - Never hold it around a call that loops waiting for the USB task
extern mutex_t __usb_mutex;

static inline void acquire_usb_lock() { mutex_enter_blocking(&__usb_mutex); }
static inline void release_usb_lock() { mutex_exit(&__usb_mutex); }
