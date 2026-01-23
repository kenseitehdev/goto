clone repo and make && make install


DEPS:
requires ncurses
requires watch 
requires tmux
requires tree



add goto function to bashrc or zshrc or fishrc

goto() {
    local tempfile="/tmp/.goto_path"
    rm -f "$tempfile"
goto_dir/goto_executable_path
    if [ -f "$tempfile" ]; then
        local target=$(cat "$tempfile")
        rm -f "$tempfile"
        cd "$target"
    fi
}


