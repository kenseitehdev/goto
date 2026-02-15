//goto - Fixed version with security improvements and bug fixes
#ifndef USE_NERD_FONTS
#define USE_NERD_FONTS
#endif
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <locale.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <limits.h>
#include <fcntl.h>

#define MAX_PATH 4096
#define MAX_ITEMS 1024
#define QUOTE_BUF_SIZE (MAX_PATH * 4 + 4)

#define ICON_FOLDER "\ue5ff"
#define ICON_FOLDER_OPEN "\uf07c"
#define ICON_FILE "\uf15b"
#define ICON_C "\ue61e"
#define ICON_PYTHON "\ue73c"
#define ICON_JS "\ued0d"
#define ICON_RUST "\ue7a8"
#define ICON_GO "\ue65e"
#define ICON_JSON "\ueb0f"
#define ICON_MD "\ueb1d"
#define ICON_YAML "\ue8eb"
#define ICON_SH "\ue691"
#define ICON_IMG "\uf03e"
#define ICON_EXEC "\uf0e7"
#define ICON_GIT "\ue702"
#define ICON_HIDDEN "\uea70"

#define ASCII_EMPTYFILE "o"
#define ASCII_FILE "-"
#define ASCII_EMPTYDIR "D"
#define ASCII_DIR "D="
#define ASCII_HIDDEN_DIR "H"
#define ASCII_HIDDEN_FILE "h"

// Forward declarations
static void shell_quote_single(char *out, size_t out_len, const char *in);
static void cleanup_handler(int sig);
static void register_signal_handlers(void);

#ifndef USE_NERD_FONTS
    // Redefine all icons as ASCII fallbacks
    #undef ICON_FOLDER
    #undef ICON_FOLDER_OPEN
    #undef ICON_FILE
    #undef ICON_C
    #undef ICON_PYTHON
    #undef ICON_JS
    #undef ICON_RUST
    #undef ICON_GO
    #undef ICON_JSON
    #undef ICON_MD
    #undef ICON_YAML
    #undef ICON_SH
    #undef ICON_IMG
    #undef ICON_EXEC
    #undef ICON_GIT
    #undef ICON_HIDDEN

    #define ICON_FOLDER ASCII_DIR
    #define ICON_FOLDER_OPEN ASCII_DIR
    #define ICON_FILE ASCII_FILE
    #define ICON_C ASCII_FILE
    #define ICON_PYTHON ASCII_FILE
    #define ICON_JS ASCII_FILE
    #define ICON_RUST ASCII_FILE
    #define ICON_GO ASCII_FILE
    #define ICON_JSON ASCII_FILE
    #define ICON_MD ASCII_FILE
    #define ICON_YAML ASCII_FILE
    #define ICON_SH ASCII_FILE
    #define ICON_IMG ASCII_FILE
    #define ICON_EXEC ASCII_FILE
    #define ICON_GIT ASCII_HIDDEN_DIR
    #define ICON_HIDDEN ASCII_HIDDEN_DIR
#endif

typedef enum {
    SORT_NAME = 0,
    SORT_SIZE,
    SORT_TIME,
    SORT_EXT
} SortMode;

typedef enum {
    FILTER_ALL = 0,
    FILTER_FILES,
    FILTER_DIRS,
    FILTER_CONTAINS
} FilterMode;

typedef struct {
    char name[256];
    char full_path[MAX_PATH];
    mode_t mode;
    off_t size;
    time_t mtime;
    int is_dir;
    int is_hidden;
} FileItem;

typedef struct {
    FileItem items[MAX_ITEMS];
    int count;
    int selected;
    int scroll_offset;
    char cwd[MAX_PATH];
    int show_hidden;

    SortMode sort_mode;
    int sort_reverse;

    FilterMode filter_mode;
    char filter_text[256];

    char pending_prefix;

} FileList;

// Global state for cleanup
static char g_terminal_pane_id[128] = {0};
static char g_temp_files[10][PATH_MAX] = {{0}};
static int g_temp_file_count = 0;

// Signal handler for cleanup on interrupt
static void cleanup_handler(int sig) {
    (void)sig;
    endwin();
    
    // Clean up temp files
    for (int i = 0; i < g_temp_file_count; i++) {
        if (g_temp_files[i][0] != '\0') {
            unlink(g_temp_files[i]);
        }
    }
    
    exit(0);
}

static void register_signal_handlers(void) {
    signal(SIGINT, cleanup_handler);
    signal(SIGTERM, cleanup_handler);
}

static void register_temp_file(const char *path) {
    if (g_temp_file_count < 10 && path && *path) {
        strncpy(g_temp_files[g_temp_file_count], path, PATH_MAX - 1);
        g_temp_files[g_temp_file_count][PATH_MAX - 1] = '\0';
        g_temp_file_count++;
    }
}

static void popup_message(const char *title, const char *message) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int w = (int)(max_x * 0.6);
    if (w < 44) w = 44;
    if (w > max_x - 4) w = max_x - 4;

    int h = 7;
    int y = (max_y - h) / 2;
    int x = (max_x - w) / 2;

    WINDOW *win = newwin(h, w, y, x);
    if (!win) return;
    
    keypad(win, TRUE);
    box(win, 0, 0);

    if (title) mvwprintw(win, 0, 2, " %s ", title);
    mvwprintw(win, 2, 2, "%s", message ? message : "");
    mvwprintw(win, 4, 2, "Press any key...");
    wrefresh(win);

    wgetch(win);
    delwin(win);
    touchwin(stdscr);
    refresh();
}

static int popup_confirm(const char *title, const char *message) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int w = (int)(max_x * 0.6);
    if (w < 44) w = 44;
    if (w > max_x - 4) w = max_x - 4;

    int h = 7;
    int y = (max_y - h) / 2;
    int x = (max_x - w) / 2;

    WINDOW *win = newwin(h, w, y, x);
    if (!win) return 0;
    
    keypad(win, TRUE);
    box(win, 0, 0);

    if (title) mvwprintw(win, 0, 2, " %s ", title);
    mvwprintw(win, 2, 2, "%s", message ? message : "Are you sure?");
    mvwprintw(win, 4, 2, "[y] Yes    [n] No");
    wrefresh(win);

    int ch;
    int result = 0;
    while ((ch = wgetch(win))) {
        if (ch == 'y' || ch == 'Y') { result = 1; break; }
        if (ch == 'n' || ch == 'N' || ch == 27) { result = 0; break; }
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
    return result;
}

