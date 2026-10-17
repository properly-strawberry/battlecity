/* SDL3 front end: runs the C reconstruction as a game.
 *
 * The reconstruction talks to hardware only through hal_read, hal_write and
 * hal_wait_vblank (machine.h). This file implements them on top of the PPU
 * device of the reference emulator (tools/emu/nes.c, used here without its
 * CPU, exactly as build/lockstep does), and adds a window, input, sound
 * (src/platform/apu.c) and 60 Hz pacing.
 *
 * Sound timing: the reconstruction has no cycle timing inside a frame, so
 * APU writes are placed at fixed points of the frame (29780.5 CPU cycles on
 * average, counted from the end of visible line 239, as the reference
 * emulator's frames are). Measured on the original (apucheck --offsets, 4
 * user movies): the NMI's sound writes fall at cycles 1433..5352 of the
 * frame, median 2513, so the NMI runs at NMI_AT. Writes from the main
 * thread ($4015/$4017 at sound init, which also resets the frame counter)
 * fall at ~3860..3990 in about half the cases and ~9500..12800 in most of
 * the rest; they take effect at MAIN_AT, the most common value. Where the
 * original's write was elsewhere, envelope/length/sweep clocks shift
 * against the notes until the next reset (one-step differences, as against
 * FCEUX in tools/apu_state_compare.py).
 *
 * usage: battlecity [ROM.nes] [--scale N] [--nmis N] [--headless] [--mute] [--apumute M]
 *                   [--movie MOVIE --nmimap FILE] [--shot N:OUT.ppm]...
 *                   [--audio PREFIX]
 *   ROM        default: battlecity.nes next to the executable, then in the
 *              current directory. Checked against the expected dump.
 *   --headless no window, no pacing (for tests); needs --nmis.
 *   --nmis N   quit after N NMIs.
 *   --movie    play pads from a movie (nesrun format) instead of the
 *              keyboard. --nmimap is `build/nesrun ROM MOVIE FRAMES --nmipc F`
 *              output: NMI k reads the pads of the k-th frame that took an
 *              NMI in the original, as in lockstep.
 *   --shot N:F write the picture shown after NMI N as a PPM.
 *   --audio P  also write the sound as P.f32 (mono float, 48 kHz), P.cnt
 *              (samples per wait for vblank) and, with --movie, P.map (the
 *              original's frame for each wait, -1 if NMI was off), for
 *              tools/harness/audiocmp.c --map.
 *   --mute     no sound output.
 *   --apumute M  (tests) silence APU channels: bit 0/1 pulse 1/2, 2 triangle, 3 noise.
 *
 * Keys (player 1): arrows, X = A (fire), Z = B (fire), Enter = Start,
 * Right Shift = Select. Gamepads: the first two are players 1 and 2.
 * Esc quits. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../machine.h"
#include "../rom.h"
#include "../../tools/emu/nes.h"
#include "apu.h"

#define SCR_W 256
#define SCR_H 240
#define FRAME_NS 16639267ULL   /* NTSC: 1e9 / 60.0988 */
#define AUDIO_RATE 48000
#define FRAME_CYCLES_X3 (341 * 262)   /* CPU cycles per frame, times 3 */
#define NMI_AT  2513                  /* CPU cycle of the frame where the NMI's APU writes land */
#define MAIN_AT 3984                  /* ... and where the main thread's land (see above) */

static nes *dev;
static int headless, scale = 3;
static long nmi_count, nmis_max = -1;
static uint8_t rgb[SCR_W * SCR_H * 3];

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_Gamepad *pads[2];
static Uint64 next_frame_ns;

/* movie playback */
static uint8_t *movie1, *movie2;
static long movie_len;
static long *nmi_frame;   /* NMI k (1-based) -> movie frame */
static long nmi_frames;

static struct { long nmi; const char *path; } shots[64];
static int nshots;

