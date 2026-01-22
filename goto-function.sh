#!/bin/bash

# goto - Interactive directory navigator
# Add this to your ~/.zshrc or ~/.bashrc:
#   source /path/to/goto-function.sh

goto() {
    local tempfile="/tmp/.goto_path"
    rm -f "$tempfile"
    
    # Get the directory where this script lives
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    
    # Run the goto binary
    "$script_dir/bin/goto"
    
    # If user pressed 'o', cd to that directory
    if [ -f "$tempfile" ]; then
        local target=$(cat "$tempfile")
        rm -f "$tempfile"
        cd "$target" && pwd
    fi
}
