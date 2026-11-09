        ++row;
        /* Shadowgreat patch: an open folder's own rows follow its row. */
        if (rel == 2 && sg_folder_is_open(page, rest, folder_len)) {
            char sub[256];
            sg_folder_path(page, rest, folder_len, sub, sizeof(sub));
            for (size_t j = 0; j < ui->config.item_count; ++j) {
                const char *inner = NULL;
                if (path_relation(item_path(&ui->config.items[j]), sub, &inner) != 1)
                    continue;
                if (row == want) {
                    if (item_out) *item_out = &ui->config.items[j];
                    if (folder_out) *folder_out = NULL;
                    if (folder_len_out) *folder_len_out = 0;
                    return row;
                }
                ++row;
            }
        }
    }
    return row;
}

/* Shadowgreat patch: for the renderer. 1 if `row` of the open page is a
 * folder row that is open, 0 otherwise. */
int recomp_runtime_ui_sg_folder_open(const RecompRuntimeUi *ui, size_t row) {
    const RecompRuntimeUiItem *item = NULL;
    const char *name = NULL;
    size_t len = 0;
    char page[256];
    if (!ui || ui->section_index >= ui->section_count) return 0;
    if (row >= page_walk(ui, (size_t)-1, NULL, NULL, NULL)) return 0;
    page_walk(ui, row, &item, &name, &len);
    if (item || !name || !len) return 0;
    open_page_path(ui, page, sizeof(page));
    return sg_folder_is_open(page, name, len);
}

/* Shadowgreat patch: 1 if `row` is an item shown inside an open folder. */
int recomp_runtime_ui_sg_row_nested(const RecompRuntimeUi *ui, size_t row) {
    const RecompRuntimeUiItem *item = NULL;
    char page[256];
    if (!ui || ui->section_index >= ui->section_count) return 0;
    if (row >= page_walk(ui, (size_t)-1, NULL, NULL, NULL)) return 0;
    page_walk(ui, row, &item, NULL, NULL);
    if (!item) return 0;
    open_page_path(ui, page, sizeof(page));
    const char *path = item_path(item);
    return path && strcmp(path, page) != 0;
}
