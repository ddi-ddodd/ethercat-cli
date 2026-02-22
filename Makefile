BUILD_DIR  := build
BUILD_TYPE ?= Release
TOOLCHAIN  := $(BUILD_DIR)/conan_toolchain.cmake

.PHONY: all setup build clean rebuild

all: build

## setup: build + cache the SOEM library, then generate the consumer toolchain
setup:
	conan create . --build=missing
	conan install conanfile_consumer.py \
	    --output-folder=$(BUILD_DIR) \
	    --build=missing

## If the toolchain file is missing, run setup first
$(TOOLCHAIN):
	$(MAKE) setup

## build: cmake configure (idempotent) then compile
build: $(TOOLCHAIN)
	cmake -S . -B $(BUILD_DIR) \
	    -DCMAKE_TOOLCHAIN_FILE=$(BUILD_DIR)/conan_toolchain.cmake \
	    -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build $(BUILD_DIR) --config $(BUILD_TYPE)

## clean: remove the build directory entirely (cross-platform)
clean:
	$(if $(filter Windows_NT,$(OS)),cmd.exe /c "if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)",rm -rf $(BUILD_DIR))

## rebuild: full clean, then setup, then build
rebuild: clean setup build

profile: 
	conan profile detect --force

