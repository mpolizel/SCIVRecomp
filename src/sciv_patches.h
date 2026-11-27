#ifndef SCIV_PATCHES_H
#define SCIV_PATCHES_H
/* The Uncensored hack and MSU-1 packs, chosen in the launcher and applied
 * in memory: see sciv_patches.c. */
#include <stddef.h>
#include <stdint.h>

/* SnesDesktopHostGame.replace_rom: the verified ROM with the Uncensored
 * hack and the MSU-1 pack chosen in the launcher applied, or NULL when
 * neither is on. */
uint8_t *ScivReplaceRom(const uint8_t *rom, size_t size, size_t *out_size);
/* SnesDesktopHostGame.on_reset: after a save state or rewind frame is
 * loaded, select the MSU-1 track the game now expects. */
void ScivPatchesOnReset(void);

#ifdef RECOMP_LAUNCHER
struct RecompLauncherCGameInfo;
/* The launcher's EXTRAS card: Uncensored and MSU-1 rows. */
void ScivPatchesConfigureLauncher(struct RecompLauncherCGameInfo *info);
#endif

#endif
