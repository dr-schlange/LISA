import sys
import time

from nallely import LFO, stop_all_connected_devices
from nallely.experimental.lisa_pico import Lisa


def setup(lisa, lfo1, lfo2):
    print("* Reset wavetables and wait 0.5s...")
    lisa.wavetable.reset_all_wt = "ON"
    lisa.wavetable.reset_all_wt = "OFF"
    print(f"* Stream lfos 2 x {lfo1.waveform} and 2 x {lfo2.waveform}")
    lisa.wavetable.stream_table1 = lfo1.scale(-8192, 8192)
    lisa.wavetable.stream_table2 = lfo1.scale(-8192, 8192)
    lisa.wavetable.stream_table3 = lfo2.scale(-8192, 8192)
    lisa.wavetable.stream_table4 = lfo2.scale(-8192, 8192)
    input("Press enter to start tests or ctrl+c to cancel...")


def teardown(lisa):
    print("Stopping now...")
    lisa.force_all_notes_off()
    lisa.wavetable.reset_all_wt = "ON"
    stop_all_connected_devices()


def play_sequence(lisa, notes):
    for note in notes:
        print("note on", note)
        lisa.note_on(note)
        time.sleep(0.5)
        print("note off", note)
        lisa.note_off(note)


def play_cluster(lisa, notes, duration=4):
    for note in notes:
        print("note on", note)
        lisa.note_on(note)
    print(f"Wait for {duration}s")
    time.sleep(duration)
    for note in notes[::-1]:
        print("note off", note)
        lisa.note_off(note)
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


# unison test
def test4(lisa, lfo1, lfo2):
    lisa.force_all_notes_off()
    print("Note on 54")
    lisa.note_on(54)
    time.sleep(4)
    print("Note off")
    lisa.note_off(54)
    input("Note off...")
    input("Press enter to play notes sequences...")
    play_sequence(lisa, [54, 47, 42, 58])
    input("Press enter to play notes cluster...")
    play_cluster(lisa, [54, 47, 42])

tests = [test1, test2, test3, test4]

if __name__ == "__main__":
    if len(sys.argv) >= 2:
        testtorun = int(sys.argv[1]) - 1
    else:
        testtorun = None

    print("Start LISA quick test")
    lisa = Lisa()
    lisa.general.engine_select = 127

    lfo1 = LFO(
        waveform="sine", speed=1, sampling_rate=258, auto_srate="OFF", autoconnect=True
    )
    lfo2 = LFO(
        waveform="sawtooth",
        speed=1,
        sampling_rate=258,
        auto_srate="OFF",
        autoconnect=True,
    )
    setup(lisa, lfo1, lfo2)
    try:
        if testtorun is not None:
            tests[testtorun](lisa, lfo1, lfo2)
        else:
            for test in tests:
                test(lisa, lfo1, lfo2)
    except KeyboardInterrupt:
        print("* tests canceled")
    teardown(lisa)
