import os
import sys
import time

from nallely import LFO, VirtualDevice, VirtualParameter, on, stop_all_connected_devices

backend = os.getenv("LISA_IMPL", "HW")
if backend == "HW":
    from nallely.experimental.lisa_pico import Lisa

    print("!! Loaded HW implementation")
elif backend == "SW":
    from simulator import LisaSim as Lisa

    print("!! Loaded SW implementation")
else:
    raise ImportError("Unknown backend", backend)


class Logger(VirtualDevice):
    input_cv = VirtualParameter(name="input", range=[0, 127], default=0)

    @on(input_cv, "any")
    def push_value(self, value, ctx):
        print("[LOGGER] receiving", value)


class Constant(VirtualDevice):
    value_cv = VirtualParameter(name="value", range=[-8192, 8192], default=0)

    def main(self, ctx):
        return self.value, [self.value_cv]


def setup(lisa, lfo1, lfo2):
    print("* Reset wavetables and wait 0.5s...")
    lisa.wavetable.reset_all_wt = "ON"
    lisa.wavetable.reset_all_wt = "OFF"
    lisa.wavetable.mode_wt1 = "circular"
    lisa.wavetable.mode_wt2 = "circular"
    lisa.wavetable.mode_wt3 = "circular"
    lisa.wavetable.mode_wt4 = "circular"
    print(f"* Stream lfos 2 x {lfo1.waveform} and 2 x {lfo2.waveform}")
    lisa.wavetable.stream_table1 = lfo1.scale(-8192, 8192)
    lisa.wavetable.stream_table2 = lfo1.scale(-8192, 8192)
    lisa.wavetable.stream_table3 = lfo2.scale(-8192, 8192)
    lisa.wavetable.stream_table4 = lfo2.scale(-8192, 8192)


def teardown(lisa):
    print("Stopping now...")
    lisa.force_all_notes_off()
    lisa.wavetable.reset_all_wt = "ON"
    lisa.wavetable.reset_all_wt = "OFF"
    stop_all_connected_devices()


def play_sequence(lisa, notes, timing=0.5, timing_on=0.5, timing_off=0.5):
    for note in notes:
        print("  note on", note)
        lisa.note_on(note)
        time.sleep(timing_on or timing)
        print("  note off", note)
        lisa.note_off(note)
        time.sleep(timing_off or timing)


def play_cluster(lisa, notes, duration=4, off_at_once=False):
    for note in notes:
        print("  note on", note)
        lisa.note_on(note, velocity=127)
    print(f"  Wait for {duration}s")
    time.sleep(duration)
    for note in notes[::-1]:
        print("  note off", note)
        lisa.note_off(note)
        if not off_at_once:
            time.sleep(1)


def test1(lisa, lfo1, lfo2):
    print("* Test 6 notes voicing 4s")
    lisa.note_on(50, velocity=70)
    lisa.note_on(52, velocity=80)
    lisa.note_on(54, velocity=90)
    lisa.note_on(56, velocity=95)
    lisa.note_on(58, velocity=99)
    lisa.note_on(60, velocity=127)
    time.sleep(4)


def test2(lisa, lfo1, lfo2):
    print("* Plug LFO to the cutoff")
    lisa.filter.cutoff = lfo1.scale(45, 70)
    time.sleep(2)
    lisa.force_all_notes_off()

    print("* Plug LFO to the notes")
    lisa.keys.notes = lfo1.scale(100, 20)
    time.sleep(2)

    print("* Remove the LFO on the cutoff")
    lisa.filter.cutoff -= lfo1
    lisa.filter.cutoff = 50
    time.sleep(2)


def test3(lisa, lfo1, lfo2):
    print("* Tests fm slew")
    lisa.keys.notes -= lfo1
    fm_lfo = LFO(waveform="step", speed=0.1, autoconnect=True)
    print("Slew rate 1")
    lisa.modulation.FM_slew = 1
    lisa.modulation.FM_mod = fm_lfo
    lisa.note_on(54, velocity=126)
    input("Press enter for next value...")
    print("Slew rate 5")
    lisa.modulation.FM_slew = 5
    input("Press enter for next value...")
    print("Slew rate 10")
    lisa.modulation.FM_slew = 10
    input("Press enter for next value...")
    print("Slew rate 40")
    lisa.modulation.FM_slew = 40
    input("Press enter for next value...")
    print("Slew rate 127")
    lisa.modulation.FM_slew = 127
    input("Press enter for next value...")
    print("Slew rate 1")
    lisa.modulation.FM_slew = 1
    input("Press enter...")
    lisa.modulation.FM_mod -= lfo1
    print("* Set back slew to almost instantaneous...")
    lisa.modulation.FM_slew = 127
    fm_lfo.stop()


