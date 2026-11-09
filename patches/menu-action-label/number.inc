    } else if (item->key && std::strncmp(item->key, "sg.voice.", 9) == 0 &&
               (std::strcmp(item->key + 9, "speed") == 0 ||
                std::strcmp(item->key + 9, "depth") == 0 ||
                std::strcmp(item->key + 9, "pause") == 0)) {
        // Shadowgreat (patches/menu-action-label): our voice rows are kept in
        // hundredths; show them as 1.22 (speed, depth) or 0.5 (pause).
        std::snprintf(out, out_size, item->key[9] == 'p' ? "%.1f" : "%.2f",
                      static_cast<double>(value) / 100.0);
    } else {
        std::snprintf(out, out_size, "%d", value);
    }
