#ifndef SCIV_MSU_H
#define SCIV_MSU_H
/* MSU-1 packs: see sciv_msu.c. */
#include <stddef.h>
#include <stdint.h>

/* SnesDesktopHostGame.on_rom_loaded: apply the pack's patch in memory and arm
 * the MSU-1 chip when a pack sits in the "msu" folder. */
void ScivMsuOnRomLoaded(const uint8_t *rom, size_t size);
/* SnesDesktopHostGame.on_reset: after a save state or rewind frame is
 * loaded, select the track the game now expects. */
void ScivMsuOnReset(void);

#endif
