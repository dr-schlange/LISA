SOURCES = $(shell find . -name "*.ino" -o -name "*.h")
BUILD_DIR = ./build

.PHONY: all clean upload test

all: compile

compile: $(BUILD_DIR)/rp2040.rp2040.rpipicow/LISA.ino.uf2

$(BUILD_DIR)/rp2040.rp2040.rpipicow/LISA.ino.uf2: $(SOURCES)
	arduino-cli compile --export-binaries

upload: $(BUILD_DIR)/rp2040.rp2040.rpipicow/LISA.ino.uf2
	-python -c "from nallely.experimental.lisa_pico import Lisa; Lisa().control_change(127, 127)"
	arduino-cli upload -p /run/media/$$USER/RPI-RP2

fulltest: upload test
test:
	python quick-test.py
test%:
	python quick-test.py $*

clean:
	rm -rf $(BUILD_DIR)
