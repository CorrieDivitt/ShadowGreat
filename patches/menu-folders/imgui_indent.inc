        const bool selected = ui->in_section && index == ui->row_index;
        /* Shadowgreat patch: rows inside an open folder are indented. */
        const float sg_indent = recomp_runtime_ui_sg_row_nested(ui, index)
                                    ? theme.spacing_md * 2.0f : 0.0f;
        char value[128];
