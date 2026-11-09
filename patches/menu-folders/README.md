# Menu folders patch (our own code, applied at build time)

recomp-ui shows a section's sub-folders (items whose section is a path such
as `Shadowgreat/Control Options`) as rows that open a page of their own. We
want the Shadowgreat headings to open in place instead: choosing a heading
shows its rows indented right below it, and choosing it again hides them.

CMakeLists.txt builds copies of two recomp-ui files in the build folder:

- `src/common/recomp_runtime_ui.c` (the menu's rows and keys)
  - `state.inc` goes before `/* Walk the open page's rows in order.`:
    which folders are open (remembered while the game runs, all closed at
    start).
  - `walk.inc` replaces the end of `page_walk` (`++row; } return row; }`):
    an open folder's rows follow its row; it also adds two small functions
    the renderer asks (is this folder open? is this row inside one?).
  - `enter.inc` replaces the first line of `recomp_runtime_ui_enter_folder`:
    a folder row opens or closes in place instead of opening a page.
- `src/common/backends/imgui/runtime_ui_imgui.cpp` (after the
  menu-action-label patch):
  - `imgui_decl.inc` replaces `#include <cstring>` (adds the declarations).
  - `imgui_folder.inc` replaces the folder row label: `>` when closed,
    `v` when open.
  - `imgui_indent.inc` and two one-line changes indent rows inside an open
    folder.

recomp-ui itself is never changed, and none of its code is stored in this
project. The two files are patched together or not at all: if recomp-ui
changes any of these lines, the build prints a warning and uses the original
files, and the headings then open as pages (everything still works).
