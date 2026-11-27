/*
 * Super Castlevania IV -- the Uncensored hack and MSU-1 packs, applied in
 * memory.
 *
 * Both are chosen in the launcher's Settings, EXTRAS card:
 *
 *   Uncensored  Super Castlevania IV Uncensored 2.1 (ShadowOne333 and
 *               others): restores the crosses, blood and other art the US
 *               release censored. The player picks the hack's .ips; its
 *               optional add-ons (US font, US chain-whip sound, original US
 *               title screen) are found beside it, as the hack's zip lays
 *               them out, and offered as sub-options.
 *   MSU-1       A pack made for Conn's MSU-1 patch: the player picks the
 *               folder holding sc4_msu.ips and the sc4_msu1-<N>.pcm tracks.
 *               Every song the cartridge would play comes from the pack's
 *               track instead, as on an emulator or an FXPak.
 *
 * The choices live in extras.ini beside the executable. Each file is
 * checked by its own SHA-256 when it is picked, and the card says at once
 * whether it is recognized; only the patch files listed in kPatches are
 * ever applied, so a different version or a patch made for another ROM
 * cannot break the game.
 *
 * When the game starts, after the ROM passed its own SHA-256 check, the host
 * hands it to ScivReplaceRom, which applies what is on to a copy in memory --
 * the ROM file is never changed -- in a fixed order: Uncensored, its
 * add-ons, then the MSU-1 patch. The "Header" and "Headerless" variants
 * both work. Uncensored grows the ROM from 1 to 3 MiB; the host runs the
 * image it is given. This works for this title because all of its code runs
 * in the interpreter (recomp/bank00.cfg forces the vectors to LLE), so
 * patched bytes are executed as written.
 *
 * Nothing on: the game is the plain build.
 *
 * Save states and the rewind filmstrip do not record the MSU-1 player, so
 * after one is loaded the track the pack's code last chose ($1120) is
 * selected again from its start.
 */
#include "sciv_patches.h"

#include "common_rtl.h"
#include "snes/msu1.h"
#include "rom_patch.h"
#include "sha256.h"
#ifdef RECOMP_LAUNCHER
#include "recomp_launcher.h"
#endif

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kHeader = 0x200 };

typedef enum PatchKind { kBase, kAddon, kMsu } PatchKind;

enum { kAddonFont, kAddonWhip, kAddonTitle, kAddons };

typedef struct KnownPatch {
    const char *sha256;  /* of the .ips file */
    const char *name;
    PatchKind kind;
    int addon;           /* kAddon: which one */
    bool header;         /* made for a ROM with a 512-byte copier header */
} KnownPatch;

/* In the order they are applied. */
static const KnownPatch kPatches[] = {
    { "efa3c435df65670e49c7dbf2d7c01f6019ea6be911d71a32598a0313dd8a50c6",
      "Super Castlevania IV Uncensored 2.1", kBase, 0, false },
    { "aab318cf908a4d29bd602ee5ad5df07d175c09a7e746e9c32121fd91ff2f26e2",
      "Super Castlevania IV Uncensored 2.1", kBase, 0, true },
    { "e9e5cc175bf1eebb0470266aa6ec3fd7fe95f05d96b92f1a45d744f0ccfcc83f",
      "Uncensored add-on: US font", kAddon, kAddonFont, false },
    { "85d86f6efac4d717e3ebff000b077cdd1d4b18cb4238ec768cd53e0453d8b684",
      "Uncensored add-on: US font", kAddon, kAddonFont, true },
    { "27422f7feecb9d04ccba155db14ec77f39674f62e41179a73f0ba7c788eaed90",
      "Uncensored add-on: US chain-whip sound", kAddon, kAddonWhip, false },
    { "d9c2dd477ce425aabf63deee7cce132023836c4fd5c388293f7a5a75ca78ad4e",
      "Uncensored add-on: US chain-whip sound", kAddon, kAddonWhip, true },
    { "66d2a1dc56b90c8b10df9a896257970e426c59138a77c6f78666a112c4fd0b92",
      "Uncensored add-on: original US title screen", kAddon, kAddonTitle, false },
    { "192aa20cdc4ce6cb39597d8c425c61974dd3c379a61077b4552384adad09642a",
      "Uncensored add-on: original US title screen", kAddon, kAddonTitle, true },
    { "151ab27348355d3ffacf5000b1fa7482c1c5d5455a80a90d93a9a08a6eb885f6",
      "Conn's MSU-1 patch (2024)", kMsu, 0, false },
    { "6617d259d28cf8c7508cc283f4397928a759bbc204cd115eb8abfe2e6297d848",
      "Conn's MSU-1 patch (2017)", kMsu, 0, false },
};
enum { kKnown = sizeof(kPatches) / sizeof(kPatches[0]) };

