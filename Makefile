# Thin wrappers around PlatformIO for the common dev loop.
#
# PIO defaults to whatever `pio` resolves to on PATH, but falls back to
# PlatformIO's own venv install if that binary is broken (as the
# Debian/Ubuntu apt package currently is, incompatible with the system's
# `click` version). Override explicitly with `make PIO=/some/path upload`
# if neither guess is right for your machine.
PIO := $(shell command -v pio >/dev/null 2>&1 && pio --version >/dev/null 2>&1 && echo pio || echo $(HOME)/.platformio/penv/bin/pio)

.PHONY: build upload monitor flash clean debug upload-debug flash-debug

build:
	$(PIO) run

upload:
	$(PIO) run -t upload

monitor:
	$(PIO) device monitor

flash: upload monitor

clean:
	$(PIO) run -t clean

# Debug build: same firmware, with -D DEBUG_TRACES enabling the Serial
# lifecycle traces (WiFi up, client connect, settings saved, door
# open/close) - see the esp32dev-debug env in platformio.ini.
debug:
	$(PIO) run -e esp32dev-debug

upload-debug:
	$(PIO) run -e esp32dev-debug -t upload

flash-debug: upload-debug monitor
