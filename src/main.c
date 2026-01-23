#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <locale.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>

#define MAX_PATH 4096
#define MAX_ITEMS 1024

// NerdFont Icons (UTF-8)
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

    // NEW: sort + filter state
    SortMode sort_mode;
    int sort_reverse;

    FilterMode filter_mode;
    char filter_text[256]; // used for FILTER_CONTAINS

    // NEW: prefix input state for 2-key commands
    char pending_prefix;   // 0, 's', or 'f'
} FileList;

/* ---------- Popup helpers ---------- */

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

    // trim whitespace
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

/* ---------- File ops ---------- */

static int create_new_file(const char *cwd, const char *name) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", cwd, name);

    struct stat st;
    if (lstat(path, &st) == 0) { errno = EEXIST; return -1; }

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fclose(f);
    return 0;
}

static int create_new_dir(const char *cwd, const char *name) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", cwd, name);

    struct stat st;
    if (lstat(path, &st) == 0) { errno = EEXIST; return -1; }

    return mkdir(path, 0755);
}

static int delete_item_shallow(const FileItem *item) {
    if (item->is_dir) return rmdir(item->full_path);  // fails if not empty
    return unlink(item->full_path);
}

static int rename_item(const FileItem *item, const char *new_name) {
    char dirbuf[MAX_PATH];
    strncpy(dirbuf, item->full_path, sizeof(dirbuf) - 1);
    dirbuf[sizeof(dirbuf) - 1] = '\0';

    char *slash = strrchr(dirbuf, '/');
    if (!slash) { errno = EINVAL; return -1; }
    *slash = '\0';

    char new_path[MAX_PATH];
    snprintf(new_path, sizeof(new_path), "%s/%s", dirbuf, new_name);

    struct stat st;
    if (lstat(new_path, &st) == 0) { errno = EEXIST; return -1; }

    return rename(item->full_path, new_path);
}

/* ---------- Icons / Colors ---------- */

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
    if (item->is_dir) return 1; // Cyan
    if (item->mode & S_IXUSR) return 2; // Green

    const char *ext = strrchr(item->name, '.');
    if (!ext) return 4;

    if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) return 5;
    if (strcmp(ext, ".py") == 0) return 6;
    if (strcmp(ext, ".js") == 0 || strcmp(ext, ".ts") == 0) return 2;
    if (strcmp(ext, ".rs") == 0) return 7;
    if (strcmp(ext, ".md") == 0) return 6;

    return 4;
}

/* ---------- Sorting helpers ---------- */

static const char* file_ext(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext || ext == name) return "";
    return ext + 1; // without dot
}

static int compare_items(const void *a, const void *b, void *ctx) {
    const FileList *list = (const FileList*)ctx;
    const FileItem *A = (const FileItem*)a;
    const FileItem *B = (const FileItem*)b;

    // Dirs first (common file manager behavior)
    if (A->is_dir != B->is_dir) {
        int r = (B->is_dir - A->is_dir); // A dir => negative => comes first
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

static void sort_items(FileList *list) {
#if defined(__APPLE__) || defined(__FreeBSD__)
    // no qsort_r portable signature across platforms; we’ll do a simple global ctx workaround:
    // but easiest: use a static pointer for comparator.
    // We'll implement below in portable way using a static.
#else
#endif
}

// Portable qsort with static context
static FileList *g_sort_ctx = NULL;
static int compare_items_static(const void *a, const void *b) {
    return compare_items(a, b, g_sort_ctx);
}
static void sort_items_portable(FileList *list) {
    g_sort_ctx = list;
    qsort(list->items, list->count, sizeof(FileItem), compare_items_static);
    g_sort_ctx = NULL;
}

/* ---------- Filtering ---------- */

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

/* ---------- Directory loading ---------- */

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

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && list->count < MAX_ITEMS) {
        int is_hidden = (entry->d_name[0] == '.');
        if (is_hidden && !list->show_hidden) continue;

        FileItem tmp = {0};
        strncpy(tmp.name, entry->d_name, 255);
        tmp.name[255] = '\0';

        snprintf(tmp.full_path, MAX_PATH, "%s/%s", list->cwd, entry->d_name);

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

        // Apply filter
        if (!passes_filter(list, &tmp)) continue;

        list->items[list->count++] = tmp;
    }

    closedir(dir);

    // Apply sorting
    sort_items_portable(list);

    return 0;
}

