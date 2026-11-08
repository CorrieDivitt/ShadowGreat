        // Shadowgreat (patches/menu-action-label): our reset row's own word.
        std::snprintf(out, out_size, "%s",
                      item->key && std::strcmp(item->key, "sg.defaults") == 0 ? "Restore Defaults" : "OPEN");
