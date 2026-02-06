# -------- Toolchain --------
CC ?= clang
CFLAGS ?= -Wall -Wextra -std=c11 -DUSE_NERD_FONTS
CPPFLAGS ?=
LDFLAGS ?=

# -------- Dirs/Targets --------
SRC_DIR   = src
BUILD_DIR = build
BIN_DIR   = bin

SRC    = $(SRC_DIR)/main.c
OBJ    = $(BUILD_DIR)/main.o
TARGET = $(BIN_DIR)/goto

# -------- OS / ncurses detection --------
UNAME_S := $(shell uname -s)

# Prefer pkg-config on Linux for correct ncurses flags (esp. ncursesw)
ifeq ($(UNAME_S),Linux)
  PKG_CONFIG ?= pkg-config
  # Try ncursesw first, then ncurses
  NCURSES_PC := $(shell $(PKG_CONFIG) --exists ncursesw && echo ncursesw || \
                      ($(PKG_CONFIG) --exists ncurses && echo ncurses))
  ifneq ($(NCURSES_PC),)
    CPPFLAGS += $(shell $(PKG_CONFIG) --cflags $(NCURSES_PC))
    LDFLAGS  += $(shell $(PKG_CONFIG) --libs   $(NCURSES_PC))
  else
    # Fallback if pkg-config or .pc files aren't available
    LDFLAGS  += -lncurses
  endif
else
  # macOS and others (often works without pkg-config)
  LDFLAGS += -lncurses
endif

# -------- Rules --------
all: $(TARGET)

$(TARGET): $(OBJ) | $(BIN_DIR)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

$(OBJ): $(SRC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(SRC) -o $(OBJ)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

install: $(TARGET)
	@echo "======================================"
	@echo "Installation complete!"
	@echo "======================================"
	@echo ""
	@echo "Add this function to your ~/.zshrc (or ~/.bashrc):"
	@echo ""
	@echo "goto() {"
	@echo "    local tempfile=\"/tmp/.goto_path\""
	@echo "    rm -f \"\$$tempfile\""
	@echo "    $(shell pwd)/$(TARGET)"
	@echo "    if [ -f \"\$$tempfile\" ]; then"
	@echo "        local target=\$$(cat \"\$$tempfile\")"
	@echo "        rm -f \"\$$tempfile\""
	@echo "        cd \"\$$target\" && pwd"
	@echo "    fi"
	@echo "}"
	@echo ""
	@echo "Then run: source ~/.zshrc"
	@echo ""

.PHONY: all clean install