static int popup_prompt(char *out, size_t out_len, const char *title, const char *label) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int w = (int)(max_x * 0.6);
    if (w < 44) w = 44;
    if (w > max_x - 4) w = max_x - 4;

    int h = 7;
    int y = (max_y - h) / 2;
    int x = (max_x - w) / 2;

    WINDOW *win = newwin(h, w, y, x);
    if (!win) return 0;
    
    keypad(win, TRUE);
    box(win, 0, 0);

    if (title) mvwprintw(win, 0, 2, " %s ", title);
    mvwprintw(win, 2, 2, "%s", label ? label : "Input:");
    mvwprintw(win, 4, 2, "> ");
    wmove(win, 4, 4);
    wrefresh(win);

    echo();
    curs_set(1);

    char buf[1024] = {0};
    wgetnstr(win, buf, (int)sizeof(buf) - 1);

    noecho();
    curs_set(0);

    delwin(win);
    touchwin(stdscr);
    refresh();

    size_t len = strlen(buf);
    while (len > 0 && isspace((unsigned char)buf[len - 1])) buf[--len] = '\0';
    size_t start = 0;
    while (buf[start] && isspace((unsigned char)buf[start])) start++;

    if (buf[start] == '\0') {
        out[0] = '\0';
        return 0;
    }

    strncpy(out, buf + start, out_len - 1);
    out[out_len - 1] = '\0';
    return 1;
}

static int command_exists(const char *cmd) {
    if (!cmd || !*cmd) return 0;
    
    // Validate command name (no shell metacharacters)
    for (const char *p = cmd; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') {
            return 0;
        }
    }
    
    char buf[512];
    int ret = snprintf(buf, sizeof(buf), "command -v %s >/dev/null 2>&1", cmd);
    if (ret < 0 || ret >= (int)sizeof(buf)) return 0;
    
    return system(buf) == 0;
}

// returns "ff", "fzf", or NULL
static const char *pick_fuzzy_tool(void) {
    if (command_exists("ff")) return "ff";
    if (command_exists("fzf"))  return "fzf";
    return NULL;
}

// simple manual fallback: prompt for substring, select first match in current list
static int manual_select_in_list(FileList *list) {
    char q[256] = {0};
    if (!popup_prompt(q, sizeof(q), "Search", "Substring to match (empty cancels):")) return 0;
    if (q[0] == '\0') return 0;

    for (int i = 0; i < list->count; i++) {
        if (strstr(list->items[i].name, q) != NULL) {
            list->selected = i;

            int max_y, max_x;
            getmaxyx(stdscr, max_y, max_x);
            int visible = max_y - 3;

            if (list->selected < list->scroll_offset) list->scroll_offset = list->selected;
            if (list->selected >= list->scroll_offset + visible)
                list->scroll_offset = list->selected - visible + 1;

            return 1;
        }
    }

    popup_message("No match", "No items matched your query.");
    return 0;
}

static int create_new_file(const char *cwd, const char *name) {
    // Validate name
    if (!name || !*name || strchr(name, '/') || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        errno = EINVAL;
        return -1;
    }

    char path[MAX_PATH];
    int ret = snprintf(path, sizeof(path), "%s/%s", cwd, name);
    if (ret < 0 || ret >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    // Use O_EXCL to atomically fail if file exists (prevents TOCTOU)
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return -1;
    
    close(fd);
    return 0;
}

static int create_new_dir(const char *cwd, const char *name) {
    // Validate name
    if (!name || !*name || strchr(name, '/') || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        errno = EINVAL;
        return -1;
    }

    char path[MAX_PATH];
    int ret = snprintf(path, sizeof(path), "%s/%s", cwd, name);
    if (ret < 0 || ret >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    // mkdir with O_EXCL behavior is atomic
    return mkdir(path, 0755);
}

static int delete_item_shallow(const FileItem *item) {
    if (item->is_dir) return rmdir(item->full_path);
    return unlink(item->full_path);
}

static int rename_item(const FileItem *item, const char *new_name) {
    // Validate new name
    if (!new_name || !*new_name || strchr(new_name, '/') || 
        strcmp(new_name, ".") == 0 || strcmp(new_name, "..") == 0) {
        errno = EINVAL;
        return -1;
    }

    char dirbuf[MAX_PATH];
    strncpy(dirbuf, item->full_path, sizeof(dirbuf) - 1);
    dirbuf[sizeof(dirbuf) - 1] = '\0';

    char *slash = strrchr(dirbuf, '/');
    if (!slash) { errno = EINVAL; return -1; }
    *slash = '\0';

    char new_path[MAX_PATH];
    int ret = snprintf(new_path, sizeof(new_path), "%s/%s", dirbuf, new_name);
    if (ret < 0 || ret >= (int)sizeof(new_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return rename(item->full_path, new_path);
}

const char* get_file_icon(FileItem *item) {
    if (item->is_dir) {
        if (strcmp(item->name, ".git") == 0) return ICON_GIT;
        if (item->is_hidden) return ICON_HIDDEN;
        return ICON_FOLDER;
    }

    const char *ext = strrchr(item->name, '.');
    if (!ext) {
        if (item->mode & S_IXUSR) return ICON_EXEC;
        return ICON_FILE;
    }

    if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) return ICON_C;
    if (strcmp(ext, ".py") == 0) return ICON_PYTHON;
    if (strcmp(ext, ".js") == 0 || strcmp(ext, ".ts") == 0) return ICON_JS;
    if (strcmp(ext, ".jsx") == 0 || strcmp(ext, ".tsx") == 0) return ICON_JS;
    if (strcmp(ext, ".rs") == 0) return ICON_RUST;
    if (strcmp(ext, ".go") == 0) return ICON_GO;
    if (strcmp(ext, ".json") == 0) return ICON_JSON;
    if (strcmp(ext, ".md") == 0) return ICON_MD;
    if (strcmp(ext, ".yaml") == 0 || strcmp(ext, ".yml") == 0) return ICON_YAML;
    if (strcmp(ext, ".sh") == 0 || strcmp(ext, ".bash") == 0 || strcmp(ext, ".zsh") == 0) return ICON_SH;
    if (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".gif") == 0) return ICON_IMG;

    return ICON_FILE;
}

int get_file_color(FileItem *item) {
    if (item->is_dir) return 1;

    if (item->mode & S_IXUSR) return 2;

    const char *ext = strrchr(item->name, '.');
    if (!ext) return 4;

    if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) return 5;
    if (strcmp(ext, ".py") == 0) return 6;
    if (strcmp(ext, ".js") == 0 || strcmp(ext, ".ts") == 0) return 2;
    if (strcmp(ext, ".rs") == 0) return 7;
    if (strcmp(ext, ".md") == 0) return 6;

    return 4;
}

static const char* file_ext(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext || ext == name) return "";
    return ext + 1;
}

