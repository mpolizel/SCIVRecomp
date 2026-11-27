/*
 * Super Castlevania IV -- MSU-1 packs.
 *
 * Players who own an MSU-1 pack (Conn's sc4_msu.ips plus a set of
 * sc4_msu1-<N>.pcm tracks) put it in a folder named "msu" beside the
 * executable. After the ROM passed its SHA-256 check, the patch is applied
 * to the ROM image in memory -- the file on disk is never touched -- and the
 * framework's MSU-1 chip ($2000-$2007, snes/msu1.c) is armed on that folder.
 * The game then runs the pack's own code: every song the cartridge would
 * upload to the sound chip is played from the matching PCM track instead,
 * exactly as on an emulator or an FXPak.
 *
 * This works for this title because all of its code runs in the
 * interpreter (recomp/bank00.cfg forces the vectors to LLE), so patched
 * bytes are executed as written.
 *
 * No folder, no patch or no tracks: nothing changes, the game is
 * byte-for-byte the plain build.
 *
 * Save states and the rewind filmstrip do not record the MSU-1 player, so
 * after one is loaded the track the pack's code last chose ($1120) is
 * selected again from its start.
 */
#include "sciv_msu.h"

#include "common_rtl.h"
#include "snes/msu1.h"
#include "rom_patch.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#undef HIBYTE
#undef LOBYTE
#include <windows.h>
#endif

enum { kTrackWram = 0x1120, kLoopTable = 0xFF700 /* $1F:F700 */ };

static bool s_active;
static const uint8_t *s_rom;     /* patched image (headerless view) */
static size_t s_rom_size;

static bool MsuDir(char *out, size_t cap)
{
    const char *env = getenv("SCIV_MSU_DIR");
    if (env && *env) { snprintf(out, cap, "%s", env); return true; }
#ifdef _WIN32
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    char *sep = strrchr(exe, '\\');
    if (!sep) return false;
    sep[1] = '\0';
    /* <exe dir>\msu, else the install folder's msu (the executable lives in
     * <install>\build, which a rebuild may recreate). */
    snprintf(out, cap, "%smsu", exe);
    DWORD attr = GetFileAttributesA(out);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        snprintf(out, cap, "%s..\\msu", exe);
#else
    snprintf(out, cap, "msu");
#endif
    return true;
}

static uint8_t *ReadAll(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (n > 0 && n < (16 << 20)) ? (uint8_t *)malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (buf) *size = (size_t)n;
    return buf;
}

/* The pack's patch (one .ips or .bps) and how many .pcm tracks sit beside it. */
static bool FindPack(const char *dir, char *patch, size_t cap, int *tracks)
{
    *tracks = 0;
    patch[0] = '\0';
#ifdef _WIN32
    char pat[MAX_PATH + 8];
    snprintf(pat, sizeof(pat), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        const char *name = fd.cFileName;
        const char *dot = strrchr(name, '.');
        if (!dot) continue;
        if (_stricmp(dot, ".pcm") == 0) ++*tracks;
        else if ((_stricmp(dot, ".ips") == 0 || _stricmp(dot, ".bps") == 0) && !patch[0])
            snprintf(patch, cap, "%s\\%s", dir, name);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#endif
    return patch[0] != '\0';
}

void ScivMsuOnRomLoaded(const uint8_t *rom_c, size_t size)
{
    char dir[1024], patch_path[1300];
    int tracks = 0;
    if (!rom_c || !MsuDir(dir, sizeof(dir)))
        return;
    if (!FindPack(dir, patch_path, sizeof(patch_path), &tracks)) {
        if (tracks)
            fprintf(stderr, "[MSU-1] %d track(s) in '%s' but no .ips/.bps patch: "
                            "the pack is not used\n", tracks, dir);
        return;
    }
    if (tracks == 0) {
        fprintf(stderr, "[MSU-1] patch '%s' found but no .pcm tracks beside it: "
                        "the pack is not used\n", patch_path);
        return;
    }
    /* A copier header (512 bytes) is skipped: patches target the bare ROM. */
    uint8_t *rom = (uint8_t *)rom_c;
    const size_t head = (size & 0x7FFF) == 0x200 ? 0x200 : 0;
    size_t patch_size = 0;
    uint8_t *patch = ReadAll(patch_path, &patch_size);
    if (!patch) {
        fprintf(stderr, "[MSU-1] cannot read '%s': the pack is not used\n", patch_path);
        return;
    }
    uint8_t *out = NULL;
    size_t out_size = 0;
    RomPatchStatus st = snes_rom_patch_apply(rom + head, size - head, patch, patch_size,
                                             size - head, &out, &out_size);
    free(patch);
    if (st != kRomPatch_Ok || out_size != size - head) {
        fprintf(stderr, "[MSU-1] '%s' could not be applied (%s): the pack is not used\n",
                patch_path, st != kRomPatch_Ok ? snes_rom_patch_status_text(st)
                                               : "it changes the ROM size");
        free(out);
        return;
    }
    memcpy(rom + head, out, out_size);
    free(out);
    s_rom = rom + head;
    s_rom_size = size - head;

    /* RtlRegisterGame (right after this hook) arms the chip from the
     * environment; a folder resolves to the pack's "<name>-<N>.pcm" base. */
#ifdef _WIN32
    _putenv_s("SNESRECOMP_MSU1", dir);
#else
    setenv("SNESRECOMP_MSU1", dir, 1);
#endif
    s_active = true;
    fprintf(stderr, "[MSU-1] pack on: '%s' applied in memory, %d track(s) in '%s'\n",
            patch_path, tracks, dir);
}

/* A save state or rewind frame was loaded: play what the game now expects. */
void ScivMsuOnReset(void)
{
    if (!s_active || !msu1_enabled())
        return;
    const uint8_t track = g_ram[kTrackWram];
    if (track == 0 || track >= 0x30) {
        msu1_write(0x2007, 0);
        return;
    }
    const uint8_t loop = (s_rom && kLoopTable + track < s_rom_size)
                             ? s_rom[kLoopTable + track] : 3;
    msu1_write(0x2004, track);
    msu1_write(0x2005, 0);
    msu1_write(0x2006, 0xFF);
    msu1_write(0x2007, loop);
}