# unison test
def test4(lisa, lfo1, lfo2):
    lisa.force_all_notes_off()
    # Poly first to hear the difference
    lisa.general.voice_mode = "poly"
    print("[poly] Play note 54")
    lisa.note_on(54)
    time.sleep(2)
    lisa.note_off(54)
    time.sleep(2)
    print("[poly] Play notes sequences...")
    play_sequence(lisa, [54, 47, 42, 58])
    print("[poly] Play notes cluster and one note by one note off...")
    play_cluster(lisa, [54, 47, 42])
    # unison now to hear the difference
    lisa.general.voice_mode = "unison"
    print("[unison] Play note 54")
    lisa.note_on(54)
    time.sleep(2)
    lisa.note_off(54)
    time.sleep(2)
    print("[unison] Play notes sequences...")
    play_sequence(lisa, [54, 47, 42, 58])
    print("[unison] Play notes cluster and one note by one note off...")
    play_cluster(lisa, [54, 47, 42])


# mono test
def test5(lisa, lfo1, lfo2):
    lisa.force_all_notes_off()
    # Poly first to hear the difference
    lisa.general.voice_mode = "poly"
    lisa.envelope.release = 90
    print(lisa.general.voice_mode, int(lisa.general.voice_mode))
    print("[poly] Play notes sequences...")
    play_sequence(lisa, [54, 47, 42, 58])
    print("[poly] Play notes cluster and one note by one note off...")
    play_cluster(lisa, [54, 47])
    # unison now to hear the difference
    lisa.general.voice_mode = "unison"
    print(lisa.general.voice_mode, int(lisa.general.voice_mode))
    print("[unison] Play notes sequences...")
    play_sequence(lisa, [54, 47, 42, 58])
    print("[unison] Play notes cluster and one note by one note off...")
    play_cluster(lisa, [54, 47])
    # mono now to hear the difference
    lisa.general.voice_mode = "mono"
    print(lisa.general.voice_mode, int(lisa.general.voice_mode))
    print("[mono] Play notes sequences...")
    play_sequence(lisa, [54, 47, 42, 58])
    print("[mono] Play notes cluster and one note by one note off...")
    play_cluster(lisa, [54, 47], duration=1)


# filters test
def test6(lisa, lfo1, lfo2):
    lisa.force_all_notes_off()
    lisa.envelope.release = 70
    lisa.filter.cutoff = 64
    lisa.general.voice_mode = "poly"
    # low pass filter
    lisa.filter.type = "lowpass"
    print(lisa.filter.type, int(lisa.filter.type))
    play_cluster(lisa, [42, 45, 47])
    # high pass filter
    lisa.filter.type = "highpass"
    print(lisa.filter.type, int(lisa.filter.type))
    play_cluster(lisa, [42, 45, 47])
    # band pass filter
    lisa.filter.type = "bandpass"
    print(lisa.filter.type, int(lisa.filter.type))
    play_cluster(lisa, [42, 45, 47])


# levels test
def test7(lisa, lfo1, lfo2):
    lisa.force_all_notes_off()
    # force filter to a valid value
    lisa.general.voice_mode = "unison"
    lisa.envelope.release = 70
    lisa.filter.cutoff = 64
    # low pass filter
    lisa.filter.type = "lowpass"
    lisa.wavetable.level_table1 = 127
    lisa.wavetable.level_table2 = 127
    lisa.wavetable.level_table3 = 127
    lisa.wavetable.level_table4 = 127
    print(
        "All levels full",
        [
            lisa.wavetable.level_table1,
            lisa.wavetable.level_table2,
            lisa.wavetable.level_table3,
            lisa.wavetable.level_table4,
        ],
    )
    play_cluster(lisa, [42, 45, 47])

    # table 1 50%
    print("Wavetable 1 50%")
    lisa.wavetable.level_table1 = 64
    play_cluster(lisa, [42, 45, 47])

    # table 2 50%
    print("Wavetable 2 50%")
    lisa.wavetable.level_table2 = 64
    play_cluster(lisa, [42, 45, 47])

    # table 3 50%
    print("Wavetable 3 50%")
    lisa.wavetable.level_table3 = 64
    play_cluster(lisa, [42, 45, 47])

    # table 4 50%
    print("Wavetable 4 50%")
    lisa.wavetable.level_table4 = 64
    play_cluster(lisa, [42, 45, 47])

    # lfo on wt1 and wt3
    print("Plug an LFO on wt1 and wt3")
    lfo = LFO(speed=1, sampling_rate=259, auto_srate="OFF")
    lisa.wavetable.level_table1 = lfo.scale(127, 30)
    lisa.wavetable.level_table2 = lfo.scale(127, 30)
    lisa.wavetable.level_table3 = lfo.scale(0, 127)
    lisa.wavetable.level_table4 = lfo.scale(45, 80)
    lfo.start()
    play_cluster(lisa, [42, 45, 47])
    lfo.stop()


