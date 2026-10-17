// Headless FCEUX driver: run a ROM with a scripted input movie and dump CPU
// RAM after every emulated frame. Test oracle for the Battle City project.
//
// Copyright (c) 2026 Strawberry
// SPDX-License-Identifier: GPL-2.0-or-later
// Builds and links against the FCEUX 2.6.6 core (GPL-2.0-or-later), so this
// directory is GPL, unlike the rest of the project (MIT). See COPYING.
//
// usage: fceux_headless ROM MOVIE FRAMES RAM_OUT [--raminit N] [--audio PREFIX]
//   MOVIE: nesrun format ("COUNT BUTTONS [BUTTONS2]" lines, '-' for none)
//   RAM_OUT: FRAMES x 2048 bytes
//   --raminit: FCEUX RAMInitOption (0 = default 00x4/FFx4 pattern, 2 = zeros)
//   --audio: FCEUX's sound output at 48 kHz, high quality, default volumes:
//     PREFIX.f32 = mono float samples (int16 scale / 32768),
//     PREFIX.cnt = samples produced per frame (text, one line per frame)
//   --mute MASK: silence channels in FCEUX's mixer (bit 0/1 square 1/2,
//     2 triangle, 3 noise), with FCEUI_SetSquare1Volume etc.
//   --apustate F0 F1: print FCEUX's APU state (from an uncompressed in-memory
//     save state) at the end of frames F0..F1: sweep counters, periods,
//     frame counter, length counters, envelope levels, triangle linear counter.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "types.h"
#include "fceu.h"
#include "driver.h"
#include "emufile.h"
#include "utils/memory.h"
#include "state.h"

// ---- globals the frontend normally provides ----
int dendy = 0;
int pal_emulation = 0;
bool swapDuty = false;
bool turbo = false;
bool paldeemphswap = false;
int eoptions = 0;
int KillFCEUXonFrame = 0;
int closeFinishedMovie = 0;

// ---- driver hooks (all no-ops for a headless run) ----
FILE *FCEUD_UTF8fopen(const char *fn, const char *mode) { return fopen(fn, mode); }
EMUFILE_FILE *FCEUD_UTF8_fstream(const char *n, const char *m) { return new EMUFILE_FILE(n, m); }
void FCEUD_PrintError(const char *s) { fprintf(stderr, "fceux error: %s\n", s); }
void FCEUD_Message(const char *s) { (void)s; }
void FCEUD_NetworkClose(void) {}
void FCEUD_DebugBreakpoint(int) {}
void FCEUD_SetPalette(uint8, uint8, uint8, uint8) {}
void FCEUD_GetPalette(uint8, uint8 *r, uint8 *g, uint8 *b) { *r = *g = *b = 0; }
void FCEUI_AviVideoUpdate(const unsigned char *) {}
bool FCEUI_AviIsRecording() { return false; }
bool FCEUI_AviEnableHUDrecording() { return false; }
bool FCEUI_AviDisableMovieMessages() { return true; }
int FCEUD_SendData(void *, uint32) { return 0; }
int FCEUD_RecvData(void *, uint32) { return 0; }
void FCEUD_FlushTrace() {}
uint64 FCEUD_GetTime(void) { return 0; }
uint64 FCEUD_GetTimeFreq(void) { return 1000; }
void FCEUD_VideoChanged() {}
void FCEUD_UpdatePPUView(int, int) {}
void FCEUD_UpdateNTView(int, bool) {}
bool FCEUD_ShouldDrawInputAids() { return false; }
FCEUFILE *FCEUD_OpenArchive(ArchiveScanRecord &, std::string &, std::string *, int *) { return 0; }
FCEUFILE *FCEUD_OpenArchive(ArchiveScanRecord &, std::string &, std::string *) { return 0; }
FCEUFILE *FCEUD_OpenArchiveIndex(ArchiveScanRecord &, std::string &, int, int *) { return 0; }
FCEUFILE *FCEUD_OpenArchiveIndex(ArchiveScanRecord &, std::string &, int) { return 0; }
ArchiveScanRecord FCEUD_ScanArchive(std::string) { return ArchiveScanRecord(); }
void RefreshThrottleFPS() {}
u32 ModernDeemphColorMap(const u8 *, const u8 *, int) { return 0; }
void GetMouseData(uint32 (&md)[3]) { md[0] = md[1] = md[2] = 0; }
unsigned int *GetKeyboard(void) { static unsigned int k[256]; return k; }
void FCEUI_UseInputPreset(int) {}
void FCEUD_TurboToggle(void) {}
void FCEUD_TurboOn(void) {}
void FCEUD_TurboOff(void) {}
void FCEUD_TraceInstruction(uint8 *, int) {}
void FCEUD_ToggleStatusIcon(void) {}
void FCEUD_SoundVolumeAdjust(int) {}
void FCEUD_SoundToggle(void) {}
int FCEUD_ShowStatusIcon(void) { return 0; }
void FCEUD_SetInput(bool, bool, ESI, ESI, ESIFC) {}
void FCEUD_SetEmulationSpeed(int) {}
void FCEUD_SaveStateAs(void) {}
bool FCEUD_PauseAfterPlayback() { return false; }
void FCEUD_NetplayText(uint8 *) {}
void FCEUD_MovieReplayFrom(void) {}
void FCEUD_MovieRecordTo(void) {}
void FCEUD_LoadStateFrom(void) {}
void FCEUD_HideMenuToggle(void) {}
const char *FCEUD_GetCompilerString() { return "headless"; }
void FCEUD_AviStop(void) {}
void FCEUD_AviRecordTo(void) {}
void FCEUD_LuaRunFrom(void) {}
void FCEUD_OnCloseGame(void) {}
void FCEUD_CmdOpen(void) {}