static int compare_items(const void *a, const void *b, void *ctx) {
    const FileList *list = (const FileList*)ctx;
    const FileItem *A = (const FileItem*)a;
    const FileItem *B = (const FileItem*)b;

    if (A->is_dir != B->is_dir) {
        int r = (B->is_dir - A->is_dir);
        return list->sort_reverse ? -r : r;
    }

    int r = 0;
    switch (list->sort_mode) {
        case SORT_NAME:
            r = strcasecmp(A->name, B->name);
            break;
        case SORT_SIZE:
            if (A->size < B->size) r = -1;
            else if (A->size > B->size) r = 1;
            else r = strcasecmp(A->name, B->name);
            break;
        case SORT_TIME:
            if (A->mtime < B->mtime) r = -1;
            else if (A->mtime > B->mtime) r = 1;
            else r = strcasecmp(A->name, B->name);
            break;
        case SORT_EXT: {
            const char *ea = file_ext(A->name);
            const char *eb = file_ext(B->name);
            r = strcasecmp(ea, eb);
            if (r == 0) r = strcasecmp(A->name, B->name);
            break;
        }
        default:
            r = strcasecmp(A->name, B->name);
            break;
    }

    return list->sort_reverse ? -r : r;
}

// Thread-safe sorting using qsort_r when available
#if defined(__APPLE__)
// macOS has different signature: context comes first
static int compare_items_wrapper(void *ctx, const void *a, const void *b) {
    return compare_items(a, b, ctx);
}

static void sort_items_portable(FileList *list) {
    qsort_r(list->items, list->count, sizeof(FileItem), list, compare_items_wrapper);
}
#elif defined(__GLIBC__)
// glibc has context last
static int compare_items_wrapper(const void *a, const void *b, void *ctx) {
    return compare_items(a, b, ctx);
}

static void sort_items_portable(FileList *list) {
    qsort_r(list->items, list->count, sizeof(FileItem), compare_items_wrapper, list);
}
#else
// Fallback for systems without qsort_r
static FileList *g_sort_ctx = NULL;
static int compare_items_static(const void *a, const void *b) {
    return compare_items(a, b, g_sort_ctx);
}

static void sort_items_portable(FileList *list) {
    g_sort_ctx = list;
    qsort(list->items, list->count, sizeof(FileItem), compare_items_static);
    g_sort_ctx = NULL;
}
#endif

static int ff_grep_selected_file(FileList *list, int *out_line) {
    if (!list || !out_line) return -1;
    *out_line = 0;

    if (list->selected < 0 || list->selected >= list->count) return 0;

    FileItem *it = &list->items[list->selected];
    if (it->is_dir) {
        popup_message("Not a file", "Select a file first.");
        return 0;
    }
    if (strcmp(it->name, ".") == 0 || strcmp(it->name, "..") == 0) {
        popup_message("Nope", "Refusing to search '.' or '..'.");
        return 0;
    }

    if (!command_exists("ff")) {
        popup_message("Missing ff", "Install `ff` or put it in PATH.");
        return 0;
    }

    char tmp_out[] = "/tmp/goto_ff_linepick_XXXXXX";
    int fd = mkstemp(tmp_out);
    if (fd < 0) return -1;
    close(fd);
    register_temp_file(tmp_out);

    char qcwd[QUOTE_BUF_SIZE];
    char qfile[QUOTE_BUF_SIZE];
    char qout[QUOTE_BUF_SIZE];

    shell_quote_single(qcwd, sizeof(qcwd), list->cwd);
    shell_quote_single(qfile, sizeof(qfile), it->full_path);
    shell_quote_single(qout,  sizeof(qout),  tmp_out);

    /* Use nl -ba so ff shows "  123  actual line contents" (no ':' delimiter) */
    char cmd[16384];
    int ret = snprintf(cmd, sizeof(cmd),
        "sh -lc \""
        "cd %s || exit 1; "
        "nl -ba -- %s | ff > %s 2> /dev/tty"
        "\"",
        qcwd, qfile, qout
    );
    
    if (ret < 0 || ret >= (int)sizeof(cmd)) {
        unlink(tmp_out);
        return -1;
    }

    fflush(stdout);
    fflush(stderr);
    def_prog_mode();
    endwin();

    int sys_rc = system(cmd);

    reset_prog_mode();
    refresh();
    clear();

    if (sys_rc == -1) {
        unlink(tmp_out);
        return -1;
    }

    int exit_code = 0;
    if (WIFEXITED(sys_rc)) exit_code = WEXITSTATUS(sys_rc);
    else {
        unlink(tmp_out);
        return 0;
    }

    if (exit_code != 0) {
        unlink(tmp_out);
        return 0;
    }

    FILE *fp = fopen(tmp_out, "r");
    if (!fp) {
        unlink(tmp_out);
        return -1;
    }

    char sel[4096] = {0};
    if (!fgets(sel, sizeof(sel), fp)) {
        fclose(fp);
        unlink(tmp_out);
        return 0;
    }
    fclose(fp);
    unlink(tmp_out);

    sel[strcspn(sel, "\r\n")] = '\0';
    if (sel[0] == '\0') return 0;

    /* sel format from nl: "   12\tThe line..." or "   12  The line..." */
    char *p = sel;
    while (*p && isspace((unsigned char)*p)) p++;

    char *end = NULL;
    long ln = strtol(p, &end, 10);
    if (ln <= 0) return 0;

    *out_line = (int)ln;
    return 1;
}

static int passes_filter(const FileList *list, const FileItem *item) {
    switch (list->filter_mode) {
        case FILTER_ALL:
            return 1;
        case FILTER_FILES:
            return !item->is_dir;
        case FILTER_DIRS:
            return item->is_dir;
        case FILTER_CONTAINS:
            if (list->filter_text[0] == '\0') return 1;
            return strstr(item->name, list->filter_text) != NULL;
        default:
            return 1;
    }
}

int load_directory(FileList *list, const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return -1;

    list->count = 0;
    list->selected = 0;
    list->scroll_offset = 0;

    char resolved[MAX_PATH];
    if (realpath(path, resolved)) {
        strncpy(list->cwd, resolved, MAX_PATH - 1);
        list->cwd[MAX_PATH - 1] = '\0';
    } else {
        strncpy(list->cwd, path, MAX_PATH - 1);
        list->cwd[MAX_PATH - 1] = '\0';
    }

    if (chdir(list->cwd) != 0) {
        closedir(dir);
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && list->count < MAX_ITEMS) {
        int is_hidden = (entry->d_name[0] == '.');
        if (is_hidden && !list->show_hidden) continue;

        FileItem tmp = (FileItem){0};
        strncpy(tmp.name, entry->d_name, 255);
        tmp.name[255] = '\0';

        int ret = snprintf(tmp.full_path, MAX_PATH, "%s/%s", list->cwd, entry->d_name);
        if (ret < 0 || ret >= MAX_PATH) continue;

        struct stat st;
        if (lstat(tmp.full_path, &st) == 0) {
            tmp.mode = st.st_mode;
            tmp.size = st.st_size;
            tmp.mtime = st.st_mtime;
            tmp.is_dir = S_ISDIR(st.st_mode);
        } else {
            tmp.is_dir = 0;
            tmp.mode = 0;
            tmp.size = 0;
            tmp.mtime = 0;
        }

        tmp.is_hidden = is_hidden;

        if (!passes_filter(list, &tmp)) continue;

        list->items[list->count++] = tmp;
    }

    closedir(dir);

    sort_items_portable(list);

    return 0;
}

