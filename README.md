clone repo and make && make install


DEPS:
requires ncurses



add goto function to bashrc or zshrc or fishrc

goto() {
    local tempfile="/tmp/.goto_path"
    rm -f "$tempfile"
    /Users/{YOURUSER}/Documents/Projects/goto/bin/goto
    if [ -f "$tempfile" ]; then
        local target=$(cat "$tempfile")
        rm -f "$tempfile"
        cd "$target" && pwd
    fi
}


