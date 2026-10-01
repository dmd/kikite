# Device parameters
DEVICE ?= tanmatsu
PORT ?= /dev/ttyACM0

# Build parameters
IDF_VERSION ?= v6.0.2
BUILD_ROOT ?= $(shell cat .BUILD_ROOT 2>/dev/null || echo build)
BUILD ?= $(BUILD_ROOT)/$(DEVICE)
FAT ?= 0
SDKCONFIG_DEFAULTS ?= sdkconfigs/general;sdkconfigs/$(DEVICE)
SDKCONFIG ?= sdkconfig_$(DEVICE)

# SDK
IDF_PATH ?= $(shell cat .IDF_PATH 2>/dev/null || { test -d `pwd`/esp-idf && echo `pwd`/esp-idf; } || echo '$(HOME)/.espressif/$(IDF_VERSION)/esp-idf')
IDF_TOOLS_PATH ?= $(shell cat .IDF_TOOLS_PATH 2>/dev/null || { test -d `pwd`/esp-idf-tools && echo `pwd`/esp-idf-tools; } || echo '$(HOME)/.espressif/tools')
IDF_SOURCE ?= $(shell { test -f .IDF_PATH || test -d `pwd`/esp-idf; } && echo '$(IDF_PATH)/export.sh' || echo '$(HOME)/.espressif/tools/activate_idf_$(IDF_VERSION).sh')
IDF_EXPORT_QUIET ?= 1
IDF_GITHUB_ASSETS ?= dl.espressif.com/github_assets
IDF_INSTALL_PATH ?= $(shell echo `pwd`/esp-idf)
IDF_INSTALL_TOOLS_PATH ?= $(shell echo `pwd`/esp-idf-tools)

MAKEFLAGS += --silent
SHELL := /usr/bin/env bash


####

# Set IDF_TARGET based on device name

ifeq ($(DEVICE), tanmatsu)
IDF_TARGET ?= esp32p4
else ifeq ($(DEVICE), mch2022)
IDF_TARGET ?= esp32
else ifeq ($(DEVICE), kami)
IDF_TARGET ?= esp32
else ifeq ($(DEVICE), hackerhotel-2024)
IDF_TARGET ?= esp32c6
else ifeq ($(DEVICE), hackaday2025)
IDF_TARGET ?= esp32s3
else ifeq ($(DEVICE), heltecv3)
IDF_TARGET ?= esp32s3
else ifeq ($(DEVICE), esp32-p4-function-ev-board)
IDF_TARGET ?= esp32p4
else ifeq ($(DEVICE), esp32-s31-korvo-1)
IDF_TARGET ?= esp32s31
else ifeq ($(DEVICE), why2025)
IDF_TARGET ?= esp32p4
else
$(warning "Unknown device, defaulting to ESP32")
IDF_TARGET ?= esp32
endif

IDF_PARAMS := -B $(BUILD) -DDEVICE=$(DEVICE) -DSDKCONFIG_DEFAULTS="$(SDKCONFIG_DEFAULTS)" -DSDKCONFIG=$(SDKCONFIG) -DIDF_TARGET=$(IDF_TARGET) -DFAT=$(FAT)

#####

export IDF_TOOLS_PATH
export IDF_GITHUB_ASSETS

# General targets

.PHONY: all
all: build

# Badgelink
.PHONY: badgelink
badgelink:
	rm -rf badgelink
	git clone https://github.com/badgeteam/esp32-component-badgelink.git badgelink
	python3 -m venv badgelink/tools/.venv
	badgelink/tools/.venv/bin/pip install -r badgelink/tools/requirements.txt

BADGELINK ?= badgelink/tools/.venv/bin/python badgelink/tools/badgelink.py
APP_SLUG ?= io.github.dmd.kikite
APP_VERSION ?= 0.1.0
APP_REVISION ?= $(shell echo $$(( ($$(date +%s) / 60) % 65536 )))
DIST ?= $(BUILD_ROOT)/dist/$(APP_SLUG)

.PHONY: install
install: build
	$(BADGELINK) appfs upload $(APP_SLUG) "kikite" $(APP_REVISION) $(BUILD)/application.bin

.PHONY: run
run:
	$(BADGELINK) start $(APP_SLUG)

.PHONY: package
package: build
	rm -rf $(DIST)
	mkdir -p $(DIST)
	cp $(BUILD)/application.bin $(DIST)/kikite.bin
	cp assets/icon16.png assets/icon32.png assets/icon64.png LICENSE $(DIST)/
	sed -e 's/@VERSION@/$(APP_VERSION)/' -e 's/@REVISION@/$(APP_REVISION)/' assets/metadata.json.in > $(DIST)/metadata.json
	echo "Packaged $(DIST). Copy that folder to apps/ on the SD card, or upload it to the app repository."