/* sound */
static apu snd;
static SDL_AudioStream *audio;
static int muted;
static long cycle_carry_x3;           /* fractional CPU cycles carried between frames */
static long frame_len = -1;           /* CPU cycles in the current frame (-1: none started) */
static long frame_pos;                /* CPU cycles of it already rendered */
static long chunk_ref_frame = -1;     /* original's frame for the current chunk (--audio) */
static int chunk_n;                   /* samples in the current chunk */
static float abuf[2048];
static FILE *audio_f32, *audio_cnt, *audio_map;

/* ---- HAL ---- */
uint8_t hal_read(uint16_t a) { return nes_io_read(dev, a); }
void hal_write(uint16_t a, uint8_t v) {
    if (a == 0x4014) memcpy(dev->ram, RAM, 0x800);   /* OAM DMA source */
    else if (a >= 0x4000 && a <= 0x4017 && a != 0x4016) apu_write(&snd, a, v);
    nes_io_write(dev, a, v);
}

static void write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
    fwrite(rgb, 1, sizeof rgb, f);
    fclose(f);
}

static uint8_t keyboard_pad(void) {
    const bool *k = SDL_GetKeyboardState(NULL);
    uint8_t b = 0;
    if (k[SDL_SCANCODE_X]) b |= 0x01;
    if (k[SDL_SCANCODE_Z]) b |= 0x02;
    if (k[SDL_SCANCODE_RSHIFT]) b |= 0x04;
    if (k[SDL_SCANCODE_RETURN]) b |= 0x08;
    if (k[SDL_SCANCODE_UP]) b |= 0x10;
    if (k[SDL_SCANCODE_DOWN]) b |= 0x20;
    if (k[SDL_SCANCODE_LEFT]) b |= 0x40;
    if (k[SDL_SCANCODE_RIGHT]) b |= 0x80;
    return b;
}

static uint8_t gamepad_pad(SDL_Gamepad *g) {
    if (!g) return 0;
    uint8_t b = 0;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_SOUTH)) b |= 0x01;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_WEST)) b |= 0x02;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK)) b |= 0x04;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START)) b |= 0x08;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP)) b |= 0x10;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) b |= 0x20;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) b |= 0x40;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) b |= 0x80;
    return b;
}

static void poll_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) exit(0);
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) exit(0);
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
            for (int i = 0; i < 2; i++)
                if (!pads[i]) { pads[i] = SDL_OpenGamepad(e.gdevice.which); break; }
        }
        if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
            for (int i = 0; i < 2; i++)
                if (pads[i] && SDL_GetGamepadID(pads[i]) == e.gdevice.which) { SDL_CloseGamepad(pads[i]); pads[i] = NULL; }
        }
    }
}

static void present(void) {
    SDL_UpdateTexture(tex, NULL, rgb, SCR_W * 3);
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    Uint64 now = SDL_GetTicksNS();
    if (next_frame_ns > now) SDL_DelayNS(next_frame_ns - now);
    else next_frame_ns = now;   /* fell behind: do not try to catch up */
    next_frame_ns += FRAME_NS;
    /* The sound card's clock and ours drift apart slowly: keep about three
     * frames of sound queued by stretching or shortening frames slightly
     * (at most 0.5 ms each). */
    if (audio) {
        long queued = SDL_GetAudioStreamQueued(audio) / (long)sizeof(float);
        long target = 3 * AUDIO_RATE / 60;
        long d = (queued - target) * 1000000000LL / AUDIO_RATE / 8;
        if (d > 500000) d = 500000;
        if (d < -500000) d = -500000;
        next_frame_ns += d;
    }
}

/* Advance the APU to cycle `to` of the current frame. */
static void audio_to(long to) {
    if (to <= frame_pos) return;
    int n = apu_run(&snd, to - frame_pos, abuf, (int)(sizeof abuf / sizeof *abuf));
    frame_pos = to;
    if (audio && !muted) SDL_PutAudioStreamData(audio, abuf, n * (int)sizeof(float));
    if (audio_f32) fwrite(abuf, sizeof(float), n, audio_f32);
    chunk_n += n;
}
/* Finish the current frame's sound and start the next frame. */
static void audio_next_frame(void) {
    if (frame_len >= 0) {
        audio_to(frame_len);
        if (audio_f32) {
            fprintf(audio_cnt, "%d\n", chunk_n);
            if (audio_map) fprintf(audio_map, "%ld\n", chunk_ref_frame);
        }
    }
    long x3 = FRAME_CYCLES_X3 + cycle_carry_x3;
    frame_len = x3 / 3;
    cycle_carry_x3 = x3 % 3;
    frame_pos = 0;
    chunk_n = 0;
    chunk_ref_frame = -1;
}

