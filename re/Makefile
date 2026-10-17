# Battle City reconstruction -- build and verification entry points.
#
#   make                 official build (src/lifted/*.c + generated mech)
#   make agent MOD=name  private build for a work package in work/<name>/:
#                        uses src/lifted (merged) + work/<name>/*.c and
#                        *.lifted, outputs to build/<name>/ (no shared files)
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-label -Wno-unused-function
ROM := ../BattleCity\ (Japan)\ (En).nes
EMU := tools/emu/nes.c tools/emu/cpu6502.c
EMUH := tools/emu/nes.h tools/emu/cpu6502.h
HDRS := src/machine.h src/rom.h src/ram.h src/tables.h src/mech/rt.h src/lifted/common.h $(EMUH)
BASE := src/machine.c src/rom.c $(wildcard src/lifted/*.c)
GEN := tools/lift.py tools/disasm.py tools/disasm_cfg.py

# ---------------- official build ----------------
all: build/lockstep build/nesrun build/difftest

src/mech/mech.c: $(GEN) $(wildcard src/lifted/*.lifted)
	python3 tools/lift.py

src/ram.h: tools/symbols.py tools/gen_ram_h.py
	cd tools && python3 gen_ram_h.py

build/lockstep: $(BASE) src/mech/mech.c tools/harness/lockstep.c $(EMU) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tools/harness/lockstep.c $(BASE) src/mech/mech.c $(EMU)

build/difftest: $(BASE) src/mech/mech.c tools/harness/difftest.c $(EMU) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tools/harness/difftest.c $(BASE) src/mech/mech.c $(EMU)

APU := src/platform/apu.c
build/apucheck: tools/harness/apucheck.c src/rom.c $(APU) src/platform/apu.h $(EMU) $(EMUH)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tools/harness/apucheck.c src/rom.c $(APU) $(EMU) -lm

build/audiocmp: tools/harness/audiocmp.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tools/harness/audiocmp.c -lm

build/nesrun: tools/emu/nesrun.c $(EMU) $(EMUH)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tools/emu/nesrun.c $(EMU)

# ---------------- the game (SDL3 front end) ----------------
#   make game       native build (SDL3 via pkg-config) -> build/battlecity
#   make game-win   Windows build (MinGW + third_party/SDL3-devel-*-mingw)
#                   -> build/win/battlecity.exe and SDL3.dll
GAME_SRC := src/platform/main_sdl.c src/platform/apu.c $(BASE) src/mech/mech.c $(EMU)
SDL_MINGW := $(wildcard third_party/SDL3-devel-*-mingw/x86_64-w64-mingw32)
MINGW_CC ?= x86_64-w64-mingw32-gcc

game: build/battlecity
build/battlecity: $(GAME_SRC) $(HDRS) src/platform/apu.h
	@mkdir -p build
	$(CC) $(CFLAGS) $$(pkg-config --cflags sdl3) -o $@ $(GAME_SRC) $$(pkg-config --libs sdl3) -lm

game-win: build/win/battlecity.exe
build/win/battlecity.exe: $(GAME_SRC) $(HDRS) src/platform/apu.h
	@mkdir -p build/win
	$(MINGW_CC) $(CFLAGS) -DBC_NO_WEAK -I$(SDL_MINGW)/include -o $@ $(GAME_SRC) -L$(SDL_MINGW)/lib -lSDL3 -mwindows
	cp $(SDL_MINGW)/bin/SDL3.dll build/win/

# ---------------- private work-package build ----------------
ifdef MOD
W := work/$(MOD)
B := build/$(MOD)
WSRC := $(wildcard $(W)/*.c)
agent: $(B)/lockstep $(B)/difftest

$(B)/mech.c: $(GEN) $(wildcard src/lifted/*.lifted) $(wildcard $(W)/*.lifted)
	@mkdir -p $(B)
	python3 tools/lift.py --out $@ --lifted-dir $(W)

$(B)/lockstep: $(BASE) $(WSRC) $(B)/mech.c tools/harness/lockstep.c $(EMU) $(HDRS)
	$(CC) $(CFLAGS) -I$(W) -o $@ tools/harness/lockstep.c $(BASE) $(WSRC) $(B)/mech.c $(EMU)

$(B)/difftest: $(BASE) $(WSRC) $(B)/mech.c tools/harness/difftest.c $(EMU) $(HDRS)
	$(CC) $(CFLAGS) -I$(W) -o $@ tools/harness/difftest.c $(BASE) $(WSRC) $(B)/mech.c $(EMU)
endif

listing:
	python3 tools/disasm.py && python3 tools/asm_check.py && python3 tools/gen_tables.py

clean:
	rm -rf build

.PHONY: all agent listing clean game game-win
