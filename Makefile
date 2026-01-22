CC = clang
CFLAGS = -Wall -Wextra -std=c11
LDFLAGS = -lncurses

SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin

SRC = $(SRC_DIR)/main.c
OBJ = $(BUILD_DIR)/main.o
TARGET = $(BIN_DIR)/goto

all: $(TARGET)

$(TARGET): $(OBJ) | $(BIN_DIR)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

$(OBJ): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $(SRC) -o $(OBJ)

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
	@echo "    rm -f \"\$tempfile\""
	@echo "    $(shell pwd)/$(TARGET)"
	@echo "    if [ -f \"\$tempfile\" ]; then"
	@echo "        local target=\$(cat \"\$tempfile\")"
	@echo "        rm -f \"\$tempfile\""
	@echo "        cd \"\$target\" && pwd"
	@echo "    fi"
	@echo "}"
	@echo ""
	@echo "Then run: source ~/.zshrc"
	@echo ""

.PHONY: all clean install
