/* Loading the original ROM at run time.
 *
 * The reconstruction ships no game data. The user supplies their own dump
 * (the shipped game looks for battlecity.nes next to the executable); it is
 * accepted only if its PRG and CHR hash to the exact dump this code was
 * reconstructed from, because the C code addresses tables inside PRG[]. */
#ifndef BC_ROM_H
#define BC_ROM_H
#include <stdint.h>

#define BC_ROM_FILENAME "battlecity.nes"

#define BC_PRG_SHA256 "040c74abfdf70f6eecf410651fc0a143ecab3967e321ff5793ca698aa3009d1e"
#define BC_CHR_SHA256 "a5d5a10b0fbe9b1a8b8806a06af2634d25ed993b3659131d01acdf86de9c00e9"

typedef struct {
    uint8_t chr[0x2000];   /* CHR ROM, PPU $0000-$1FFF */
    uint8_t flags6;        /* iNES header byte 6 (bit 0: 1 = vertical mirroring) */
} bc_rom_info;

/* Read an iNES file, check the PRG/CHR hashes, and fill PRG[] (machine.h).
 * Returns 0 on success; on failure returns -1 and writes a message for the
 * user into err. info may be NULL. */
int bc_rom_load(const char *path, bc_rom_info *info, char *err, int errlen);

#endif