void format_size(off_t size, char *buf, size_t len) {
    if (size < 1024) snprintf(buf, len, "%lldB", (long long)size);
    else if (size < 1024 * 1024) snprintf(buf, len, "%.1fK", size / 1024.0);
    else if (size < 1024 * 1024 * 1024) snprintf(buf, len, "%.1fM", size / (1024.0 * 1024.0));
    else snprintf(buf, len, "%.1fG", size / (1024.0 * 1024.0 * 1024.0));
}

static const char* sort_label(SortMode m) {
    switch (m) {
        case SORT_NAME: return "name";
        case SORT_SIZE: return "size";
        case SORT_TIME: return "time";
        case SORT_EXT:  return "ext";
        default: return "name";
    }
}

static void filter_label(const FileList *list, char *out, size_t out_len) {
    switch (list->filter_mode) {
        case FILTER_ALL:
            snprintf(out, out_len, "all");
            break;
        case FILTER_FILES:
            snprintf(out, out_len, "files");
            break;
        case FILTER_DIRS:
            snprintf(out, out_len, "dirs");
            break;
        case FILTER_CONTAINS:
            if (list->filter_text[0] == '\0') snprintf(out, out_len, "contains:*");
            else snprintf(out, out_len, "contains:%s", list->filter_text);
            break;
        default:
            snprintf(out, out_len, "all");
            break;
    }
}

void draw_status_bar(FileList *list) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // Draw white horizontal line above status bar
    attron(COLOR_PAIR(4));
    mvhline(max_y - 2, 0, ACS_HLINE, max_x);
    attroff(COLOR_PAIR(4));

    // Clear the status line without filling
    move(max_y - 1, 0);
    clrtoeol();

    attron(COLOR_PAIR(8) | A_BOLD);

    mvprintw(max_y - 1, 1, "NBL GoTo | mode: NORMAL |");
    mvprintw(max_y - 1, 25, " dir:  %s", list->cwd);

    char filt[300];
    filter_label(list, filt, sizeof(filt));

    char status[256];
    snprintf(status, sizeof(status),
             "Hidden:%s  Sort:%s%s  Filter:%s  %d/%d ",
             list->show_hidden ? "ON" : "OFF",
             sort_label(list->sort_mode),
             list->sort_reverse ? " (rev)" : "",
             filt,
             (list->count > 0 ? list->selected + 1 : 0),
             list->count);

    mvprintw(max_y - 1, max_x - (int)strlen(status) - 1, "%s", status);
    attroff(COLOR_PAIR(8) | A_BOLD);
}

void draw_ui(FileList *list) {
    clear();

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int visible_lines = max_y - 3;
    for (int i = 0; i < visible_lines && i + list->scroll_offset < list->count; i++) {
        int idx = i + list->scroll_offset;
        FileItem *item = &list->items[idx];
        int y = i;

        if (idx == list->selected) attron(A_REVERSE | A_BOLD);

        const char *icon = get_file_icon(item);
        int color = get_file_color(item);

        if (idx != list->selected) attron(COLOR_PAIR(color));

        mvprintw(y, 1, "%s  %-*s", icon, max_x - 20, item->name);

        if (!item->is_dir) {
            char size_str[16];
            format_size(item->size, size_str, sizeof(size_str));
            mvprintw(y, max_x - 12, "%10s", size_str);
        }

        if (idx == list->selected) attroff(A_REVERSE | A_BOLD);
        else attroff(COLOR_PAIR(color));
    }

    draw_status_bar(list);
    refresh();
}

static int fuzzy_select_path(FileList *list, char *out, size_t out_len) {
    const char *fz = pick_fuzzy_tool();
    if (!fz) {
        // manual fallback selects within current list (no external find)
        int ok = manual_select_in_list(list);
        if (ok) {
            if (list->selected >= 0 && list->selected < list->count) {
                strncpy(out, list->items[list->selected].name, out_len - 1);
                out[out_len - 1] = '\0';
                return 1;
            }
        }
        return 0;
    }

    // Quote cwd properly
    char qcwd[QUOTE_BUF_SIZE];
    shell_quote_single(qcwd, sizeof(qcwd), list->cwd);

    // nfzf is minimal: no flags. fzf supports the UI flags.
    const int is_nfzf = (strcmp(fz, "ff") == 0);

    char cmd[8192];
    int ret;
    if (is_nfzf) {
        ret = snprintf(cmd, sizeof(cmd),
                 "cd %s && "
                 "find . -maxdepth 5 -mindepth 1 2>/dev/null | "
                 "sed 's#^\\./##' | "
                 "%s",
                 qcwd, fz);
    } else {
        ret = snprintf(cmd, sizeof(cmd),
                 "cd %s && "
                 "find . -maxdepth 5 -mindepth 1 2>/dev/null | "
                 "sed 's#^\\./##' | "
                 "%s --prompt='Search> ' --height=40%% --reverse",
                 qcwd, fz);
    }
    
    if (ret < 0 || ret >= (int)sizeof(cmd)) return -1;

    FILE *p = popen(cmd, "r");
    if (!p) return -1;

    char buf[MAX_PATH] = {0};
    if (!fgets(buf, sizeof(buf), p)) {
        pclose(p);
        return 0;
    }
    pclose(p);

    buf[strcspn(buf, "\r\n")] = '\0';
    if (buf[0] == '\0') return 0;

    strncpy(out, buf, out_len - 1);
    out[out_len - 1] = '\0';
    return 1;
}

static void apply_sort_command(FileList *list, int cmd) {
    switch (cmd) {
        case 'n': list->sort_mode = SORT_NAME; break;
        case 's': list->sort_mode = SORT_SIZE; break;
        case 't': list->sort_mode = SORT_TIME; break;
        case 'e': list->sort_mode = SORT_EXT;  break;
        case 'r': list->sort_reverse = !list->sort_reverse; break;
        default: break;
    }
    load_directory(list, list->cwd);
}

