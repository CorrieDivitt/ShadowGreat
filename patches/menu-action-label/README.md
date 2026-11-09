# Menu action label patch (our own code, applied at build time)

recomp-ui shows the word OPEN on the right of every action row in the
in-game menu. For our "Restore defaults" row that reads oddly, so
CMakeLists.txt builds a copy of recomp-ui's
`src/common/backends/imgui/runtime_ui_imgui.cpp` in the build folder with the
one line below changed. recomp-ui itself is never changed, and none of its
code is stored in this project.

- `value.inc` replaces `        std::snprintf(out, out_size, "OPEN");`
  (the rows whose key is `sg.defaults` or `sg.voice.defaults` show
  "Restore Defaults"; every other action row still shows OPEN).
- `number.inc` replaces the last `} else { std::snprintf(out, out_size, "%d", value); }`
  of `value_text`: our Speed, Depth and Pause rows (keys `sg.voice.speed`,
  `sg.voice.depth`, `sg.voice.pause`) are kept in hundredths and are shown
  as 1.22 or 0.5; every other number row is unchanged.

If recomp-ui changes either line, the build prints a warning and uses the
original file.
