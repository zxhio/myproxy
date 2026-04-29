APP       := myproxy
VERSION   ?= dev
GOOS      ?= linux
GOARCH    ?= amd64

# Cross-compilation settings
ifeq ($(GOOS),linux)
  ifeq ($(GOARCH),arm64)
    CMAKE_ARGS := -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64
  else
    CMAKE_ARGS :=
  endif
else ifeq ($(GOOS),darwin)
  CMAKE_ARGS := -DCMAKE_OSX_ARCHITECTURES=$(GOARCH)
endif

.PHONY: build build-linux-amd64 build-linux-arm64 build-darwin-x86_64 build-darwin-arm64 \
        build-all pack pack-all clean

# --- Build ---

build:
	mkdir -p build && cd build && cmake .. $(CMAKE_ARGS) && cmake --build .

build-linux-amd64:
	$(MAKE) build GOOS=linux GOARCH=amd64

build-linux-arm64:
	$(MAKE) build GOOS=linux GOARCH=arm64

build-darwin-x86_64:
	$(MAKE) build GOOS=darwin GOARCH=x86_64

build-darwin-arm64:
	$(MAKE) build GOOS=darwin GOARCH=arm64

build-all: build

# --- Pack ---

pack: build
	VERSION=$(VERSION) GOOS=$(GOOS) GOARCH=$(GOARCH) ./scripts/package-release.sh

pack-linux-amd64:
	$(MAKE) pack GOOS=linux GOARCH=amd64

pack-linux-arm64:
	$(MAKE) pack GOOS=linux GOARCH=arm64

pack-darwin-x86_64:
	$(MAKE) pack GOOS=darwin GOARCH=x86_64

pack-darwin-arm64:
	$(MAKE) pack GOOS=darwin GOARCH=arm64

pack-all: pack-linux-amd64 pack-linux-arm64 pack-darwin-x86_64 pack-darwin-arm64

# --- Clean ---

clean:
	rm -rf build dist