# detune test
def test8(lisa, lfo1, lfo2):
    lisa.force_all_notes_off()
    # force filter to a valid value and unison mode
    lisa.general.voice_mode = "unison"
    lisa.envelope.release = 70
    lisa.filter.cutoff = 64
    # low pass filter
    lisa.filter.type = "lowpass"
    print("Play cluster with detune at 0 (no detune)")
    lisa.general.detune = 64
    play_cluster(lisa, [42, 45, 47])

    print("Play cluster with light +detune")
    lisa.general.detune = 75
    play_cluster(lisa, [42, 45, 47])

    print("Play cluster with harsh +detune")
    lisa.general.detune = 127
    play_cluster(lisa, [42, 45, 47])

    print("Play cluster with light -detune")
    lisa.general.detune = 58
    play_cluster(lisa, [42, 45, 47])

    print("Play cluster with harsh -detune")
    lisa.general.detune = 0
    play_cluster(lisa, [42, 45, 47])

    # lfo on detune
    print("Plug an LFO on detune")
    lfo = LFO(speed=5)
    lisa.general.detune = lfo.scale(0, 127)
    lfo.start()
    play_cluster(lisa, [42, 45, 47])
    lfo.stop()


# features test
def test9(lisa, lfo1, lfo2):
    lisa.force_all_notes_off()
    # force filter to a valid value and unison mode
    lisa.general.voice_mode = "unison"
    lisa.envelope.release = 70
    lisa.filter.cutoff = 64

    log = Logger(autoconnect=True)
    log.input_cv = lisa.features.peak_envelope.scale()
    print("Play cluster with harsh +detune")
    lisa.general.detune = 127
    play_cluster(lisa, [42, 49, 54])
    log.input_cv.disconnect_all_links()


# scroll mode
def test10(lisa, lfo1, lfo2):
    lisa.force_all_notes_off()
    lisa.general.voice_mode = "unison"
    lisa.envelope.release = 70
    lisa.filter.cutoff = 55

    print("Activate scroll mode on all wavetables")
    lisa.wavetable.mode_wt1 = "scroll"
    lisa.wavetable.mode_wt2 = "scroll"
    lisa.wavetable.mode_wt3 = "scroll"
    lisa.wavetable.mode_wt4 = "scroll"

    print("Play cluster with scroll activated")
    play_cluster(lisa, [42, 45, 47])

    print("Play cluster with scroll disabled")
    lisa.wavetable.mode_wt1 = "circular"
    lisa.wavetable.mode_wt2 = "circular"
    lisa.wavetable.mode_wt3 = "circular"
    lisa.wavetable.mode_wt4 = "circular"
    play_cluster(lisa, [42, 45, 47])

    print("Activate scroll mode on all wavetables and play sequence")
    lisa.wavetable.mode_wt1 = "scroll"
    lisa.wavetable.mode_wt2 = "scroll"
    lisa.wavetable.mode_wt3 = "scroll"
    lisa.wavetable.mode_wt4 = "scroll"
    play_sequence(lisa, [54, 47, 42, 58])

    print("Disable scroll and play sequence")
    lisa.wavetable.mode_wt1 = "circular"
    lisa.wavetable.mode_wt2 = "circular"
    lisa.wavetable.mode_wt3 = "circular"
    lisa.wavetable.mode_wt4 = "circular"
    play_sequence(lisa, [54, 47, 42, 58])


