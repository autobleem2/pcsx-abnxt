# The in-game menu's skin - `skin/skin.cfg`

The AutoBleem menu (`frontend/ab/ab_menu.c`: the menu, the disc picker, the message box, About) takes its
look from `skin/skin.cfg` next to the emulator, so the look is data and can follow the launcher's theme.
`frontend/ab/skin/skin.cfg` is the shipped file (ab2.0.0's look, autobleem-design
`themes/ab2.0.0/design/emu/README.md`); every packaging script copies `skin/` as a whole.

**When it is read**: once, the first time the menu, the picker or the HUD's font asks for it
(`ab_ui_skin()` in `frontend/ab/ab_ui.c`) - never per frame. Changing the file takes a new run.

**Format**: one `key = value` per line; spaces around both are ignored; a line starting with `#` and an
empty line are skipped. A missing file, a missing key, an unknown key or a bad value keeps the built-in
value (the same as the shipped file) and says so in the log (`autobleem: skin/skin.cfg:<line>: ...`).

| Key | Value | Built-in | What it colours |
|---|---|---|---|
| `text` | colour | `f4f6f8` | titles, the selected row's name, values being changed |
| `name` | colour | `d6dde6` | the rows' names |
| `dim` | colour | `9aa4b2` | values, the game's id line, help text, the build lines |
| `grey` | colour | `5c6674` | a row that cannot be used now |
| `shadow` | colour | `0c0f13` | the shadow under text drawn over the art |
| `accent` | colour | `36d9e0` | the panels' rim, section headings and their rule, the value arrows, the disc in the drive |
| `panel` | colour | `262e38` | the panels (the rows', the picker's, the message box, About, the snapshot frame) |
| `panel_alpha` | 0..255 | `220` | the panels' opacity over the art |
| `row` | colour | `481a36` | the selected row's wash, the L1/R1 chips |
| `row_alpha` | 0..255 | `200` | the wash's opacity over its panel |
| `select_rim` | colour | `ff46aa` | the selected row's rim, the L1/R1 chips' rim |
| `hint_disc` | colour | `3a3a3e` | the hint bar's pad glyphs: the disc |
| `hint_rim` | colour | `59595d` | ... its rim |
| `hint_cross` | colour | `6d7df6` | ... the Cross |
| `hint_circle` | colour | `e44e74` | ... the Circle |
| `background` | file name | `ab_background.jpg` | the art behind the menu, a JPEG or PNG in `skin/`, scaled to cover the screen (a 1280x720 design: the logo bottom left, the hint bar at 466..1268 x 614..684) |
| `font` | file name | `ui.ttf` | the TrueType font in `skin/` for the menu and the HUD - a static instance (stb_truetype ignores a variable font's axes); a language file's `\|@font\|` still wins |

A colour is `RRGGBB` or `#RRGGBB` (hex, drawn as RGB565). A file name is a name in `skin/`, not a path (no
`/`, `\` or leading `.`); when the named file is missing the default name is tried.

**Not in the file** (code, `ab_menu.c`): the layout - the 1280x720 design's positions and sizes, the cut
corners' sizes, the text sizes.

The launcher writing this file from its current theme is a later step; until then the shipped file is the look.
