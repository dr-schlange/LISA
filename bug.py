import sys
import time

from nallely import LFO, stop_all_connected_devices
from nallely.lisa.lisa_pico_hr import LisaHR as Lisa


def setup():
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

    print("* Reset wavetables and wait 0.5s...")
    lisa.wavetable.reset_all_wt = "ON"
    lisa.wavetable.reset_all_wt = "OFF"
    lisa.wavetable.mode_wt1 = "circular"
    lisa.wavetable.mode_wt2 = "circular"
    lisa.wavetable.mode_wt3 = "circular"
    lisa.wavetable.mode_wt4 = "circular"
    print(f"* Stream lfos 2 x {lfo1.waveform} and 2 x {lfo2.waveform}")
    lisa.wavetable.stream_table1 = lfo1.scale()
    lisa.wavetable.stream_table2 = lfo1.scale()
    lisa.wavetable.stream_table3 = lfo2.scale()
    lisa.wavetable.stream_table4 = lfo2.scale()
    return lisa, lfo1, lfo2


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


def bug_freeze(lisa, lfo1, lfo2):
    lfo = LFO(
        autoconnect=True,
        speed=0.2,
        auto_srate="OFF",
        sampling_rate=2048,
        waveform="triangle",
    )
    lisa.filter.cutoff = lfo.scale()
    lisa.keys.notes = lfo.scale()

    input("Enter to finish this bug repro...")
    lfo.stop()


bugs = [
    bug_freeze,
]

if __name__ == "__main__":
    if len(sys.argv) >= 2:
        testtorun = int(sys.argv[1]) - 1
    else:
        testtorun = None

    print("Start LISA quick test")

    lisa, lfo1, lfo2 = setup()
    try:
        if testtorun is not None:
            print(f"Run test{testtorun + 1} in 2s...")
            time.sleep(2)
            bugs[testtorun](lisa, lfo1, lfo2)
        else:
            input("Press enter to start tests or ctrl+c to cancel...")
            for i, test in enumerate(bugs, start=1):
                print(f"== TEST {i} ==")
                test(lisa, lfo1, lfo2)
    except KeyboardInterrupt:
        print("* tests canceled")
    teardown(lisa)