static void apply_filter_command(FileList *list, int cmd) {
    switch (cmd) {
        case 'f':
            list->filter_mode = FILTER_FILES;
            list->filter_text[0] = '\0';
            load_directory(list, list->cwd);
            break;
        case 'd':
            list->filter_mode = FILTER_DIRS;
            list->filter_text[0] = '\0';
            load_directory(list, list->cwd);
            break;
        case 'F':
            list->filter_mode = FILTER_ALL;
            list->filter_text[0] = '\0';
            load_directory(list, list->cwd);
            break;
        case 'c': {
            char s[256];
            if (popup_prompt(s, sizeof(s), "Filter (contains)", "Substring to match (empty clears):")) {
                list->filter_mode = FILTER_CONTAINS;
                strncpy(list->filter_text, s, sizeof(list->filter_text) - 1);
                list->filter_text[sizeof(list->filter_text) - 1] = '\0';
            } else {
                list->filter_mode = FILTER_ALL;
                list->filter_text[0] = '\0';
            }
            load_directory(list, list->cwd);
            break;
        }
        default:
            break;
    }
}

// FIXED: Proper shell quoting that checks bounds before writing
static void shell_quote_single(char *out, size_t out_len, const char *in) {
    if (!out || out_len < 3 || !in) {
        if (out && out_len > 0) out[0] = '\0';
        return;
    }

    size_t j = 0;
    
    // Check space for opening quote
    if (j + 1 >= out_len) {
        out[0] = '\0';
        return;
    }
    out[j++] = '\'';
    
    for (size_t i = 0; in[i] != '\0'; i++) {
        if (in[i] == '\'') {
            // Need 5 chars for '"'"' plus null terminator
            if (j + 6 >= out_len) {
                out[0] = '\0';
                return;
            }
            const char *esc = "'\"'\"'";
            for (int k = 0; esc[k]; k++) {
                out[j++] = esc[k];
            }
        } else {
            // Need 1 char plus room for closing quote and null
            if (j + 3 >= out_len) {
                out[0] = '\0';
                return;
            }
            out[j++] = in[i];
        }
    }
    
    // Add closing quote
    if (j + 2 >= out_len) {
        out[0] = '\0';
        return;
    }
    out[j++] = '\'';
    out[j] = '\0';
}

static int run_viewer_command(const char *cmd) {
    if (!cmd || !*cmd) return -1;
    
    endwin();
    int rc = system(cmd);
    refresh();
    clear();
    return rc;
}

static int in_tmux(void) {
    const char *t = getenv("TMUX");
    return (t && *t);
}

static int tmux_get_current_pane_id(char *out, size_t out_len) {
    FILE *p = popen("tmux display-message -p \"#{pane_id}\" 2>&1", "r");
    if (!p) return -1;

    char buf[128] = {0};
    if (!fgets(buf, sizeof(buf), p)) {
        pclose(p);
        return -1;
    }
    pclose(p);

    buf[strcspn(buf, "\r\n")] = '\0';
    if (buf[0] == '\0') return -1;

    strncpy(out, buf, out_len - 1);
    out[out_len - 1] = '\0';
    return 0;
}

static int tmux_split_left_detached(const char *cwd, const char *filetree_cmd) {
    char qcwd[QUOTE_BUF_SIZE];
    char qscript[QUOTE_BUF_SIZE * 2];
    shell_quote_single(qcwd, sizeof(qcwd), cwd);
    shell_quote_single(qscript, sizeof(qscript), filetree_cmd);
    
    if (qcwd[0] == '\0' || qscript[0] == '\0') return -1;

    char cmd[8192];
    int ret = snprintf(cmd, sizeof(cmd),
             "tmux split-window -d -h -b -p 25 -c %s sh -lc %s",
             qcwd, qscript);
    
    if (ret < 0 || ret >= (int)sizeof(cmd)) return -1;

    return system(cmd);
}

static void tmux_stop_left_of_pane(const char *editor_pane_id, int remove_pane) {
    char qid[256];
    shell_quote_single(qid, sizeof(qid), editor_pane_id);
    
    if (qid[0] == '\0') return;

    char cmd[2048];
    int ret;

    if (remove_pane) {
        ret = snprintf(cmd, sizeof(cmd),
                 "tmux select-pane -t %s \\; "
                 "send-keys -t '{left-of}' C-c \\; "
                 "kill-pane -t '{left-of}'",
                 qid);
    } else {
        ret = snprintf(cmd, sizeof(cmd),
                 "tmux select-pane -t %s \\; "
                 "send-keys -t '{left-of}' C-c",
                 qid);
    }
    
    if (ret < 0 || ret >= (int)sizeof(cmd)) return;

    system(cmd);
}

static int tmux_toggle_terminal(const char *cwd) {
    if (!in_tmux()) {
        popup_message("Not in tmux", "Terminal toggle only works inside tmux");
        return -1;
    }

    // Check if terminal pane already exists
    if (g_terminal_pane_id[0] != '\0') {
        // Check if the pane is still alive
        char check_cmd[512];
        char qid[256];
        shell_quote_single(qid, sizeof(qid), g_terminal_pane_id);
        
        if (qid[0] == '\0') {
            g_terminal_pane_id[0] = '\0';
            return -1;
        }
        
        int ret = snprintf(check_cmd, sizeof(check_cmd),
                 "tmux display-message -p -t %s '#{pane_id}' 2>/dev/null", qid);
        
        if (ret < 0 || ret >= (int)sizeof(check_cmd)) {
            g_terminal_pane_id[0] = '\0';
            return -1;
        }

        FILE *p = popen(check_cmd, "r");
        if (p) {
            char buf[128] = {0};
            int alive = (fgets(buf, sizeof(buf), p) != NULL);
            pclose(p);

            if (alive) {
                // Pane exists, kill it
                char kill_cmd[512];
                ret = snprintf(kill_cmd, sizeof(kill_cmd), "tmux kill-pane -t %s", qid);
                if (ret > 0 && ret < (int)sizeof(kill_cmd)) {
                    system(kill_cmd);
                }
                g_terminal_pane_id[0] = '\0';
                return 0;
            }
        }
        // Pane doesn't exist anymore, reset
        g_terminal_pane_id[0] = '\0';
    }

    // Create new terminal pane at bottom
    char qcwd[QUOTE_BUF_SIZE];
    shell_quote_single(qcwd, sizeof(qcwd), cwd);
    
    if (qcwd[0] == '\0') return -1;

    char cmd[8192];
    int ret = snprintf(cmd, sizeof(cmd),
             "tmux split-window -v -p 30 -c %s", qcwd);
    
    if (ret < 0 || ret >= (int)sizeof(cmd)) return -1;

    if (system(cmd) != 0) {
        popup_message("Error", "Failed to create terminal pane");
        return -1;
    }

    // Get the pane ID of the newly created pane
    FILE *p = popen("tmux display-message -p '#{pane_id}'", "r");
    if (p) {
        char buf[128] = {0};
        if (fgets(buf, sizeof(buf), p)) {
            buf[strcspn(buf, "\r\n")] = '\0';
            strncpy(g_terminal_pane_id, buf, sizeof(g_terminal_pane_id) - 1);
            g_terminal_pane_id[sizeof(g_terminal_pane_id) - 1] = '\0';
        }
        pclose(p);
    }

    // Switch focus back to the file manager pane
    system("tmux last-pane");

    return 0;
}