static uint8 parse_buttons(const char *s) {
    uint8 b = 0;
    for (; *s; s++) switch (*s) {
        case 'A': b |= 0x01; break; case 'B': b |= 0x02; break;
        case 's': b |= 0x04; break; case 'S': b |= 0x08; break;
        case 'U': b |= 0x10; break; case 'D': b |= 0x20; break;
        case 'L': b |= 0x40; break; case 'R': b |= 0x80; break;
    }
    return b;
}

extern int RAMInitOption;

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: fceux_headless ROM MOVIE FRAMES RAM_OUT [--raminit N]\n"); return 2; }
    int frames = atoi(argv[3]);
    const char *audio = NULL;
    long st0 = -1, st1 = -1;
    int mute = 0;
    for (int i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "--raminit")) RAMInitOption = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--audio")) audio = argv[++i];
        else if (!strcmp(argv[i], "--mute") && i + 1 < argc) mute = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--apustate") && i + 2 < argc) { st0 = atol(argv[i + 1]); st1 = atol(argv[i + 2]); i += 2; }
    }
    std::vector<uint8> p1(frames + 1), p2(frames + 1);
    if (strcmp(argv[2], "-")) {
        FILE *f = fopen(argv[2], "r");
        if (!f) { perror(argv[2]); return 2; }
        char line[256]; int pos = 0;
        while (fgets(line, sizeof line, f) && pos < frames) {
            if (line[0] == '#' || line[0] == '\n') continue;
            int cnt; char b1[64] = ".", b2[64] = ".";
            if (sscanf(line, "%d %63s %63s", &cnt, b1, b2) < 1) continue;
            for (int k = 0; k < cnt && pos < frames; k++) { p1[pos] = parse_buttons(b1); p2[pos++] = parse_buttons(b2); }
        }
        fclose(f);
    }
    if (!FCEUI_Initialize()) { fprintf(stderr, "init failed\n"); return 1; }
    FCEUI_SetBaseDirectory(".");
    static uint32 joy = 0;
    FCEUI_SetInput(0, SI_GAMEPAD, &joy, 0);
    FCEUI_SetInput(1, SI_GAMEPAD, &joy, 0);
    if (!FCEUI_LoadGame(argv[1], 1, true)) { fprintf(stderr, "load failed\n"); return 1; }
    FILE *af = NULL, *cf = NULL;
    if (audio) {
        FCEUI_Sound(48000);
        FCEUI_SetSoundQuality(1);
        if (mute & 1) FCEUI_SetSquare1Volume(0);
        if (mute & 2) FCEUI_SetSquare2Volume(0);
        if (mute & 4) FCEUI_SetTriangleVolume(0);
        if (mute & 8) FCEUI_SetNoiseVolume(0);
        std::string a(audio);
        af = fopen((a + ".f32").c_str(), "wb");
        cf = fopen((a + ".cnt").c_str(), "w");
    }
    FILE *out = fopen(argv[4], "wb");
    for (int fr = 0; fr < frames; fr++) {
        joy = p1[fr] | (p2[fr] << 8);
        uint8 *gfx; int32 *snd; int32 ssize;
        FCEUI_Emulate(&gfx, &snd, &ssize, 0);
        fwrite(RAM, 1, 0x800, out);
        if (fr >= st0 && fr <= st1) {
            EMUFILE_MEMORY ms;
            FCEUSS_SaveMS(&ms, 0);
            const uint8 *b = (const uint8 *)ms.buf();
            size_t n = ms.size();
            printf("%d", fr);
            const char *tags[] = {"SWCT", "CRF1", "CRF2", "FCNT", "FHCN", "LEN0", "LEN1", "LEN2", "LEN3",
                                  "E0DV", "E1DV", "E2DV", "TRIC"};
            for (int t = 0; t < 13; t++)
                for (size_t k = 0; k + 8 <= n; k++)
                    if (!memcmp(b + k, tags[t], 4)) {
                        uint32 sz = b[k + 4] | (b[k + 5] << 8) | (b[k + 6] << 16) | ((uint32)b[k + 7] << 24);
                        if (sz > 8 || k + 8 + sz > n) continue;
                        printf(" %s=", tags[t]);
                        for (uint32 j = 0; j < sz; j++) printf("%02X", b[k + 8 + sz - 1 - j]);
                        break;
                    }
            printf("\n");
        }
        if (af) {
            for (int32 i = 0; i < ssize; i++) { float v = snd[i] / 32768.0f; fwrite(&v, 4, 1, af); }
            fprintf(cf, "%d\n", (int)ssize);
        }
    }
    fclose(out);
    if (af) { fclose(af); fclose(cf); }
    FCEUI_CloseGame();
    FCEUI_Kill();
    return 0;
}