# Preparation

.PHONY: prepare
prepare: sdk

.PHONY: submodules
submodules: 
	if [ ! -f .submodules_update_done ]; then \
		echo "Updating submodules"; \
		git submodule update --init --recursive; \
		touch .submodules_update_done; \
	fi

.PHONY: sdk
sdk:
	if test -d "$(IDF_INSTALL_PATH)"; then echo -e "ESP-IDF target folder exists!\r\nPlease remove the folder or un-set the environment variable."; exit 1; fi
	if test -d "$(IDF_INSTALL_TOOLS_PATH)"; then echo -e "ESP-IDF tools target folder exists!\r\nPlease remove the folder or un-set the environment variable."; exit 1; fi
	git clone --recursive --branch "$(IDF_VERSION)" https://github.com/espressif/esp-idf.git "$(IDF_INSTALL_PATH)" --depth=1 --shallow-submodules
	cd "$(IDF_INSTALL_PATH)"; git submodule update --init --recursive
	cd "$(IDF_INSTALL_PATH)"; bash install.sh all

.PHONY: eim-sdk
eim-sdk:
	eim install --do-not-track true -i $(IDF_VERSION)

.PHONY: check-sdk
check-sdk:
	@if test -d $(IDF_PATH); then \
		printf '%s\n' "ESP-IDF $(IDF_VERSION) found!"; \
	else \
		printf 'ESP-IDF SDK not found. %s\n' "Please install ESP-IDF $(IDF_VERSION), either by running 'make prepare' (installs to this folder) or 'make eim-sdk' (installs using Espressif installation manager) or if manually installed set the IDF_PATH and IDF_TOOLS_PATH environment variables or create files .IDF_PATH and .IDF_TOOLS_PATH in this folder containing the paths." >&2; \
		exit 1; \
	fi

.PHONY: print-sdk
print-sdk:
	echo "ESP-IDF path:               $(IDF_PATH)"
	echo "ESP-IDF tools:              $(IDF_TOOLS_PATH)"
	echo "ESP-IDF source command:     $(IDF_SOURCE)"
	echo "ESP-IDF installation path:  $(IDF_INSTALL_PATH)"
	echo "ESP-IDF installation tools: $(IDF_INSTALL_TOOLS_PATH)"

.PHONY: reinstallsdk
reinstallsdk:
	cd "$(IDF_PATH)"; bash install.sh all

.PHONY: removesdk
removesdk:
	rm -rf "$(IDF_PATH)"
	rm -rf "$(IDF_TOOLS_PATH)"

.PHONY: refreshsdk
refreshsdk: removesdk sdk

.PHONY: menuconfig
menuconfig:
	source "$(IDF_SOURCE)" && idf.py menuconfig -DDEVICE=$(DEVICE) -DSDKCONFIG_DEFAULTS="$(SDKCONFIG_DEFAULTS)" -DSDKCONFIG=$(SDKCONFIG) -DIDF_TARGET=$(IDF_TARGET)
	
# Cleaning

.PHONY: clean
clean:
	rm -rf $(BUILD)
	rm -f .submodules_update_done
	rm -rf managed_components
	rm -f sdkconfig_*

.PHONY: fullclean
fullclean: clean
	rm -rf build
	rm -f sdkconfig_*
	rm -f sdkconfig
	rm -f sdkconfig.old
	rm -f sdkconfig.ci
	rm -f sdkconfig.defaults

# Check if build environment is set up correctly
.PHONY: checkbuildenv
checkbuildenv:
	if [ -z "$(IDF_PATH)" ]; then echo "IDF_PATH is not set!"; exit 1; fi
	if [ -z "$(IDF_TOOLS_PATH)" ]; then echo "IDF_TOOLS_PATH is not set!"; exit 1; fi

# Building

.PHONY: build
build: check-sdk icons checkbuildenv
	source "$(IDF_SOURCE)" >/dev/null && idf.py $(IDF_PARAMS) build

.PHONY: reconfigure
reconfigure: check-sdk checkbuildenv
	source "$(IDF_SOURCE)" >/dev/null && idf.py $(IDF_PARAMS) reconfigure

# Hardware

.PHONY: flash
flash: build
	source "$(IDF_SOURCE)" && \
	idf.py $(IDF_PARAMS) flash -p $(PORT)

.PHONY: flashmonitor
flashmonitor: build
	source "$(IDF_SOURCE)" && \
	idf.py $(IDF_PARAMS) flash -p $(PORT) monitor

