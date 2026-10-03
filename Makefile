SOURCES = $(shell find . -name "*.ino" -o -name "*.h")
BUILD_DIR = ./build

.PHONY: all clean upload test

all: compile

compile: $(BUILD_DIR)/rp2040.rp2040.rpipico/LISA.ino.uf2

$(BUILD_DIR)/rp2040.rp2040.rpipico/LISA.ino.uf2: $(SOURCES)
	arduino-cli compile --export-binaries

upload: $(BUILD_DIR)/rp2040.rp2040.rpipico/LISA.ino.uf2
	-python -c "from nallely.lisa.lisa_pico import Lisa; lisa = Lisa(); lisa.control_change(127, 127); lisa.close()"
	arduino-cli upload -p /run/media/$$USER/RPI-RP2

fulltest: upload test
test:
	python quick-test.py
test%:
	python quick-test.py $*

test-simulator:
	LISA_IMPL=SW python quick-test.py

test%-simulator:
	LISA_IMPL=SW python quick-test.py $*

format: $(SOURCES)
	clang-format -i --style=LLVM $(SOURCES)

clean:
	rm -rf $(BUILD_DIR)