# manual mode
def test11(lisa, lfo1, lfo2):
    lisa.force_all_notes_off()
    lisa.general.voice_mode = "unison"
    lisa.envelope.release = 70
    lisa.filter.cutoff = 55
    print("* Reset wavetables...")
    lisa.wavetable.reset_all_wt = "ON"
    lisa.wavetable.reset_all_wt = "OFF"

    lfos = [LFO(speed=i, waveform="sawtooth") for i in range(1, 5)]
    for lfo in lfos:
        lfo.start()
    lisa.wavetable.index_wt1 = lfos[0].scale(0, 64)

    lisa.wavetable.index_wt2 = lfos[1].scale(0, 64)
    lisa.wavetable.index_wt1 = lfos[1].scale(64, 127)

    lisa.wavetable.index_wt3 = lfos[2]
    lisa.wavetable.index_wt2 = lfos[2]

    lisa.wavetable.index_wt4 = lfos[3]
    lisa.wavetable.index_wt1 = lfos[3]

    lisa.modulation.timbre = lisa.features.peak_envelope.scale(127, 0)
    lisa.modulation.color = lisa.features.peak_envelope.scale(127, 0)

    print("Activate manual mode on all wavetables")
    lisa.wavetable.mode_wt1 = "manual"
    lisa.wavetable.mode_wt2 = "manual"
    lisa.wavetable.mode_wt3 = "circular"
    lisa.wavetable.mode_wt4 = "scroll"

    print("Play cluster with manual activated")
    play_cluster(lisa, [42, 45, 47])

    print("Play  sequence")
    play_sequence(lisa, [54, 47, 42, 58])

    print("Restore circular mode on all wavetables")
    lisa.wavetable.mode_wt1 = "circular"
    lisa.wavetable.mode_wt2 = "circular"
    lisa.wavetable.mode_wt3 = "circular"
    lisa.wavetable.mode_wt4 = "circular"
    for lfo in lfos:
        lfo.stop()

    lisa.modulation.timbre.disconnect_incoming_links()
    lisa.modulation.color.disconnect_incoming_links()
    lisa.modulation.timbre = 64
    lisa.modulation.color = 64


def test12(lisa, lfo1, lfo2):
    lisa.force_all_notes_off()
    lisa.general.voice_mode = "poly"
    lisa.filter.cutoff = 55

    print("* Pause LFOs")
    lfo1.pause()
    lfo2.pause()

    print("* Reset wavetables...")
    lisa.wavetable.reset_all_wt = "ON"
    lisa.wavetable.reset_all_wt = "OFF"

    print("Activate manual interpolation mode on all wavetables")
    lisa.wavetable.mode_wt1 = "manual_interpolated"
    lisa.wavetable.mode_wt2 = "manual_interpolated"
    lisa.wavetable.mode_wt3 = "manual_interpolated"
    lisa.wavetable.mode_wt4 = "manual_interpolated"

    lfo = LFO(
        speed=1,
        waveform="sine",
        autoconnect=True,
        # sampling_rate=254,
        auto_srate="ON",
    )
    lisa.wavetable.index_wt1 = lfo.scale()

    wave = LFO(
        speed=3,
        waveform="sine",
        autoconnect=True,
        # sampling_rate=259,
        auto_srate="ON",
    )
    lisa.wavetable.stream_table1 = wave.scale()

    print("* Wait 4s to fill the wavetable")
    time.sleep(4)

    print("Play cluster with manual activated")
    play_cluster(lisa, [42, 45, 47], duration=15)

    print("Play  sequence")
    play_sequence(lisa, [54, 47, 42, 58])

    lfo.stop()
    wave.stop()


def test14(lisa, lfo1, lfo2):
    lisa.force_all_notes_off()

    lisa.general.voice_mode = "poly"
    lisa.filter.cutoff = 55

    print("* Reset wavetables...")
    lisa.wavetable.mode_wt1 = "circular"
    lisa.wavetable.mode_wt2 = "circular"
    lisa.wavetable.mode_wt3 = "circular"
    lisa.wavetable.mode_wt4 = "circular"
    lisa.wavetable.reset_all_wt = "ON"
    lisa.wavetable.reset_all_wt = "OFF"
    lfo1.start()
    lisa.wavetable.stream_table1 = lfo1.scale()

    print("* Create a slow LFO for panning")
    lfo = LFO(speed=0.5)
    lfo.start()
    lisa.general.panning = lfo

    print("* Play cluster with manual activated")
    play_cluster(lisa, [42, 45, 47], duration=6)

    print("* Disconnect LFO from panning")
    lisa.general.panning -= lfo
    lfo.stop()