void format_size(off_t size, char *buf, size_t len) {
    if (size < 1024) snprintf(buf, len, "%lldB", (long long)size);
    else if (size < 1024 * 1024) snprintf(buf, len, "%.1fK", size / 1024.0);
    else if (size < 1024 * 1024 * 1024) snprintf(buf, len, "%.1fM", size / (1024.0 * 1024.0));
    else snprintf(buf, len, "%.1fG", size / (1024.0 * 1024.0 * 1024.0));
}

/* ---------- UI ---------- */

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

    attron(COLOR_PAIR(8) | A_BOLD);
    mvhline(max_y - 2, 0, ' ', max_x);

    mvprintw(max_y - 2, 1, "NORMAL");
    mvprintw(max_y - 2, 10, "  %s", list->cwd);

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

    mvprintw(max_y - 2, max_x - (int)strlen(status) - 1, "%s", status);
    attroff(COLOR_PAIR(8) | A_BOLD);
}

void draw_help_line(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    attron(COLOR_PAIR(4));
    mvhline(max_y - 1, 0, ' ', max_x);
    mvprintw(max_y - 1, 1,
             "j/k:move l:open bs:up h:hidden n:new N:mkdir r:rename d:del /:fzf "
             "e:edit v: vi p:page s?:sort(sn/ss/st/se/sr) f?:filter(ff/fd/fF/fc) o:cd q:quit");
    attroff(COLOR_PAIR(4));
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
    draw_help_line();
    refresh();
}

/* ---------- fzf ---------- */

static int fzf_select_path(FileList *list, char *out, size_t out_len) {
    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
             "cd '%s' && "
             "find . -maxdepth 5 -mindepth 1 2>/dev/null | "
             "sed 's#^\\./##' | "
             "fzf --prompt='Search> ' --height=40%% --reverse",
             list->cwd);

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

/* ---------- Command handling for s? and f? ---------- */

static void apply_sort_command(FileList *list, int cmd) {
    switch (cmd) {
        case 'n': list->sort_mode = SORT_NAME; break;   // sn
        case 's': list->sort_mode = SORT_SIZE; break;   // ss
        case 't': list->sort_mode = SORT_TIME; break;   // st
        case 'e': list->sort_mode = SORT_EXT;  break;   // se
        case 'r': list->sort_reverse = !list->sort_reverse; break; // sr
        default: break;
    }
    load_directory(list, list->cwd);
}