.PHONY: prepappfs
prepappfs:
	source "$(IDF_SOURCE)" && \
	python3 managed_components/badgeteam__appfs/tools/appfs_generate.py \
	8192000 \
	appfs.bin

.PHONY: erase
erase:
	source "$(IDF_SOURCE)" && idf.py $(IDF_PARAMS) erase-flash -p $(PORT)

.PHONY: monitor
monitor:
	source "$(IDF_SOURCE)" && idf.py $(IDF_PARAMS) monitor -p $(PORT)

.PHONY: openocd
openocd:
	source "$(IDF_SOURCE)" && idf.py $(IDF_PARAMS) openocd

.PHONY: openocdftdi
openocdftdi:
	source "$(IDF_SOURCE)" && idf.py $(IDF_PARAMS) openocd --openocd-commands "-f board/esp32p4-ftdi.cfg"

.PHONY: gdb
gdb:
	source "$(IDF_SOURCE)" && idf.py $(IDF_PARAMS) gdb

.PHONY: gdbgui
gdbgui:
	source "$(IDF_SOURCE)" && idf.py $(IDF_PARAMS) gdbgui

.PHONY: gdbtui
gdbtui:
	source "$(IDF_SOURCE)" && idf.py $(IDF_PARAMS) gdbtui

# Tools

.PHONY: size
size:
	source "$(IDF_SOURCE)" && idf.py $(IDF_PARAMS) size

.PHONY: size-components
size-components:
	source "$(IDF_SOURCE)" && idf.py $(IDF_PARAMS) size-components

.PHONY: size-files
size-files:
	source "$(IDF_SOURCE)" && idf.py $(IDF_PARAMS) size-files

.PHONY: efuse
efuse:
	$(IDF_PATH)/components/efuse/efuse_table_gen.py --idf_target esp32p4 $(IDF_PATH)/components/efuse/esp32p4/esp_efuse_table.csv main/esp_efuse_custom_table.csv

# Host unit tests

OPENSSL_PREFIX ?= $(shell brew --prefix openssl@3 2>/dev/null || echo /usr)

.PHONY: test-host
test-host:
	mkdir -p $(BUILD_ROOT)/host
	cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -Imain -I$(OPENSSL_PREFIX)/include -o $(BUILD_ROOT)/host/test_decode test/host/test_decode.c test/host/aes_ctr_openssl.c main/decode.c main/lorawan_netid.c main/meshtastic.c -L$(OPENSSL_PREFIX)/lib -lcrypto
	$(BUILD_ROOT)/host/test_decode

.PHONY: render-host
render-host:
	cmake -S test/host/render -B $(BUILD_ROOT)/host/render >/dev/null
	cmake --build $(BUILD_ROOT)/host/render -j
	mkdir -p $(BUILD_ROOT)/host/render/out
	$(BUILD_ROOT)/host/render/render $(BUILD_ROOT)/host/render/out
	cd $(BUILD_ROOT)/host/render/out && for f in *.ppm; do sips -s format png "$$f" --out "$${f%.ppm}.png" >/dev/null; done
	echo "Screens rendered to $(BUILD_ROOT)/host/render/out"

# Formatting

.PHONY: format
format:
	find main/ -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' | xargs clang-format -i

# Re-compile protobuf files
# If you are an end user, you do not need to run this;
# the output files are already there in the repository.

.PHONY: compile-protobuf
compile-protobuf:
	protoc --pyi_out=tools --python_out=tools badgelink.proto
	python3 main/badgelink/nanopb/generator/nanopb_generator.py -D main/badgelink -f badgelink.options badgelink.proto

# Take all svg files from main/static/icons and put them in main/fat/icons as png using tools/connvert.sh
ICONS_SRC := $(wildcard main/static/icons/*.svg)
ICONS_DST := $(patsubst main/static/icons/%.svg,main/fat/icons/%.png,$(ICONS_SRC))

.PHONY: icons
icons: $(ICONS_DST)

main/fat/icons/%.png: main/static/icons/%.svg
	mkdir -p main/fat/icons
	tools/convert.sh $< $@
	
# Build all targets
.PHONY: buildall
buildall:
	$(MAKE) build DEVICE=tanmatsu
	$(MAKE) build DEVICE=esp32-p4-function-ev-board
	$(MAKE) build DEVICE=mch2022
	$(MAKE) build DEVICE=hackaday2025
	$(MAKE) build DEVICE=hackerhotel-2024
	$(MAKE) build DEVICE=heltecv3
	$(MAKE) build DEVICE=kami

# Vscode
.PHONY: vscode
vscode:
	rm -rf .vscode
	cp -r .vscode.template .vscode