// Validate editor command to prevent shell injection
static int validate_editor(const char *editor) {
    if (!editor || !*editor) return 0;
    
    // Allow alphanumeric, dash, underscore, slash, and dot
    for (const char *p = editor; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_' && *p != '/' && *p != '.') {
            return 0;
        }
    }
    return 1;
}

static int open_selected_with_tmux_tree(FileList *list,
                                       const char *filetree_cmd,
                                       const char *editor_env,
                                       const char *editor_fallback) {
    if (list->selected >= list->count) return -1;
    FileItem *item = &list->items[list->selected];

    if (item->is_dir) {
        popup_message("Nope", "That is a directory. Use 'l' to enter it.");
        return 0;
    }
    if (strcmp(item->name, ".") == 0 || strcmp(item->name, "..") == 0) {
        popup_message("Nope", "Refusing to open '.' or '..'.");
        return 0;
    }

    const char *editor = getenv(editor_env);
    if (!editor || !*editor) editor = editor_fallback;
    
    // Validate editor
    if (!validate_editor(editor)) {
        popup_message("Error", "Invalid editor command");
        return -1;
    }

    char qpath[QUOTE_BUF_SIZE];
    shell_quote_single(qpath, sizeof(qpath), item->full_path);
    
    if (qpath[0] == '\0') return -1;

    if (!in_tmux()) {
        char cmd[8192];
        int ret = snprintf(cmd, sizeof(cmd), "%s %s", editor, qpath);
        if (ret < 0 || ret >= (int)sizeof(cmd)) return -1;
        return run_viewer_command(cmd);
    }

    char editor_pane_id[128] = {0};

    endwin();

    if (tmux_get_current_pane_id(editor_pane_id, sizeof(editor_pane_id)) != 0) {
        char cmd[8192];
        int ret = snprintf(cmd, sizeof(cmd), "%s %s", editor, qpath);
        if (ret < 0 || ret >= (int)sizeof(cmd)) return -1;
        int rc = system(cmd);
        refresh();
        clear();
        return rc;
    }

    tmux_split_left_detached(list->cwd, filetree_cmd);

    char editcmd[8192];
    int ret = snprintf(editcmd, sizeof(editcmd), "%s %s", editor, qpath);
    if (ret < 0 || ret >= (int)sizeof(editcmd)) {
        refresh();
        clear();
        return -1;
    }
    
    int rc = system(editcmd);

    tmux_stop_left_of_pane(editor_pane_id, 1);

    refresh();
    clear();
    return rc;
}

static int open_selected_with(FileList *list, const char *envvar, const char *fallback_cmd) {
    if (list->selected >= list->count) return -1;

    FileItem *item = &list->items[list->selected];

    if (item->is_dir) {
        popup_message("Nope", "That is a directory. Use 'l' to enter it.");
        return 0;
    }
    if (strcmp(item->name, ".") == 0 || strcmp(item->name, "..") == 0) {
        popup_message("Nope", "Refusing to open '.' or '.''.");
        return 0;
    }

    const char *tool = getenv(envvar);
    if (!tool || !*tool) tool = fallback_cmd;
    
    // Validate tool
    if (!validate_editor(tool)) {
        popup_message("Error", "Invalid tool command");
        return -1;
    }

    char qpath[QUOTE_BUF_SIZE];
    shell_quote_single(qpath, sizeof(qpath), item->full_path);
    
    if (qpath[0] == '\0') return -1;

    char cmd[8192];
    int ret = snprintf(cmd, sizeof(cmd), "%s %s", tool, qpath);
    if (ret < 0 || ret >= (int)sizeof(cmd)) return -1;

    int rc = run_viewer_command(cmd);
    if (rc != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Command failed (%d). Tried: %s", rc, cmd);
        popup_message("Error", msg);
    }
    return 0;
}

static int open_with_right_split(FileList *list, const char *envvar, const char *fallback_cmd) {
    if (list->selected >= list->count) return -1;

    FileItem *item = &list->items[list->selected];

    if (item->is_dir) {
        popup_message("Nope", "That is a directory. Use 'l' to enter it.");
        return 0;
    }
    if (strcmp(item->name, ".") == 0 || strcmp(item->name, "..") == 0) {
        popup_message("Nope", "Refusing to open '.' or '.''.");
        return 0;
    }

    if (!in_tmux()) {
        // Fallback to regular opening if not in tmux
        return open_selected_with(list, envvar, fallback_cmd);
    }

    const char *tool = getenv(envvar);
    if (!tool || !*tool) tool = fallback_cmd;
    
    // Validate tool
    if (!validate_editor(tool)) {
        popup_message("Error", "Invalid tool command");
        return -1;
    }

    char qpath[QUOTE_BUF_SIZE];
    char qcwd[QUOTE_BUF_SIZE];
    shell_quote_single(qpath, sizeof(qpath), item->full_path);
    shell_quote_single(qcwd, sizeof(qcwd), list->cwd);
    
    if (qpath[0] == '\0' || qcwd[0] == '\0') return -1;

    // Create split on the right at 80% width and run tool in a shell wrapper
    char cmd[8192];
    int ret = snprintf(cmd, sizeof(cmd),
             "tmux split-window -h -p 80 -c %s 'sh -lc \"%s %s; tmux kill-pane\"'",
             qcwd, tool, qpath);
    
    if (ret < 0 || ret >= (int)sizeof(cmd)) return -1;
    
    endwin();
    system(cmd);
    refresh();
    clear();

    return 0;
}