static void apply_filter_command(FileList *list, int cmd) {
    switch (cmd) {
        case 'f': // ff => files only
            list->filter_mode = FILTER_FILES;
            list->filter_text[0] = '\0';
            load_directory(list, list->cwd);
            break;
        case 'd': // fd => dirs only
            list->filter_mode = FILTER_DIRS;
            list->filter_text[0] = '\0';
            load_directory(list, list->cwd);
            break;
        case 'F': // fF => clear filter
            list->filter_mode = FILTER_ALL;
            list->filter_text[0] = '\0';
            load_directory(list, list->cwd);
            break;
        case 'c': { // fc => contains substring
            char s[256];
            if (popup_prompt(s, sizeof(s), "Filter (contains)", "Substring to match (empty clears):")) {
                list->filter_mode = FILTER_CONTAINS;
                strncpy(list->filter_text, s, sizeof(list->filter_text) - 1);
                list->filter_text[sizeof(list->filter_text) - 1] = '\0';
            } else {
                // empty/cancel => clear contains
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

/* ---------- Input ---------- */


static void shell_quote_single(char *out, size_t out_len, const char *in) {
    // Produces a single-quoted shell string, escaping embedded single quotes safely.
    // Example: abc'def -> 'abc'"'"'def'
    size_t j = 0;
    if (out_len == 0) return;

    out[j++] = '\'';
    for (size_t i = 0; in[i] != '\0' && j + 6 < out_len; i++) {
        if (in[i] == '\'') {
            // close ', add " ' ", reopen '
            const char *esc = "'\"'\"'";
            for (int k = 0; esc[k] && j + 1 < out_len; k++) out[j++] = esc[k];
        } else {
            out[j++] = in[i];
        }
    }
    if (j + 1 < out_len) out[j++] = '\'';
    out[j] = '\0';
}

static int run_viewer_command(const char *cmd) {
    // Leave curses, run command, then resume curses.
    endwin();
    int rc = system(cmd);
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
        popup_message("Nope", "Refusing to open '.' or '..'.");
        return 0;
    }

    const char *tool = getenv(envvar);
    if (!tool || !*tool) tool = fallback_cmd;

    char qpath[MAX_PATH * 2];
    shell_quote_single(qpath, sizeof(qpath), item->full_path);

    char cmd[8192];
    // Allow envvar to contain args (e.g., "nvim -p", "bat -p", "less -R").
    snprintf(cmd, sizeof(cmd), "%s %s", tool, qpath);

    int rc = run_viewer_command(cmd);
    if (rc != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Command failed (%d). Tried: %s", rc, cmd);
        popup_message("Error", msg);
    }
    return 0;
}
void handle_input(FileList *list, int *running) {
    int ch = getch();
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int visible_lines = max_y - 3;

    // If we’re waiting for a second key for s? or f?
    if (list->pending_prefix) {
        char prefix = list->pending_prefix;
        list->pending_prefix = 0;

        if (prefix == 's') {
            apply_sort_command(list, ch);
            return;
        }
        if (prefix == 'f') {
            apply_filter_command(list, ch);
            return;
        }
    }

    switch (ch) {
        case 'q':
        case 'Q':
            *running = 0;
            break;
                case 'e':
        case 'E':
            // Open highlighted file in $EDITOR (fallback: vi)
            open_selected_with(list, "EDITOR", "vi");
            break;
        case 'v':
        case 'V':
            open_selected_with(list,"vi","vi");
            break;
        case 'p':
        case 'P':
            // Open highlighted file in $PAGER (fallback: less -R)
            open_selected_with(list, "PAGER", "less -R");
            break;

        case 'h':
        case 'H':
            list->show_hidden = !list->show_hidden;
            load_directory(list, list->cwd);
            break;

        case 's': // begin sort prefix
            list->pending_prefix = 's';
            break;

        case 'f': // begin filter prefix
            list->pending_prefix = 'f';
            break;

        case 'n': { // new file
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

        case 'N': { // new dir
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
        case 'R': { // rename
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
        case 'D': { // delete
            if (list->selected < list->count) {
                FileItem *item = &list->items[list->selected];

                if (strcmp(item->name, ".") == 0 || strcmp(item->name, "..") == 0) {
                    popup_message("Nope", "Refusing to delete '.' or '..'.");
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

        case '/': { // fzf
            char rel[MAX_PATH];

            endwin();
            int ok = fzf_select_path(list, rel, sizeof(rel));
            refresh();
            clear();

            if (ok > 0) {
                char full[MAX_PATH];
                snprintf(full, sizeof(full), "%s/%s", list->cwd, rel);

                struct stat st;
                if (lstat(full, &st) == 0) {
                    if (S_ISDIR(st.st_mode)) {
                        load_directory(list, full);
                    } else {
                        load_directory(list, list->cwd);
                        // try highlight exact match in current dir listing
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
            if (fout) {
                fprintf(fout, "%s", list->cwd);
                fclose(fout);
            }
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

/* ---------- Main ---------- */

int main(void) {
    setlocale(LC_ALL, "");
    FileList list = {0};
    char cwd[MAX_PATH];

    list.show_hidden = 0;

    // defaults
    list.sort_mode = SORT_NAME;
    list.sort_reverse = 0;
    list.filter_mode = FILTER_ALL;
    list.filter_text[0] = '\0';
    list.pending_prefix = 0;

    if (!getcwd(cwd, sizeof(cwd))) {
        perror("getcwd");
        return 1;
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
        init_pair(8, COLOR_BLACK, COLOR_CYAN);
    }

    if (load_directory(&list, cwd) != 0) {
        endwin();
        fprintf(stderr, "Failed to load directory\n");
        return 1;
    }

    int running = 1;
    while (running) {
        draw_ui(&list);
        handle_input(&list, &running);
    }

    endwin();
    return 0;
}