static const char *const kAddonLabels[kAddons] = {
    "US font (the English release's letters)",
    "US chain-whip sound",
    "Original US title screen",
};

enum { kTrackWram = 0x1120, kLoopTable = 0xFF700 /* $1F:F700 */ };

/* ---- settings (extras.ini) ------------------------------------------- */

static struct {
    bool loaded;
    int unc_on;
    char unc_path[1024];
    int addon_on[kAddons];
    int msu_on;
    char msu_dir[1024];
} s_set;

/* What the chosen files are, worked out when they are chosen or loaded. */
static struct {
    int unc;                       /* kPatches index of the hack, or -1 */
    char unc_status[512];
    int unc_level;
    int addon[kAddons];            /* kPatches index found beside it, or -1 */
    char addon_path[kAddons][1024];
    int msu;                       /* kPatches index of Conn's patch, or -1 */
    char msu_patch_path[1024];
    int msu_tracks;
    char msu_status[512];
    int msu_level;
} s_chk;

static bool s_msu_live;            /* MSU-1 patch applied this run */
static uint8_t s_loop[0x30];       /* the MSU patch's loop table, per track */

static void IniPath(char *out, size_t cap)
{
    const char *env = getenv("SCIV_EXTRAS_INI");   /* tests */
    if (env && *env) { snprintf(out, cap, "%s", env); return; }
    const char *base = SDL_GetBasePath();
    snprintf(out, cap, "%sextras.ini", base ? base : "");
}

static void Trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') ++i;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}

static void SaveSettings(void)
{
    char path[1100];
    IniPath(path, sizeof(path));
    char buf[4096];
    int n = snprintf(buf, sizeof(buf),
        "; Super Castlevania IV extras, set in the launcher (Settings, EXTRAS).\n"
        "[Uncensored]\nEnabled = %d\nPatch = %s\nUsFont = %d\nUsWhipSound = %d\nUsTitleScreen = %d\n"
        "[MSU1]\nEnabled = %d\nFolder = %s\n",
        s_set.unc_on, s_set.unc_path, s_set.addon_on[kAddonFont],
        s_set.addon_on[kAddonWhip], s_set.addon_on[kAddonTitle],
        s_set.msu_on, s_set.msu_dir);
    if (n > 0 && (size_t)n < sizeof(buf))
        SDL_SaveFile(path, buf, (size_t)n);
}

static void CheckAll(void);

static void LoadSettings(void)
{
    if (s_set.loaded) return;
    s_set.loaded = true;
    char path[1100];
    IniPath(path, sizeof(path));
    size_t size = 0;
    char *data = (char *)SDL_LoadFile(path, &size);
    if (data) {
        char section[32] = "";
        for (char *line = strtok(data, "\n"); line; line = strtok(NULL, "\n")) {
            Trim(line);
            if (!line[0] || line[0] == ';' || line[0] == '#') continue;
            if (line[0] == '[') { snprintf(section, sizeof(section), "%s", line); continue; }
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = '\0';
            char *key = line, *val = eq + 1;
            Trim(key); Trim(val);
            const bool unc = strcmp(section, "[Uncensored]") == 0;
            const bool msu = strcmp(section, "[MSU1]") == 0;
            if (unc && !strcmp(key, "Enabled")) s_set.unc_on = atoi(val) != 0;
            else if (unc && !strcmp(key, "Patch")) snprintf(s_set.unc_path, sizeof(s_set.unc_path), "%s", val);
            else if (unc && !strcmp(key, "UsFont")) s_set.addon_on[kAddonFont] = atoi(val) != 0;
            else if (unc && !strcmp(key, "UsWhipSound")) s_set.addon_on[kAddonWhip] = atoi(val) != 0;
            else if (unc && !strcmp(key, "UsTitleScreen")) s_set.addon_on[kAddonTitle] = atoi(val) != 0;
            else if (msu && !strcmp(key, "Enabled")) s_set.msu_on = atoi(val) != 0;
            else if (msu && !strcmp(key, "Folder")) snprintf(s_set.msu_dir, sizeof(s_set.msu_dir), "%s", val);
        }
        SDL_free(data);
    }
    CheckAll();
}