/* Time passes only while the program waits (same model as lockstep): the
 * pre-render line latches the finished frame, which is shown; then vblank
 * starts and the NMI runs if PPUCTRL enables it. */
void hal_wait_vblank(void) {
    nes_prerender(dev);
    nes_render(dev, rgb);
    for (int i = 0; i < nshots; i++)
        if (shots[i].nmi == nmi_count) write_ppm(shots[i].path);
    if (!headless) { poll_events(); present(); }
    audio_next_frame();
    audio_to(NMI_AT);
    nes_vblank_start(dev);
    if (dev->ctrl & 0x80) {
        if (nmis_max >= 0 && nmi_count >= nmis_max) {
            if (audio_f32) { fclose(audio_f32); fclose(audio_cnt); if (audio_map) fclose(audio_map); }
            exit(0);
        }
        nmi_count++;
        if (movie1 && nmi_count <= nmi_frames) chunk_ref_frame = nmi_frame[nmi_count - 1];
        if (movie1) {
            long f = nmi_count <= nmi_frames ? nmi_frame[nmi_count - 1] : movie_len;
            dev->pad[0] = f < movie_len ? movie1[f] : 0;
            dev->pad[1] = f < movie_len ? movie2[f] : 0;
        } else if (!headless) {
            dev->pad[0] = keyboard_pad() | gamepad_pad(pads[0]);
            dev->pad[1] = gamepad_pad(pads[1]);
        }
        bc_nmi();
    }
    audio_to(MAIN_AT);   /* the main thread's APU writes land here */
}

void mech_brk(uint16_t at) { fprintf(stderr, "BRK at $%04X\n", at); exit(3); }
void mech_bad_dispatch(uint16_t addr) { fprintf(stderr, "bad dispatch to $%04X\n", addr); exit(3); }

/* ---- movie ---- */
static uint8_t parse_buttons(const char *s) {
    uint8_t b = 0;
    for (; *s; s++) switch (*s) {
        case 'A': b |= 0x01; break; case 'B': b |= 0x02; break;
        case 's': b |= 0x04; break; case 'S': b |= 0x08; break;
        case 'U': b |= 0x10; break; case 'D': b |= 0x20; break;
        case 'L': b |= 0x40; break; case 'R': b |= 0x80; break;
    }
    return b;
}
static void load_movie(const char *path, const char *mappath) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    char line[256];
    long cap = 1 << 16;
    movie1 = malloc(cap); movie2 = malloc(cap);
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        int cnt; char b1[64] = ".", b2[64] = ".";
        if (sscanf(line, "%d %63s %63s", &cnt, b1, b2) < 1) continue;
        for (int i = 0; i < cnt; i++) {
            if (movie_len == cap) { cap *= 2; movie1 = realloc(movie1, cap); movie2 = realloc(movie2, cap); }
            movie1[movie_len] = parse_buttons(b1); movie2[movie_len++] = parse_buttons(b2);
        }
    }
    fclose(f);
    f = fopen(mappath, "r");
    if (!f) { perror(mappath); exit(2); }
    long fr; unsigned pc; cap = 1 << 16;
    nmi_frame = malloc(cap * sizeof *nmi_frame);
    while (fscanf(f, "%ld %x", &fr, &pc) == 2) {
        if (!pc) continue;
        if (nmi_frames == cap) { cap *= 2; nmi_frame = realloc(nmi_frame, cap * sizeof *nmi_frame); }
        nmi_frame[nmi_frames++] = fr;
    }
    fclose(f);
}

/* ---- start-up ---- */
static int try_rom(const char *path, bc_rom_info *rom, char *err, int errlen) {
    return bc_rom_load(path, rom, err, errlen);
}