static void cmd_show_help(void) {
    const char *fz = pick_fuzzy_tool();
    if (!fz) {
        popup_message("No fuzzy finder", "Install ff or fzf for the help menu.");
        return;
    }
    
    char help_template[] = "/tmp/goto_help_XXXXXX";
    int fd = mkstemp(help_template);
    if (fd < 0) {
        popup_message("Error", "Failed to create temp file");
        return;
    }
    register_temp_file(help_template);

    FILE *help_file = fdopen(fd, "w");
    if (!help_file) {
        close(fd);
        unlink(help_template);
        popup_message("Error", "Failed to open temp file");
        return;
    }

    // Write help content organized by category
    fprintf(help_file, "=== NAVIGATION ===\n");
    fprintf(help_file, "j / DOWN        | Move down\n");
    fprintf(help_file, "k / UP          | Move up\n");
    fprintf(help_file, "l / ENTER       | Enter directory / open file\n");
    fprintf(help_file, "bs / BACKSPACE  | Go to parent directory\n");
    fprintf(help_file, "g               | Jump to top\n");
    fprintf(help_file, "G               | Jump to bottom\n");
    fprintf(help_file, "/               | Fuzzy search files (fzf)\n");
    fprintf(help_file, "?               | Grep search in selected file (ff + nl)\n");
    fprintf(help_file, "o               | Set current dir and quit (for shell integration)\n");
    fprintf(help_file, "\n");

    fprintf(help_file, "=== FILE OPERATIONS ===\n");
    fprintf(help_file, "n               | Create new file\n");
    fprintf(help_file, "N               | Create new directory\n");
    fprintf(help_file, "r               | Rename selected item\n");
    fprintf(help_file, "d               | Delete selected item\n");
    fprintf(help_file, "\n");

    fprintf(help_file, "=== OPEN WITH ===\n");
    fprintf(help_file, "e               | Open with $EDITOR in right split (default: vi)\n");
    fprintf(help_file, "v               | Open with $vic in tmux split with file tree\n");
    fprintf(help_file, "p               | Open with $PAGER in right split (default: less -R)\n");
    fprintf(help_file, "t               | Toggle terminal pane (tmux only)\n");
    fprintf(help_file, "\n");

    fprintf(help_file, "=== SORT ===\n");
    fprintf(help_file, "sn              | Sort by name\n");
    fprintf(help_file, "ss              | Sort by size\n");
    fprintf(help_file, "st              | Sort by time\n");
    fprintf(help_file, "se              | Sort by extension\n");
    fprintf(help_file, "sr              | Reverse sort order\n");
    fprintf(help_file, "\n");

    fprintf(help_file, "=== FILTER ===\n");
    fprintf(help_file, "ff              | Filter: files only\n");
    fprintf(help_file, "fd              | Filter: directories only\n");
    fprintf(help_file, "fF              | Filter: show all (clear filter)\n");
    fprintf(help_file, "fc              | Filter: contains substring (prompt)\n");
    fprintf(help_file, "\n");

    fprintf(help_file, "=== SETTINGS ===\n");
    fprintf(help_file, "h               | Toggle hidden files\n");
    fprintf(help_file, "\n");

    fprintf(help_file, "=== OTHER ===\n");
    fprintf(help_file, "q               | Quit\n");
    fprintf(help_file, "H               | Show this help\n");

    fflush(help_file);
    fclose(help_file);

    char cmd[8192];
    int ret = snprintf(cmd, sizeof(cmd),
        "fzf --height=100%% --layout=reverse --border "
        "--header='GoTo Help - Search commands (ESC to close)' "
        "< \"%s\" > /dev/null 2> /dev/tty",
        help_template);
    
    if (ret < 0 || ret >= (int)sizeof(cmd)) {
        unlink(help_template);
        return;
    }

    endwin();
    system(cmd);
    refresh();
    clear();

    unlink(help_template);
}

void handle_input(FileList *list, int *running) {
    int ch = getch();
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int visible_lines = max_y - 3;

    if (list->pending_prefix) {
        char prefix = list->pending_prefix;
        list->pending_prefix = 0;

        if (prefix == 's') { apply_sort_command(list, ch); return; }
        if (prefix == 'f') { apply_filter_command(list, ch); return; }
    }

    switch (ch) {
        case 'q':
        case 'Q':
            *running = 0;
            break;

        case '?': {
            int line = 0;
            int ok = ff_grep_selected_file(list, &line);

            if (ok > 0) {
                const char *editor = getenv("EDITOR");
                if (!editor || !*editor) editor = "vi";
                
                if (!validate_editor(editor)) {
                    popup_message("Error", "Invalid EDITOR environment variable");
                    break;
                }

                FileItem *it = &list->items[list->selected];

                char qpath[QUOTE_BUF_SIZE];
                shell_quote_single(qpath, sizeof(qpath), it->full_path);
                
                if (qpath[0] == '\0') break;

                char ecmd[8192];
                int ret = snprintf(ecmd, sizeof(ecmd), "%s +%d %s", editor, line, qpath);
                if (ret < 0 || ret >= (int)sizeof(ecmd)) break;

                run_viewer_command(ecmd);
                load_directory(list, list->cwd);
            }
            break;
        }

        case 't':
        case 'T': {
            endwin();
            tmux_toggle_terminal(list->cwd);
            refresh();
            clear();
            break;
        }

        case 'e':
        case 'E': {
            FILE *ftout = fopen("/tmp/.goto_path", "w");
            if (ftout) { fprintf(ftout, "%s", list->cwd); fclose(ftout); }
            open_with_right_split(list, "EDITOR", "vi");
            break;
        }

        case 'v':
        case 'V': {
            FILE *fbout = fopen("/tmp/.goto_path", "w");
            if (fbout) { fprintf(fbout, "%s", list->cwd); fclose(fbout); }

            const char *filetree_cmd = "lsx -R | fzf --ansi --reverse --bind 'ctrl-r:reload(lsx -R)'";

            open_selected_with_tmux_tree(list, filetree_cmd, "vic", "vic");
            break;
        }

        case 'p':
        case 'P': {
            FILE *fbout = fopen("/tmp/.goto_path", "w");
            if (fbout) { fprintf(fbout, "%s", list->cwd); fclose(fbout); }
            open_with_right_split(list, "PAGER", "less -R");
            break;
        }

        case 'h':
            list->show_hidden = !list->show_hidden;
            load_directory(list, list->cwd);
            break;
            
        case 'H':
            cmd_show_help();
            break;

        case 's':
            list->pending_prefix = 's';
            break;

        case 'f':
            list->pending_prefix = 'f';
            break;

        case 'n': {
            char name[256];
            if (popup_prompt(name, sizeof(name), "New File", "Enter filename:")) {
                if (create_new_file(list->cwd, name) != 0) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Failed to create file: %s", strerror(errno));
                    popup_message("Error", msg);
                }
                load_directory(list, list->cwd);
            }
            break;
        }

        case 'N': {
            char name[256];
            if (popup_prompt(name, sizeof(name), "New Directory", "Enter directory name:")) {
                if (create_new_dir(list->cwd, name) != 0) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Failed to create directory: %s", strerror(errno));
                    popup_message("Error", msg);
                }
                load_directory(list, list->cwd);
            }
            break;
        }

        case 'r':
        case 'R': {
            if (list->selected < list->count) {
                FileItem *item = &list->items[list->selected];

                if (strcmp(item->name, ".") == 0 || strcmp(item->name, "..") == 0) {
                    popup_message("Nope", "Refusing to rename '.' or '..'.");
                    break;
                }

                char newname[256];
                char label[512];
                snprintf(label, sizeof(label), "Rename '%s' to:", item->name);

                if (popup_prompt(newname, sizeof(newname), "Rename", label)) {
                    if (rename_item(item, newname) != 0) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Rename failed: %s", strerror(errno));
                        popup_message("Error", msg);
                    }
                    load_directory(list, list->cwd);
                }
            }
            break;
        }

        case 'd':
        case 'D': {
            if (list->selected < list->count) {
                FileItem *item = &list->items[list->selected];

                if (strcmp(item->name, ".") == 0 || strcmp(item->name, "..") == 0) {
                    popup_message("Nope", "Refusing to delete '.' or '.''.");
                    break;
                }

                char prompt[512];
                snprintf(prompt, sizeof(prompt), "Delete '%s'? This cannot be undone.", item->name);

                if (popup_confirm("Confirm Delete", prompt)) {
                    if (delete_item_shallow(item) != 0) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Delete failed: %s", strerror(errno));
                        popup_message("Error", msg);
                    }
                    load_directory(list, list->cwd);
                    if (list->count == 0) list->selected = 0;
                    else if (list->selected >= list->count) list->selected = list->count - 1;
                }
            }
            break;
        }

        case '/': {
            char rel[MAX_PATH];

            endwin();
            int ok = fuzzy_select_path(list, rel, sizeof(rel));
            refresh();
            clear();

            if (ok > 0) {
                char full[MAX_PATH];
                int ret = snprintf(full, sizeof(full), "%s/%s", list->cwd, rel);
                if (ret < 0 || ret >= (int)sizeof(full)) break;

                struct stat st;
                if (lstat(full, &st) == 0) {
                    if (S_ISDIR(st.st_mode)) {
                        load_directory(list, full);
                    } else {
                        load_directory(list, list->cwd);
                        for (int i = 0; i < list->count; i++) {
                            if (strcmp(list->items[i].name, rel) == 0) {
                                list->selected = i;
                                int visible = max_y - 3;
                                if (list->selected < list->scroll_offset) list->scroll_offset = list->selected;
                                if (list->selected >= list->scroll_offset + visible)
                                    list->scroll_offset = list->selected - visible + 1;
                                break;
                            }
                        }
                    }
                }
            }
            break;
        }

        case 'o':
        case 'O': {
            FILE *fout = fopen("/tmp/.goto_path", "w");
            if (fout) { fprintf(fout, "%s", list->cwd); fclose(fout); }
            *running = 0;
            break;
        }

        case 'j':
        case KEY_DOWN:
            if (list->selected < list->count - 1) {
                list->selected++;
                if (list->selected >= list->scroll_offset + visible_lines) list->scroll_offset++;
            }
            break;

        case 'k':
        case KEY_UP:
            if (list->selected > 0) {
                list->selected--;
                if (list->selected < list->scroll_offset) list->scroll_offset--;
            }
            break;

        case 'g':
            list->selected = 0;
            list->scroll_offset = 0;
            break;

        case 'G':
            if (list->count > 0) {
                list->selected = list->count - 1;
                list->scroll_offset = list->count - visible_lines;
                if (list->scroll_offset < 0) list->scroll_offset = 0;
            }
            break;

        case '\n':
        case KEY_ENTER:
        case 'l':
            if (list->selected < list->count) {
                FileItem *item = &list->items[list->selected];
                if (item->is_dir) load_directory(list, item->full_path);
            }
            break;

        case KEY_BACKSPACE:
        case 127: {
            char parent[MAX_PATH];
            strncpy(parent, list->cwd, MAX_PATH - 1);
            parent[MAX_PATH - 1] = '\0';

            char *last_slash = strrchr(parent, '/');
            if (last_slash && last_slash != parent) {
                *last_slash = '\0';
                load_directory(list, parent);
            } else if (last_slash == parent) {
                load_directory(list, "/");
            }
            break;
        }

        default:
            break;
    }
}