/* ---- checking files --------------------------------------------------- */

/* kPatches index of the file at `path` (by SHA-256), or -1; hex gets the
 * digest when the file could be read. */
static int Identify(const char *path, char hex[65])
{
    hex[0] = '\0';
    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    if (!data) return -2;
    uint8_t digest[32];
    sha256_compute((const uint8_t *)data, size, digest);
    SDL_free(data);
    for (int i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    for (int k = 0; k < kKnown; ++k)
        if (strcmp(kPatches[k].sha256, hex) == 0) return k;
    return -1;
}

static bool EndsWithI(const char *s, const char *ext)
{
    size_t n = strlen(s), e = strlen(ext);
    return n >= e && SDL_strcasecmp(s + n - e, ext) == 0;
}

/* Add-ons beside the hack's .ips: the hack's zip keeps them in
 * Optional\<name>\, so look a few folders down. */
typedef struct AddonScan { int depth; } AddonScan;

static SDL_EnumerationResult SDLCALL AddonVisit(void *ud, const char *dir, const char *name)
{
    AddonScan *scan = (AddonScan *)ud;
    char path[1200];
    const size_t dl = strlen(dir);
    const bool sep = dl && (dir[dl - 1] == '\\' || dir[dl - 1] == '/');
    snprintf(path, sizeof(path), "%s%s%s", dir, sep ? "" : "/", name);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        if (scan->depth < 3) {
            AddonScan sub = { scan->depth + 1 };
            SDL_EnumerateDirectory(path, AddonVisit, &sub);
        }
    } else if (info.type == SDL_PATHTYPE_FILE && info.size < 65536 && EndsWithI(name, ".ips")) {
        char hex[65];
        const int k = Identify(path, hex);
        if (k >= 0 && kPatches[k].kind == kAddon && s_chk.addon[kPatches[k].addon] < 0) {
            s_chk.addon[kPatches[k].addon] = k;
            snprintf(s_chk.addon_path[kPatches[k].addon], sizeof(s_chk.addon_path[0]), "%s", path);
        }
    }
    return SDL_ENUM_CONTINUE;
}

static void CheckUncensored(void)
{
    s_chk.unc = -1;
    for (int a = 0; a < kAddons; ++a) { s_chk.addon[a] = -1; s_chk.addon_path[a][0] = '\0'; }
    s_chk.unc_level = 0;
    if (!s_set.unc_path[0]) {
        snprintf(s_chk.unc_status, sizeof(s_chk.unc_status),
                 "Choose \"Super Castlevania IV Uncensored (Headerless).ips\" (or the Header one) "
                 "from the hack's version 2.1.");
        return;
    }
    char hex[65];
    const int k = Identify(s_set.unc_path, hex);
    s_chk.unc_level = 2;
    if (k == -2) {
        snprintf(s_chk.unc_status, sizeof(s_chk.unc_status), "This file cannot be read.");
        return;
    }
    if (k < 0 || kPatches[k].kind != kBase) {
        snprintf(s_chk.unc_status, sizeof(s_chk.unc_status),
                 k >= 0 && kPatches[k].kind == kAddon
                     ? "This is one of the hack's optional add-ons. Choose the main file, "
                       "\"Super Castlevania IV Uncensored (Headerless).ips\"; the add-ons beside "
                       "it are found on their own."
                 : k >= 0
                     ? "This is an MSU-1 patch: choose its folder in the MSU-1 row below."
                     : "Not a supported version of the hack. Files are checked by their SHA-256; "
                       "this game knows Super Castlevania IV Uncensored 2.1 (Headerless or Header .ips).");
        return;
    }
    s_chk.unc = k;
    s_chk.unc_level = 1;
    /* The folder the .ips is in, and the add-ons under it. */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", s_set.unc_path);
    char *sep = strrchr(dir, '\\');
    char *sep2 = strrchr(dir, '/');
    if (sep2 > sep) sep = sep2;
    if (sep) {
        sep[1] = '\0';
        AddonScan scan = { 0 };
        SDL_EnumerateDirectory(dir, AddonVisit, &scan);
    }
    int found = 0;
    for (int a = 0; a < kAddons; ++a) found += s_chk.addon[a] >= 0;
    snprintf(s_chk.unc_status, sizeof(s_chk.unc_status),
             "Recognized: %s (%s)%s", kPatches[k].name,
             kPatches[k].header ? "Header" : "Headerless",
             found ? ". Its optional add-ons are below." : ".");
}

