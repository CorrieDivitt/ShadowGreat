/* ---- Shadowgreat patch (patches/menu-folders): folders open in place ----
 * A folder row inside a page no longer opens a page of its own: choosing it
 * shows or hides its rows right below it, indented. Which folders are open
 * is remembered while the game runs; every folder starts closed. */
#define SG_OPEN_FOLDERS 16
static char sg_open_folders[SG_OPEN_FOLDERS][256];

static void sg_folder_path(const char *page, const char *name, size_t len,
                           char *out, size_t size) {
    snprintf(out, size, "%s/%.*s", page, (int)len, name);
}

static int sg_folder_slot(const char *path) {
    for (int i = 0; i < SG_OPEN_FOLDERS; ++i)
        if (sg_open_folders[i][0] && strcmp(sg_open_folders[i], path) == 0) return i;
    return -1;
}

static int sg_folder_is_open(const char *page, const char *name, size_t len) {
    char path[256];
    sg_folder_path(page, name, len, path, sizeof(path));
    return sg_folder_slot(path) >= 0;
}

static void sg_folder_toggle(const char *page, const char *name, size_t len) {
    char path[256];
    sg_folder_path(page, name, len, path, sizeof(path));
    const int at = sg_folder_slot(path);
    if (at >= 0) { sg_open_folders[at][0] = '\0'; return; }
    for (int i = 0; i < SG_OPEN_FOLDERS; ++i)
        if (!sg_open_folders[i][0]) {
            snprintf(sg_open_folders[i], sizeof(sg_open_folders[i]), "%s", path);
            return;
        }
}

