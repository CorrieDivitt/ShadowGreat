int recomp_runtime_ui_enter_folder(RecompRuntimeUi *ui, size_t row) {
    /* Shadowgreat patch: show or hide the folder's rows in place. Returns 0
     * because no new page opens (callers then carry on with this page). */
    if (ui && ui->section_index < ui->section_count) {
        const char *sg_name = NULL;
        size_t sg_len = 0;
        char sg_page[256];
        page_walk(ui, row, NULL, &sg_name, &sg_len);
        if (sg_name && sg_len) {
            open_page_path(ui, sg_page, sizeof(sg_page));
            sg_folder_toggle(sg_page, sg_name, sg_len);
            return 0;
        }
    }