def test15(lisa, lfo1, lfo2):
    lisa.force_all_notes_off()
    lisa.general.voice_mode = "poly"
    lisa.general.detune = 75
    # lisa.filter.cutoff = 55
    # lisa.filter.type = "highpass"

    print("* Reset wavetables...")
    lisa.wavetable.mode_wt1 = "circular"
    lisa.wavetable.mode_wt2 = "circular"
    lisa.wavetable.mode_wt3 = "circular"
    lisa.wavetable.mode_wt4 = "circular"
    lisa.wavetable.reset_all_wt = "ON"
    lisa.wavetable.reset_all_wt = "OFF"
    lisa.wavetable.stream_table1 = lfo1.scale()
    lisa.envelope.attack = 2
    lisa.envelope.release = 40
    # lisa.wavetable.freeze_all = "ON"

    lfo = LFO(speed=0.1)
    lfo.start()
    lisa.filter.cutoff = lfo.scale(30, 60)
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=5)
    play_cluster(lisa, [60, 45, 47, 53, 40, 58, 20, 61], duration=1, off_at_once=True)
    play_cluster(lisa, [60, 40, 47, 53, 41, 57, 21, 62], duration=1, off_at_once=True)
    play_cluster(lisa, [60, 35, 47, 53, 42, 56, 22, 63], duration=1, off_at_once=True)
    play_cluster(lisa, [60, 38, 47, 50, 43, 55, 23, 64], duration=1, off_at_once=True)
    # fmt: off
    play_sequence(
        lisa,
        [
            60, 38, 47, 50, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 51, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 52, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 53, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 54, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 55, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 56, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 57, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 58, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 59, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 58, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 57, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 56, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 55, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 54, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
        ],
        timing=0.02,
    )
    # fmt: on
    lfo.stop()


# repeating notes
def test16(lisa, lfo1, lfo2):
    lisa.force_all_notes_off()
    lisa.general.voice_mode = "poly"

    print("* Reset wavetables...")
    lisa.wavetable.mode_wt1 = "circular"
    lisa.wavetable.mode_wt2 = "circular"
    lisa.wavetable.mode_wt3 = "circular"
    lisa.wavetable.mode_wt4 = "circular"
    lisa.wavetable.reset_all_wt = "ON"
    lisa.wavetable.reset_all_wt = "OFF"
    lisa.wavetable.stream_table1 = lfo1.scale()
    lisa.envelope.attack = 50
    lisa.envelope.release = 127

    play_sequence(
        lisa,
        [50],
        timing=1,
    )
    time.sleep(10)

    play_sequence(
        lisa,
        [50] * 10 + [54] * 10 + list(range(60, 70)),
        timing=0.1,
    )