static bool IsAscii(const char *s)
{
    for (; *s; ++s) if ((unsigned char)*s >= 0x80) return false;
    return true;
}

typedef struct MsuScan { const char *dir; } MsuScan;

static SDL_EnumerationResult SDLCALL MsuVisit(void *ud, const char *dir, const char *name)
{
    (void)ud;
    char path[1200];
    const size_t dl = strlen(dir);
    const bool sep = dl && (dir[dl - 1] == '\\' || dir[dl - 1] == '/');
    snprintf(path, sizeof(path), "%s%s%s", dir, sep ? "" : "/", name);
    if (EndsWithI(name, ".pcm")) { ++s_chk.msu_tracks; return SDL_ENUM_CONTINUE; }
    if (EndsWithI(name, ".ips") || EndsWithI(name, ".bps")) {
        char hex[65];
        const int k = Identify(path, hex);
        if (k >= 0 && kPatches[k].kind == kMsu && s_chk.msu < 0) {
            s_chk.msu = k;
            snprintf(s_chk.msu_patch_path, sizeof(s_chk.msu_patch_path), "%s", path);
        }
    }
    return SDL_ENUM_CONTINUE;
}

static void CheckMsu(void)
{
    s_chk.msu = -1;
    s_chk.msu_tracks = 0;
    s_chk.msu_patch_path[0] = '\0';
    s_chk.msu_level = 0;
    if (!s_set.msu_dir[0]) {
        snprintf(s_chk.msu_status, sizeof(s_chk.msu_status),
                 "Choose the folder with Conn's MSU-1 patch (sc4_msu.ips) and the pack's "
                 "sc4_msu1-<N>.pcm tracks.");
        return;
    }
    s_chk.msu_level = 2;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(s_set.msu_dir, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
        snprintf(s_chk.msu_status, sizeof(s_chk.msu_status), "This folder cannot be opened.");
        return;
    }
    if (!IsAscii(s_set.msu_dir)) {
        snprintf(s_chk.msu_status, sizeof(s_chk.msu_status),
                 "The tracks cannot be played from a folder whose path has accented or "
                 "non-English letters. Move the pack to a folder such as C:\\Games\\sc4msu "
                 "and choose it again.");
        return;
    }
    SDL_EnumerateDirectory(s_set.msu_dir, MsuVisit, NULL);
    if (s_chk.msu < 0 && s_chk.msu_tracks == 0)
        snprintf(s_chk.msu_status, sizeof(s_chk.msu_status),
                 "No MSU-1 pack here: the folder needs Conn's sc4_msu.ips and the "
                 "sc4_msu1-<N>.pcm tracks.");
    else if (s_chk.msu < 0)
        snprintf(s_chk.msu_status, sizeof(s_chk.msu_status),
                 "%d track(s) found, but not Conn's MSU-1 patch (sc4_msu.ips, checked by its "
                 "SHA-256). Put it in this folder: it comes with the pack or from Conn's "
                 "sc4_msu.zip.", s_chk.msu_tracks);
    else if (s_chk.msu_tracks == 0)
        snprintf(s_chk.msu_status, sizeof(s_chk.msu_status),
                 "%s found, but no sc4_msu1-<N>.pcm tracks.", kPatches[s_chk.msu].name);
    else {
        s_chk.msu_level = 1;
        snprintf(s_chk.msu_status, sizeof(s_chk.msu_status),
                 "Recognized: %s and %d music track(s).", kPatches[s_chk.msu].name,
                 s_chk.msu_tracks);
    }
}

static void CheckAll(void)
{
    CheckUncensored();
    CheckMsu();
}

/* ---- the launcher's EXTRAS card --------------------------------------- */

