# Changelog

## 0.1.4 — 2026-11-24

A ROM that is not the dump this game needs is now refused with a clear message
instead of the game quitting silently after Play. The message names the needed
dump, shows the SHA-256 of your file and of the needed dump, and explains that
the ROM is checked by that hash (a copier header is ignored, so headered .smc
and headerless .sfc copies of the right dump both work, and renaming a file
does not change it). This covers a ROM chosen in RecompLauncher, which the
game used to accept with only a warning in the log. Your in-game saves and
save states keep loading.

## 0.1.3 — 2026-11-24

Rewind no longer gets stuck in a loop: after a filmstrip opened with the
keyboard key (F8) closed, it opened again at once, and the button that had
just committed or closed it closed it again, so the game never came back. Now
the key is released when the filmstrip opens, and only a new press inside it
counts. Generate & rebuild works again with the build tools the setup
downloads: their `env.bat` comes with Unix line endings, which the Windows
command prompt misreads, so the rebuild window closed before building; the
setup now runs a Windows-format copy of it. Your in-game saves and save states
keep loading.

## 0.1.2 — 2026-11-12

Current snesrecomp and recomp-ui, and RecompLauncher's recommendations. The
framework brings its access-exact sound timing (a per-clock S-DSP), sprite
priority, brightness-rounding, HDMA and mosaic fixes: over the same test run
2687 of 3000 frames now match snes9x exactly, where 0.1.1 matched 2660. A
setting that needs a restart (renderer, audio output) comes back to the same
moment instead of starting the game over; a game that reads the controller
by hand gets fresh buttons on every read; a save state loaded in another
scene is no longer drawn with the previous scene's registers. Save states
now keep the CPU's place, so one loaded during the logos, a menu or the map
resumes where it was made. Save states from 0.1.1 keep loading, with the
music playing on (the old sound state is converted). With
[RecompLauncher](https://recomplauncher.com) the game builds on the
launcher's setup screen, Play goes straight into the game, Big Picture opens
full screen, and the renderer and shader are on the game's page. Controllers
are no longer read while the window is in the background ([General]
BackgroundInput = 1 restores that); the log keeps the last runs. The update
check on start stays; when RecompLauncher starts the game it leaves updates
to the launcher.

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