static void expand_tilde(char *out, size_t out_len, const char *in) {
    if (!out || out_len == 0 || !in) return;

    if (in[0] == '~' && (in[1] == '\0' || in[1] == '/')) {
        const char *home = getenv("HOME");
        if (!home) home = "";

        snprintf(out, out_len, "%s%s", home, in + 1);
    } else {
        snprintf(out, out_len, "%s", in);
    }
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    
    // Register signal handlers early
    register_signal_handlers();

    FileList list = (FileList){0};
    char start[MAX_PATH];

    list.show_hidden = 0;
    list.sort_mode = SORT_NAME;
    list.sort_reverse = 0;
    list.filter_mode = FILTER_ALL;
    list.filter_text[0] = '\0';
    list.pending_prefix = 0;

    /* 1) Always initialize start to PWD first */
    if (!getcwd(start, sizeof(start))) {
        perror("getcwd");
        return 1;
    }

    /* 2) If GOTO_START is set, try it (but don't break if it fails) */
    const char *env_start = getenv("GOTO_START");
    if (env_start && *env_start) {
        char tmp[MAX_PATH];
        expand_tilde(tmp, sizeof(tmp), env_start);

        char resolved[MAX_PATH];
        if (realpath(tmp, resolved)) {
            strncpy(start, resolved, sizeof(start) - 1);
            start[sizeof(start) - 1] = '\0';
        } else {
            strncpy(start, tmp, sizeof(start) - 1);
            start[sizeof(start) - 1] = '\0';
        }
    }

    /* 3) CLI path overrides env var (relative allowed) */
    if (argc > 1 && argv[1] && argv[1][0]) {
        char tmp[MAX_PATH];
        expand_tilde(tmp, sizeof(tmp), argv[1]);

        char resolved[MAX_PATH];
        if (realpath(tmp, resolved)) {
            strncpy(start, resolved, sizeof(start) - 1);
            start[sizeof(start) - 1] = '\0';
        } else {
            strncpy(start, tmp, sizeof(start) - 1);
            start[sizeof(start) - 1] = '\0';
        }
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_GREEN, -1);
        init_pair(3, COLOR_MAGENTA, -1);
        init_pair(4, COLOR_WHITE, -1);
        init_pair(5, COLOR_BLUE, -1);
        init_pair(6, COLOR_YELLOW, -1);
        init_pair(7, COLOR_RED, -1);
        init_pair(8, COLOR_WHITE, -1);
    }

    if (load_directory(&list, start) != 0) {
        endwin();
        fprintf(stderr, "Failed to load directory: %s\n", start);
        perror("load_directory");
        return 1;
    }

    int running = 1;
    while (running) {
        draw_ui(&list);
        handle_input(&list, &running);
    }

    endwin();
    
    // Cleanup temp files on normal exit
    for (int i = 0; i < g_temp_file_count; i++) {
        if (g_temp_files[i][0] != '\0') {
            unlink(g_temp_files[i]);
        }
    }
    
    return 0;
}