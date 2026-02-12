#!/usr/bin/env sh

WRAPPER='
# --- goto shell integration ---
goto() {
  local tempfile="/tmp/.goto_path"
  rm -f "$tempfile"

  command goto "$@"

  if [ -f "$tempfile" ]; then
    local target
    target="$(cat "$tempfile")"
    rm -f "$tempfile"
    cd "$target" || return
  fi
}
# --- end goto integration ---
'

# Detect shell
if [ -n "$ZSH_VERSION" ]; then
    RC="$HOME/.zshrc"
elif [ -n "$BASH_VERSION" ]; then
    RC="$HOME/.bashrc"
else
    # Fallback: inspect $SHELL
    case "$SHELL" in
        */zsh) RC="$HOME/.zshrc" ;;
        */bash) RC="$HOME/.bashrc" ;;
        *)
            echo "Unsupported shell. Please add wrapper manually."
            exit 1
            ;;
    esac
fi

echo "Detected shell rc file: $RC"

# Prevent duplicate install
if grep -q "goto shell integration" "$RC" 2>/dev/null; then
    echo "goto wrapper already installed."
    exit 0
fi

echo "Installing goto wrapper..."
printf "%s\n" "$WRAPPER" >> "$RC"

echo "Done."
echo "Restart your shell or run:"
echo "  source $RC"