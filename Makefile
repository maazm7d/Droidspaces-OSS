# Droidspaces v3 — Build system
# Static compilation with musl libc for Android/Linux portability

BINARY_NAME = droidspaces
SRC_DIR     = src
OUT_DIR     = output
VERSION     = 3.0

# Source files
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/utils.c \
       $(SRC_DIR)/android.c \
       $(SRC_DIR)/mount.c \
       $(SRC_DIR)/network.c \
       $(SRC_DIR)/terminal.c \
       $(SRC_DIR)/console.c \
       $(SRC_DIR)/pid.c \
       $(SRC_DIR)/boot.c \
       $(SRC_DIR)/container.c \
       $(SRC_DIR)/check.c

# Default CC for native builds
CC ?= gcc

# Auto-detect arch from compiler
ARCH := $(shell $(CC) -dumpmachine 2>/dev/null | cut -d'-' -f1 | \
        sed 's/x86_64/x86_64/; s/aarch64/aarch64/; s/i686/x86/; \
             s/armv7l/armhf/; s/^arm/armhf/; s/unknown/x86_64/' || echo "x86_64")

# Compiler flags
CFLAGS  = -Wall -Wextra -O2 -flto -std=gnu99 -I$(SRC_DIR) -no-pie -pthread
CFLAGS += -Wno-unused-parameter -Wno-unused-result
LDFLAGS = -static -no-pie -flto -pthread
LIBS    = -lutil

# Cross-compiler toolchains (musl)
CC_x86_64  ?= x86_64-linux-musl-gcc
CC_aarch64 ?= aarch64-linux-musl-gcc
CC_armhf   ?= arm-linux-musleabihf-gcc
CC_x86     ?= i686-linux-musl-gcc

# Default target: build for detected arch
.PHONY: all clean x86_64 aarch64 armhf x86 all-build tarball

all: $(ARCH)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

x86_64: $(OUT_DIR)
	@echo "[*] Building $(BINARY_NAME) v$(VERSION) for x86_64..."
	@$(CC_x86_64) $(CFLAGS) $(SRCS) -o $(OUT_DIR)/$(BINARY_NAME) $(LDFLAGS) $(LIBS)
	@strip $(OUT_DIR)/$(BINARY_NAME) 2>/dev/null || true
	@echo "[+] Built: $(OUT_DIR)/$(BINARY_NAME) (x86_64)"

aarch64: $(OUT_DIR)
	@echo "[*] Building $(BINARY_NAME) v$(VERSION) for aarch64..."
	@$(CC_aarch64) $(CFLAGS) $(SRCS) -o $(OUT_DIR)/$(BINARY_NAME) $(LDFLAGS) $(LIBS)
	@strip $(OUT_DIR)/$(BINARY_NAME) 2>/dev/null || true
	@echo "[+] Built: $(OUT_DIR)/$(BINARY_NAME) (aarch64)"

armhf: $(OUT_DIR)
	@echo "[*] Building $(BINARY_NAME) v$(VERSION) for armhf..."
	@$(CC_armhf) $(CFLAGS) $(SRCS) -o $(OUT_DIR)/$(BINARY_NAME) $(LDFLAGS) $(LIBS)
	@strip $(OUT_DIR)/$(BINARY_NAME) 2>/dev/null || true
	@echo "[+] Built: $(OUT_DIR)/$(BINARY_NAME) (armhf)"

x86: $(OUT_DIR)
	@echo "[*] Building $(BINARY_NAME) v$(VERSION) for x86..."
	@$(CC_x86) $(CFLAGS) $(SRCS) -o $(OUT_DIR)/$(BINARY_NAME) $(LDFLAGS) $(LIBS)
	@strip $(OUT_DIR)/$(BINARY_NAME) 2>/dev/null || true
	@echo "[+] Built: $(OUT_DIR)/$(BINARY_NAME) (x86)"

all-build:
	@echo "[*] Building for all architectures..."
	@mkdir -p $(OUT_DIR)
	@$(MAKE) --no-print-directory x86_64 OUT_DIR=$(OUT_DIR)/x86_64
	@$(MAKE) --no-print-directory aarch64 OUT_DIR=$(OUT_DIR)/aarch64
	@$(MAKE) --no-print-directory armhf OUT_DIR=$(OUT_DIR)/armhf
	@$(MAKE) --no-print-directory x86 OUT_DIR=$(OUT_DIR)/x86
	@echo "[+] All architectures built successfully"

tarball: all-build
	@echo "[*] Creating distribution tarball..."
	@tar czf $(BINARY_NAME)-$(VERSION).tar.gz -C $(OUT_DIR) .
	@echo "[+] Created: $(BINARY_NAME)-$(VERSION).tar.gz"

clean:
	@rm -rf $(OUT_DIR) $(BINARY_NAME)-*.tar.gz
	@echo "[+] Cleaned build artifacts"