#ifdef RECOMP_LAUNCHER
static int UncGet(void) { LoadSettings(); return s_set.unc_on; }
static void UncSet(int on) { LoadSettings(); s_set.unc_on = on != 0; SaveSettings(); }
static const char *UncPath(void) { LoadSettings(); return s_set.unc_path; }
static void UncPick(const char *path)
{
    LoadSettings();
    snprintf(s_set.unc_path, sizeof(s_set.unc_path), "%s", path);
    CheckUncensored();
    s_set.unc_on = s_chk.unc >= 0;
    SaveSettings();
}
static const char *UncStatus(int *level) { LoadSettings(); *level = s_chk.unc_level; return s_chk.unc_status; }
static int s_sub_map[kAddons];
static int UncSubCount(void)
{
    LoadSettings();
    int n = 0;
    for (int a = 0; a < kAddons; ++a) if (s_chk.addon[a] >= 0) s_sub_map[n++] = a;
    return n;
}
static const char *UncSubLabel(int i) { return kAddonLabels[s_sub_map[i]]; }
static int UncSubGet(int i) { return s_set.addon_on[s_sub_map[i]]; }
static void UncSubSet(int i, int on) { s_set.addon_on[s_sub_map[i]] = on != 0; SaveSettings(); }

static int MsuGet(void) { LoadSettings(); return s_set.msu_on; }
static void MsuSet(int on) { LoadSettings(); s_set.msu_on = on != 0; SaveSettings(); }
static const char *MsuPath(void) { LoadSettings(); return s_set.msu_dir; }
static void MsuPick(const char *path)
{
    LoadSettings();
    snprintf(s_set.msu_dir, sizeof(s_set.msu_dir), "%s", path);
    CheckMsu();
    s_set.msu_on = s_chk.msu_level == 1;
    SaveSettings();
}
static const char *MsuStatus(int *level) { LoadSettings(); *level = s_chk.msu_level; return s_chk.msu_status; }

static const char *const kIpsPatterns[] = { "*.ips" };

static const RecompLauncherHostExtra kExtras[] = {
    {
        .label = "Uncensored graphics",
        .help = "Super Castlevania IV Uncensored 2.1, a fan hack: restores the crosses, blood "
                "and other art the US release censored. Get it from romhacking.net (hack 2928), "
                "unzip it and choose \"Super Castlevania IV Uncensored (Headerless).ips\". "
                "Your ROM file is never changed: the hack is applied in memory when the game "
                "starts. A change made while playing applies the next time the game starts.",
        .pick_folder = 0,
        .pick_title = "Choose the Uncensored patch (.ips)",
        .pick_patterns = kIpsPatterns,
        .pick_pattern_count = 1,
        .pick_description = "IPS patch (.ips)",
        .get_enabled = UncGet, .set_enabled = UncSet,
        .get_path = UncPath, .set_path = UncPick,
        .status = UncStatus,
        .sub_count = UncSubCount, .sub_label = UncSubLabel,
        .sub_get = UncSubGet, .sub_set = UncSubSet,
    },
    {
        .label = "MSU-1 music",
        .help = "Plays an MSU-1 pack's CD-quality soundtrack instead of the cartridge's music. "
                "Choose the folder holding Conn's MSU-1 patch (sc4_msu.ips) and the pack's "
                "sc4_msu1-<N>.pcm tracks; packs are shared on the Zeldix forum. Sound effects "
                "still come from the console's sound chip. A change made while playing applies "
                "the next time the game starts.",
        .pick_folder = 1,
        .pick_title = "Choose the MSU-1 pack folder",
        .get_enabled = MsuGet, .set_enabled = MsuSet,
        .get_path = MsuPath, .set_path = MsuPick,
        .status = MsuStatus,
    },
};

void ScivPatchesConfigureLauncher(RecompLauncherCGameInfo *info)
{
    LoadSettings();
    info->host_extras_title = "EXTRAS";
    info->host_extras = kExtras;
    info->num_host_extras = (int)(sizeof(kExtras) / sizeof(kExtras[0]));
}
#endif

/* ---- building the image ----------------------------------------------- */