int main(int argc, char **argv) {
    const char *rom_path = NULL, *movie = NULL, *nmimap = NULL, *audio_prefix = NULL;
    int apumute = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--nmis") && i + 1 < argc) nmis_max = atol(argv[++i]);
        else if (!strcmp(argv[i], "--movie") && i + 1 < argc) movie = argv[++i];
        else if (!strcmp(argv[i], "--nmimap") && i + 1 < argc) nmimap = argv[++i];
        else if (!strcmp(argv[i], "--audio") && i + 1 < argc) audio_prefix = argv[++i];
        else if (!strcmp(argv[i], "--mute")) muted = 1;
        else if (!strcmp(argv[i], "--apumute") && i + 1 < argc) apumute = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc && nshots < 64) {
            char *p = argv[++i], *c = strchr(p, ':');
            if (!c) { fprintf(stderr, "--shot N:FILE\n"); return 2; }
            shots[nshots].nmi = atol(p); shots[nshots++].path = c + 1;
        } else if (argv[i][0] != '-' && !rom_path) rom_path = argv[i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (headless && nmis_max < 0) { fprintf(stderr, "--headless needs --nmis N\n"); return 2; }
    if (movie && !nmimap) { fprintf(stderr, "--movie needs --nmimap (build/nesrun ... --nmipc)\n"); return 2; }
    if (scale < 1) scale = 1;

    static bc_rom_info rom;
    char err[512] = "";
    int ok = -1;
    if (rom_path) ok = try_rom(rom_path, &rom, err, sizeof err);
    else {
        const char *base = SDL_GetBasePath();
        char p[4096];
        if (base) {
            snprintf(p, sizeof p, "%s%s", base, BC_ROM_FILENAME);
            ok = try_rom(p, &rom, err, sizeof err);
        }
        if (ok) ok = try_rom(BC_ROM_FILENAME, &rom, err, sizeof err);
    }
    if (ok) {
        if (!rom_path && strncmp(err, "cannot open", 11) == 0)
            snprintf(err, sizeof err, "No ROM found. Put your own Battle City (Japan) ROM next to the "
                     "program as " BC_ROM_FILENAME ", or give its path on the command line.");
        fprintf(stderr, "%s\n", err);
        if (!headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Battle City", err, NULL);
        return 2;
    }
    if (movie) load_movie(movie, nmimap);
    apu_init(&snd, AUDIO_RATE);
    snd.mute = (uint8_t)apumute;
    if (audio_prefix) {
        char p[1024];
        snprintf(p, sizeof p, "%s.f32", audio_prefix); audio_f32 = fopen(p, "wb");
        snprintf(p, sizeof p, "%s.cnt", audio_prefix); audio_cnt = fopen(p, "w");
        if (movie) { snprintf(p, sizeof p, "%s.map", audio_prefix); audio_map = fopen(p, "w"); }
        if (!audio_f32 || !audio_cnt) { perror(audio_prefix); return 2; }
    }

    if (!headless) {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
        if (!SDL_CreateWindowAndRenderer("Battle City", SCR_W * scale, SCR_H * scale,
                                         SDL_WINDOW_RESIZABLE, &win, &ren)) {
            fprintf(stderr, "window: %s\n", SDL_GetError());
            return 1;
        }
        SDL_SetRenderVSync(ren, 0);   /* paced by present() at the NES rate */
        SDL_SetRenderLogicalPresentation(ren, SCR_W, SCR_H, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        next_frame_ns = SDL_GetTicksNS();
        if (!muted) {
            SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, AUDIO_RATE };
            audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
            if (audio) SDL_ResumeAudioStreamDevice(audio);
            else fprintf(stderr, "no sound: %s\n", SDL_GetError());
        }
    }

    dev = nes_new(PRG, rom.chr);
    dev->horizontal = !(rom.flags6 & 1);   /* iNES flags6 bit 0 = 0: horizontal */
    dev->iolog_enabled = 0;
    nes_power(dev);
    memcpy(RAM, dev->ram, 0x800);   /* FCEUX power-on pattern, as in the tests */
    bc_reset();                     /* never returns */
    return 0;
}