# Changing waveform
def test17(lisa, lfo1, lfo2):
    lisa.force_all_notes_off()
    lisa.general.voice_mode = "poly"
    lisa.wavetable.level_table1 = 64
    lisa.wavetable.level_table2 = 127
    lisa.wavetable.level_table3 = 25

    print("* Reset wavetables...")
    # lisa.wavetable.stream_table2 -= lfo1
    # lisa.wavetable.stream_table3 -= lfo2
    # lisa.wavetable.stream_table4 -= lfo2

    lisa.wavetable.mode_wt1 = "circular"
    lisa.wavetable.mode_wt2 = "scroll"
    lisa.wavetable.mode_wt3 = "circular"
    lisa.wavetable.mode_wt4 = "scroll"
    lisa.wavetable.reset_all_wt = "ON"
    lisa.wavetable.reset_all_wt = "OFF"
    lfo = LFO(
        speed=0.1,
        waveform="sawtooth",
        sampling_rate=256,
        auto_srate="OFF",
        autoconnect=True,
    )
    # lisa.wavetable.blend_level = lfo

    lisa.envelope.attack = 5
    lisa.envelope.release = 10
    lisa.filter.cutoff = 55

    lfo1.waveform_cv = lfo.scale()
    lfo1.set_parameter("speed", 2)
    # lfo1.set_parameter("waveform", "sawtooth")
    #
    print("Activate snapshot mode and change sampling rate")
    lisa.wavetable.sluggish_mode = "ON"
    lfo1.set_parameter("sampling_rate", 300)
    lfo2.set_parameter("sampling_rate", 240)

    lisa.wavetable.snapshots_depth = 10
    print("Waiting 10s...")
    time.sleep(10)

    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=5)
    play_cluster(lisa, [60, 45, 47, 53, 40, 58, 20, 61], duration=1, off_at_once=True)
    play_cluster(lisa, [60, 40, 47, 53, 41, 57, 21, 62], duration=1, off_at_once=True)
    play_cluster(lisa, [60, 35, 47, 53, 42, 56, 22, 63], duration=1, off_at_once=True)
    play_cluster(lisa, [60, 38, 47, 50, 43, 55, 23, 64], duration=1, off_at_once=True)

    # fmt: off
    play_sequence(
        lisa,
        [
            60, 38, 47, 50, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 51, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 52, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 53, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 54, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 55, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 56, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 57, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 58, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 59, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 58, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 57, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 56, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 55, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 54, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
        ],
        timing=0.02,
    )
    # fmt: on

    print("Change snapshots depth to min (wait 2s)...")
    lisa.wavetable.slug_depth = 0
    lisa.wavetable.auto_blend_k = 10
    time.sleep(2)
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=10)

    print("Change snapshots depth to half...")
    lisa.wavetable.slug_depth = 64
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=10)

    print("Change snapshots depth to max...")
    lisa.wavetable.slug_depth = 127
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=10)

    # Other part

    lfo1.set_parameter("sampling_rate", 300)
    lfo2.set_parameter("sampling_rate", 252)

    print("Change auto-blend speed to full")
    lisa.wavetable.auto_blend_k = 127
    print("Waiting 2s...")
    time.sleep(2)
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=5)

    print("Change auto-blend speed to half")
    lisa.wavetable.auto_blend_k = 10
    print("Waiting 2s...")
    time.sleep(2)
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=5)

    print("Modulate auto-blend with an LFO...")
    llfo = LFO(
        waveform="sawtooth",
        speed=0.1,
        sampling_rate=256,
        auto_srate="OFF",
        autoconnect=True,
    )
    lisa.wavetable.auto_blend_k = llfo.scale()
    print("Waiting 2s...")
    time.sleep(2)
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=5)

    play_cluster(lisa, [60, 45, 47, 53, 40, 58, 20, 61], duration=1, off_at_once=True)
    play_cluster(lisa, [60, 40, 47, 53, 41, 57, 21, 62], duration=1, off_at_once=True)
    play_cluster(lisa, [60, 35, 47, 53, 42, 56, 22, 63], duration=1, off_at_once=True)
    play_cluster(lisa, [60, 38, 47, 50, 43, 55, 23, 64], duration=1, off_at_once=True)

    # fmt: off
    play_sequence(
        lisa,
        [
            60, 38, 47, 50, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 51, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 52, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 53, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 54, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 55, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 56, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 57, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 58, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 59, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 58, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 57, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 56, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 55, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
            60, 38, 47, 54, 33, 43, 36, 55, 60, 38, 47, 50, 33, 43, 36, 55,
        ],
        timing=0.02,
    )
    # fmt: on

    print("Switch auto-blend off and control it using an LFO...")
    lisa.wavetable.auto_blend = "OFF"
    llfo.set_parameter("speed", 0.1)
    lisa.wavetable.blend_level = llfo
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=5)

    print("Switching to backward blending")
    lisa.wavetable.auto_blend_direction = "backward"
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 59, 41], duration=5)

    print("Switching to forward blending")
    lisa.wavetable.auto_blend_direction = "forward"
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 59, 41], duration=5)

    lfo.stop()
    llfo.stop()

    print("Get back to non snapshot mode")
    lisa.wavetable.sluggish_mode = "OFF"


def set_ratio(lisa, ratios):
    print("Set ratios", ratios)
    for i, ratio in enumerate(ratios):
        setattr(lisa.wavetable, f"phase_ratio_table{i}", ratio)


