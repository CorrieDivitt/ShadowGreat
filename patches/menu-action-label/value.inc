        // Shadowgreat (patches/menu-action-label): our reset rows' own words.
        std::snprintf(out, out_size, "%s",
                      item->key && (std::strcmp(item->key, "sg.defaults") == 0 ||
                                    std::strcmp(item->key, "sg.voice.defaults") == 0)
                          ? "Restore Defaults" : "OPEN");
