# Pad B button patch (our own change, applied at build time)

nesrecomp's default puts the NES B button on the controller's **X** button
(the NES B sits left of A). Shadowgreat's players expect B on the controller's
**B** button, so CMakeLists.txt builds a copy of nesrecomp's
`runner/cyc/cyc_input.c` in the build folder with one default changed:

- `CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_X)` in the default pad list becomes
  `CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_B)` (both players).

Only the default changes: a `config.ini` that already says `b = x` keeps it,
and every button can still be reassigned on the game's start screen
(Controls). nesrecomp itself is never changed, and none of its code is stored
in this project. If nesrecomp changes that line, the build prints a warning and
uses the original file.
