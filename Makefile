CC := gcc
CFLAGS ?= -Wall -Wextra -O2 -std=c11
DEBUG_CFLAGS ?= -Wall -Wextra -g -O0 -std=c11 -DDEBUG

SRC_DIR := src
INCLUDE_DIR := include
BUILD_DIR := build

ifeq ($(OS), Windows_NT)
    PLATFORM := windows
    EXE := .exe
    PKG_CFLAGS :=
    PKG_LIBS := -luser32 -lgdi32 -lole32 -lshlwapi -lcomdlg32 -lshell32
    MKDIR = mkdir -p $(1)
    RM = rm -rf $(1) 
    RUN_EMBED = ./$(BUILD_DIR)/embed$(EXE)
    RUN_TARGET = ./$(TARGET)
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S), Linux)
        PLATFORM := linux
        EXE :=
        PKGS := webkitgtk-6.0 gtk4
        PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS))
        PKG_LIBS := $(shell pkg-config --libs $(PKGS))
    else ifeq ($(UNAME_S), Darwin)
        PLATFORM := macos
        EXE :=
        PKG_CFLAGS :=
        PKG_LIBS := -framework Cocoa -framework WebKit
    endif
    MKDIR = mkdir -p $(1)
    RM = rm -rf $(1)
    RUN_EMBED = ./$(BUILD_DIR)/embed
    RUN_TARGET = ./$(TARGET)
endif

TARGET := $(BUILD_DIR)/app$(EXE)
EMBED_BIN := $(BUILD_DIR)/embed$(EXE)

# Common source files in src/ (main.c, engine/*.c, commands/*.c)
COMMON_SRCS := $(wildcard $(SRC_DIR)/*.c) \
               $(wildcard $(SRC_DIR)/engine/*.c) \
               $(wildcard $(SRC_DIR)/commands/*.c)
PLATFORM_SRCS := $(wildcard $(SRC_DIR)/platform/$(PLATFORM)/*.c) $(wildcard $(SRC_DIR)/platform/$(PLATFORM)/*.m)
SRCS := $(COMMON_SRCS) $(PLATFORM_SRCS)

# Convert src/foo.c -> build/foo.o and src/platform/... -> build/platform/...
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(filter %.c, $(SRCS))) \
        $(patsubst $(SRC_DIR)/%.m, $(BUILD_DIR)/%.o, $(filter %.m, $(SRCS)))

.PHONY: all run debug release clean test

all: $(TARGET)

# 1. Compile the self-hosted embed tool
$(EMBED_BIN): tools/embed.c | $(BUILD_DIR)
	@$(CC) $(CFLAGS) $< -o $@

# 2. Generate bridge.h whenever assets/bridge.js changes
$(BUILD_DIR)/bridge.h: assets/bridge.js $(EMBED_BIN) | $(BUILD_DIR)
	@$(RUN_EMBED) assets/bridge.js $@ bridge_js

$(TARGET): $(OBJS) | $(BUILD_DIR)
	@echo   [LD]    $@
	@$(CC) $(CFLAGS) $(OBJS) $(PKG_LIBS) -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(BUILD_DIR)/bridge.h | $(BUILD_DIR)
	@$(call MKDIR,$(dir $@))
	@echo   [CC]    $<
	@$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -I$(BUILD_DIR) $(PKG_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.m $(BUILD_DIR)/bridge.h | $(BUILD_DIR)
	@$(call MKDIR,$(dir $@))
	@echo   [OBJC]  $<
	@$(CC) $(CFLAGS) -fobjc-arc -I$(INCLUDE_DIR) -I$(BUILD_DIR) $(PKG_CFLAGS) -c $< -o $@

$(BUILD_DIR):
	@$(call MKDIR,$(BUILD_DIR))

# Quick run command
run: $(TARGET)
	@echo   [RUN]   $(TARGET)
	@$(RUN_TARGET)

# Debug build with symbols (-g) and no optimizations (-O0)
debug: CFLAGS := $(DEBUG_CFLAGS)
debug: clean $(TARGET)

# Release build: strips all debug symbols, unused functions, and exception tables
release: CFLAGS += -s -Os -ffunction-sections -Wl,--gc-sections -fno-asynchronous-unwind-tables
release: clean $(TARGET)

# Unit tests for pure C11 engine (ASan + UBSan leak/bounds checking)
test: $(BUILD_DIR)
	@echo   [TEST]  Compiling engine suite with ASan + UBSan...
	@$(CC) -Wall -Wextra -g -O1 -std=c11 -fsanitize=address,undefined -I$(INCLUDE_DIR) $(PKG_CFLAGS) tests/test_engine.c $(wildcard $(SRC_DIR)/engine/*.c) -o $(BUILD_DIR)/test_engine$(EXE)
	@./$(BUILD_DIR)/test_engine$(EXE)

clean:
	@echo   [CLEAN] Removing $(BUILD_DIR)/
	@$(call RM,$(BUILD_DIR))