/* Apply the patch file at `path` to the headerless image in (*img, *size). */
static bool Apply(uint8_t **img, size_t *size, const char *path, const KnownPatch *k)
{
    size_t patch_size = 0;
    void *patch = SDL_LoadFile(path, &patch_size);
    if (!patch) {
        fprintf(stderr, "[extras] cannot read '%s': %s not applied\n", path, k->name);
        return false;
    }
    uint8_t *src = *img;
    size_t src_size = *size;
    uint8_t *with_header = NULL;
    if (k->header) {
        with_header = (uint8_t *)calloc(1, src_size + kHeader);
        if (!with_header) { SDL_free(patch); return false; }
        memcpy(with_header + kHeader, src, src_size);
        src = with_header;
        src_size += kHeader;
    }
    uint8_t *out = NULL;
    size_t out_size = 0;
    RomPatchStatus st = snes_rom_patch_apply(src, src_size, (const uint8_t *)patch, patch_size,
                                             (size_t)16 << 20, &out, &out_size);
    free(with_header);
    SDL_free(patch);
    if (st != kRomPatch_Ok || (k->header && out_size <= kHeader)) {
        fprintf(stderr, "[extras] %s could not be applied: %s\n", k->name,
                st != kRomPatch_Ok ? snes_rom_patch_status_text(st) : "empty result");
        free(out);
        return false;
    }
    if (k->header) {
        memmove(out, out + kHeader, out_size - kHeader);
        out_size -= kHeader;
    }
    free(*img);
    *img = out;
    *size = out_size;
    fprintf(stderr, "[extras] applied %s ('%s'), ROM now %u bytes\n", k->name, path,
            (unsigned)out_size);
    return true;
}

uint8_t *ScivReplaceRom(const uint8_t *rom, size_t size, size_t *out_size)
{
    LoadSettings();
    CheckAll();   /* files may have moved since they were chosen */
    const bool unc = s_set.unc_on && s_chk.unc >= 0;
    const bool msu = s_set.msu_on && s_chk.msu_level == 1;
    if (s_set.unc_on && !unc)
        fprintf(stderr, "[extras] Uncensored is on but not used: %s\n", s_chk.unc_status);
    if (s_set.msu_on && !msu)
        fprintf(stderr, "[extras] MSU-1 is on but not used: %s\n", s_chk.msu_status);
    if (!rom || (!unc && !msu))
        return NULL;

    /* Work on the bare ROM: a copier header is dropped. */
    const size_t head = (size & 0x7FFF) == kHeader ? kHeader : 0;
    size_t img_size = size - head;
    uint8_t *img = (uint8_t *)malloc(img_size);
    if (!img) return NULL;
    memcpy(img, rom + head, img_size);

    bool changed = false;
    if (unc && Apply(&img, &img_size, s_set.unc_path, &kPatches[s_chk.unc])) {
        changed = true;
        for (int a = 0; a < kAddons; ++a)
            if (s_set.addon_on[a] && s_chk.addon[a] >= 0)
                Apply(&img, &img_size, s_chk.addon_path[a], &kPatches[s_chk.addon[a]]);
    }
    if (msu && Apply(&img, &img_size, s_chk.msu_patch_path, &kPatches[s_chk.msu])) {
        changed = true;
        s_msu_live = true;
        if (kLoopTable + sizeof(s_loop) <= img_size)
            memcpy(s_loop, img + kLoopTable, sizeof(s_loop));
        /* RtlRegisterGame (right after this) arms the chip from the
         * environment; a folder resolves to the pack's "<name>-<N>.pcm" base. */
#ifdef _WIN32
        _putenv_s("SNESRECOMP_MSU1", s_set.msu_dir);
#else
        setenv("SNESRECOMP_MSU1", s_set.msu_dir, 1);
#endif
        fprintf(stderr, "[MSU-1] pack on: %d track(s) in '%s'\n", s_chk.msu_tracks, s_set.msu_dir);
    }
    if (!changed) { free(img); return NULL; }
    *out_size = img_size;
    return img;
}

/* A save state or rewind frame was loaded: play what the game now expects. */
void ScivPatchesOnReset(void)
{
    if (!s_msu_live || !msu1_enabled())
        return;
    const uint8_t track = g_ram[kTrackWram];
    if (track == 0 || track >= sizeof(s_loop)) {
        msu1_write(0x2007, 0);
        return;
    }
    msu1_write(0x2004, track);
    msu1_write(0x2005, 0);
    msu1_write(0x2006, 0xFF);
    msu1_write(0x2007, s_loop[track] ? s_loop[track] : 3);
}
