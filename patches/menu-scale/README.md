# Menu scale patch (our own code, applied at build time)

nesrecomp's in-game menu (recomp-ui) is drawn at a fixed size, so it looks
tiny on a 4K screen. CMakeLists.txt makes a copy of nesrecomp's
`runner/cyc/cyc_ui_imgui.cpp` in the build folder, inserts the four snippets
below at fixed places, and builds that copy instead. nesrecomp itself is never
changed, and none of its code is stored in this project.

- `helpers.inc`  after `#include "imgui_impl_sdlrenderer2.h"`: the scale
  calculation (menu about 70 % of the window height, never smaller than
  normal, at most 3x) and the mouse-position fix.
- `init.inc`     after `cyc_state_ui_init(ren, host);`: letters are drawn
  from a 3x sharper font picture so they stay crisp when scaled.
- `newframe.inc` between `ImGui_ImplSDL2_NewFrame();` and `ImGui::NewFrame();`.
- `render.inc`   replaces the `ImGui_ImplSDLRenderer2_RenderDrawData(...)` line.

If nesrecomp changes these lines, the build prints a warning and uses the
original (unscaled) file.
