from nallely.experimental.lisa_pico import Lisa
from nallely import LFO, stop_all_connected_devices
import time


print("Start LISA quick test")
lisa = Lisa()
lisa.general.engine_select = 127

lfo1 = LFO(waveform="sine", speed=1, sampling_rate=259, auto_srate="OFF", autoconnect=True)
lfo2 = LFO(waveform="sawtooth", speed=1, sampling_rate=259, auto_srate="OFF", autoconnect=True)

print("* Reset wavetables and wait 0.5s...")
lisa.wavetable.reset_all_wt = "ON"
lisa.wavetable.reset_all_wt = "OFF"
time.sleep(0.5)

print(f"* Stream lfos 2 x {lfo1.waveform} and 2 x {lfo2.waveform}")
lisa.wavetable.stream_table1 = lfo1.scale(-8192, 8192)
lisa.wavetable.stream_table2 = lfo1.scale(-8192, 8192)
lisa.wavetable.stream_table3 = lfo2.scale(-8192, 8192)
lisa.wavetable.stream_table4 = lfo2.scale(-8192, 8192)
time.sleep(1)

print("* Test 6 notes voicing 4s")
lisa.note_on(50, velocity=70)
lisa.note_on(52, velocity=80)
lisa.note_on(54, velocity=90)
lisa.note_on(56, velocity=95)
lisa.note_on(58, velocity=99)
lisa.note_on(60, velocity=127)
time.sleep(2)

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

print("* Tests fm slew")
lisa.modulation.FM_slew = lfo1.scale(0, 127)
lisa.modulation.FM_mod = 10
time.sleep(1)
lisa.modulation.FM_mod = 70
time.sleep(1)
lisa.modulation.FM_mod = 127
time.sleep(1)
lisa.modulation.FM_mod = 10
time.sleep(2)
lisa.modulation.FM_slew -= lfo1


print("Stopping now...")
lisa.wavetable.reset_all_wt = "ON"
stop_all_connected_devices()