def test18(lisa, lfo1, lfo2):
    print("Get back to non snapshot mode")
    lisa.wavetable.sluggish_mode = "OFF"
    lisa.general.voice_mode = "poly"
    lfo1.set_parameter("waveform", "sawtooth")

    set_ratio(lisa, [0, 2048, 4096, 6144])
    play_sequence(lisa, [54, 47, 42, 58], timing=0.5)
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=5)

    set_ratio(lisa, [0, 1024, 2048, 3072])
    play_sequence(lisa, [54, 47, 42, 58], timing=0.5)
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=5)

    set_ratio(lisa, [0, 4, -4, 10])
    play_sequence(lisa, [54, 47, 42, 58], timing=0.5)
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=5)

    set_ratio(lisa, [0, 512, 1024, 2048])
    play_sequence(lisa, [54, 47, 42, 58], timing=0.5)
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=5)

    set_ratio(lisa, [0, 1798, 4510, 8124])
    play_sequence(lisa, [54, 47, 42, 58], timing=0.5)
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=5)

    set_ratio(lisa, [0, 424, 750, 1264])
    play_sequence(lisa, [54, 47, 42, 58], timing=0.5)
    play_cluster(lisa, [60, 45, 47, 53, 55, 30, 80, 90], duration=5)


def test19(lisa, lfo1, lfo2):
    print("Get back to snapshot mode")
    lisa.wavetable.sluggish_mode = "ON"
    lisa.general.voice_mode = "poly"
    lisa.wavetable.reset_all_wt = "ON"
    lisa.wavetable.reset_all_wt = "OFF"
    # lisa.wavetable.stream_table2 -= lfo1
    # lisa.wavetable.stream_table3 -= lfo2
    print("Test moving offset")
    llfo = LFO(
        waveform="sine",
        speed=0.2,
        sampling_rate=255,
        auto_srate="OFF",
        autoconnect=True,
    )
    lfo1.set_parameter("waveform", "sawtooth")
    lfo1.set_parameter("speed", "1.5")
    lfo2.set_parameter("waveform", "half_wave_rectified_sine")
    lfo2.set_parameter("speed", "2.7")

    lisa.wavetable.phase_ratio_table1 = 0
    lisa.wavetable.phase_ratio_table3 = 4567
    lisa.wavetable.phase_ratio_table2 = 1024
    lisa.wavetable.phase_offset_table3 = llfo.scale(0, 2048)
    lisa.wavetable.hard_sync = "ON"
    lisa.wavetable.phase_ratio_table4 = llfo.scale(1500, 7000)
    # lisa.wavetable.mode_wt3 = "scroll"
    lisa.envelope.release = 80
    play_sequence(
        lisa,
        [
            54,
            47,
            42,
            58,
            54,
            47,
            42,
            58,
            54,
            47,
            42,
            58,
            54,
            47,
            42,
            58,
            54,
            47,
            42,
            58,
        ],
        timing_on=0.5,
        timing_off=0.001,
    )
    play_cluster(lisa, [60, 45, 47, 53, 55], duration=50)

    lfo1.set_parameter("waveform", "sine")
    llfo.stop()


def test20(lisa, lfo1, lfo2): ...


tests = [
    test1,
    test2,
    test3,
    test4,
    test5,
    test6,
    test7,
    test8,
    test9,
    test10,
    test11,
    test12,
    lambda *args, **kwargs: print("skipped, bad omen"),
    test14,
    test15,
    test16,
    test17,
    test18,
    test19,
]

if __name__ == "__main__":
    if len(sys.argv) >= 2:
        testtorun = int(sys.argv[1]) - 1
    else:
        testtorun = None

    print("Start LISA quick test")
    lisa = Lisa()
    lisa.general.engine_select = 127
    lisa.general.gain = 32
    lisa.wavetable.level_table1 = 127
    lisa.wavetable.level_table2 = 127
    lisa.wavetable.level_table3 = 127
    lisa.wavetable.level_table4 = 127
    lisa.general.panning = 64
    lisa.filter.cutoff = 55

    lfo1 = LFO(
        waveform="sine",
        speed=2,
        sampling_rate=259,
        auto_srate="OFF",
        autoconnect=True,
    )
    lfo2 = LFO(
        waveform="square",
        speed=1,
        sampling_rate=259,
        auto_srate="OFF",
        autoconnect=True,
    )
    setup(lisa, lfo1, lfo2)
    try:
        if testtorun is not None:
            print(f"Run test{testtorun + 1} in 2s...")
            time.sleep(2)
            tests[testtorun](lisa, lfo1, lfo2)
        else:
            input("Press enter to start tests or ctrl+c to cancel...")
            for i, test in enumerate(tests, start=1):
                print(f"== TEST {i} ==")
                test(lisa, lfo1, lfo2)
    except KeyboardInterrupt:
        print("* tests canceled")
    teardown(lisa)
