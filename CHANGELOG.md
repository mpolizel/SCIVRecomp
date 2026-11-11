# Changelog

## 0.1.2 — 2026-11-11

Framework fixes from the later ports and RecompLauncher support. A setting
that needs a restart (renderer, audio output) now comes back to the same
moment instead of starting the game over; a game that reads the controller
by hand gets fresh buttons on every read; a save state loaded in another
scene is no longer drawn with the previous scene's registers. The release
carries a `recomplauncher.json` for
[RecompLauncher](https://recomplauncher.com). Normal play is unchanged (the
test run gives the same pictures, frame by frame); saves and save states from
0.1.1 keep working.

## 0.1.1 — 2026-11-03

### Changed
- A fresh install starts with **no shader** (the plain picture), the lightest
  option. A full-screen CRT shader made the game drop frames on weaker laptops.
  Pick CRT Lottes or another preset under Settings → Shader; your choice is
  still saved and kept. An install that already has a shader keeps it.
- On laptops with two graphics cards (NVIDIA Optimus, AMD switchable
  graphics), the game now asks for the **dedicated graphics card** instead of
  the integrated one.

## 0.1.0 — 2026-11-02

First release: a setup pack that recompiles the game from your own ROM. Boots
through the Konami logo, intro and title screen into the first stage, which
plays. Fresh installs start with the CRT Lottes shader; scanline, CRT and
upscaler presets (ScaleFX, NNEDI3); settings during play (Ctrl+L or
Select+L3); audio interpolation (I key: Gaussian, Cubic, Sinc); update check
on start that backs up your save states before installing; console-free
Windows build.
