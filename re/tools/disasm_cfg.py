"""Disassembly configuration for Battle City PRG ($C000-$FFFF).

Every entry carries its justification. 'static' = derived from the code
that consumes it; 'dynamic' = confirmed by emulator coverage."""

VECTORS = {0xFFFA: 'NMI', 0xFFFC: 'RESET', 0xFFFE: 'IRQ'}

# Extra code entry points beyond vectors / coverage.
ENTRIES = {}

# Jump tables read by `LDA tbl,y / STA ptr / LDA tbl+1,y / STA ptr+1 / JMP (ptr)`.
# addr -> (entries, consumer, justification)
JUMP_TABLES = {
    0xCA69: (3, 0xCA66, 'targets start at $CA6F immediately after the 3-word table'),
    0xE306: (6, 0xE302, 'first target $E312 directly follows 6 words'),
    0xE498: (16, 0xDC4F, 'index ($A0,x>>3)&$FE = 16 entries; next table $E4B8 adjoins'),
    0xE4B8: (16, 0xDECA, 'index ($A0,x>>3)&$FE = 16 entries; next table $E4D8 adjoins'),
    0xE4D8: (5, 0xE04E, 'next table $E4E2 adjoins after 5 words'),
    0xE4E2: (5, 0xE0F8, 'parallel to $E4D8 (same index $CC,x); 6th word $A080 is not code'),
    0xE9E2: (7, 0xE9DA, 'first target $E9F0 directly follows 7 words'),
}

# Data tables whose full length is proven by the code that reads them.
# addr -> (length, justification). Only these extents are classed as data
# beyond the referenced address itself; other unreferenced bytes stay unknown.
DATA_EXTENTS = {
    0xF07A: (36 * 91, 'load_level ($F000) reads map n at $F07A+(n-1)*91, n = 1..36; 13 rows x 7 bytes each'),
    0xD36D: (28, 'from the reading code (names re-check 2026-10-15) tbl_eagle_brick: 28 (4 strings x7 at $CAF5-$CB2E)'),
    0xD389: (28, 'from the reading code (names re-check 2026-10-15) tbl_eagle_concrete: 28 (4 strings x7)'),
    0xD3D1: (4, 'from the reading code (names re-check 2026-10-15) tbl_tally_score: 4 (slot_idx<4 at $CDE8)'),
    0xD3D5: (4, 'from the reading code (names re-check 2026-10-15) tbl_dx3: 4'),
    0xD3D9: (4, 'from the reading code (names re-check 2026-10-15) tbl_dy3: 4 (index 0..3: decode_direction $E451 / banner_dir 0,1,3 in tanks.c)'),
    0xD555: (16, 'from the reading code (names re-check 2026-10-15) tbl_sprite_palette: 16 ($D540 ldy #$10)'),
    0xD565: (144, 'from the reading code (names re-check 2026-10-15) tbl_bg_palettes: 144 (9 rows'),
    0xDABB: (16, 'from the reading code (names re-check 2026-10-15) tbl_block_attr: 16 (block type is a nibble)'),
    0xDACB: (64, 'from the reading code (names re-check 2026-10-15) tbl_block_cells: 64 (16 types x 4, x=type*4+0..3)'),
    0xE003: (8, 'from the reading code (names re-check 2026-10-15) tbl_enemy_palette: 8 (AND #$07 at $DFD4)'),
    0xE46C: (4, 'from the reading code (names re-check 2026-10-15) tbl_dx: 4 (index is &3 at $DC99/$E09B)'),
    0xE470: (4, 'from the reading code (names re-check 2026-10-15) tbl_dy: 4 (index &3 at $DC99/$E09B)'),
    0xE474: (3, 'from the reading code (names re-check 2026-10-15) enemy_spawn_x: 3 (cpy #3 at $E380)'),
    0xE477: (3, 'from the reading code (names re-check 2026-10-15) enemy_spawn_y: 3 (cpy #3 at $E380)'),
    0xE47A: (2, 'from the reading code (names re-check 2026-10-15) player_spawn_x: 2 (slot<2 at $E367)'),
    0xE47C: (2, 'from the reading code (names re-check 2026-10-15) player_spawn_y: 2 (slot<2 at $E367)'),
    0xE47E: (8, 'from the reading code (names re-check 2026-10-15) tbl_slot_facing: 8 (8 tank slots)'),
    0xE486: (18, 'from the reading code (names re-check 2026-10-15) tbl_heading: 18 (max index 8+9=17)'),
    0xE4EC: (140, 'from the reading code (names re-check 2026-10-15) tbl_enemy_type: 140 (35 stages x 4: max index 34*4+3=139)'),
    0xE578: (140, 'from the reading code (names re-check 2026-10-15) tbl_enemy_counts: 140 (35 x 4)'),
    0xE8BA: (4, 'from the reading code (names re-check 2026-10-15) tbl_enemy_score: 4'),
    0xE8FA: (8, 'from the reading code (names re-check 2026-10-15) tbl_powerup_kind: 8 (AND #$07)'),
    0xEA49: (4, 'from the reading code (names re-check 2026-10-15) tbl_dx2: 4 (index &3)'),
    0xEA4D: (4, 'from the reading code (names re-check 2026-10-15) tbl_dy2: 4 (index &3)'),
}

# Subroutines that pop their return address and use the bytes after the JSR
# as an inline word table indexed by A (so the JSR never returns there).
INLINE_DISPATCH = {0xECD0}

# Per call site of an inline dispatcher: number of table entries.
INLINE_TABLES = {
    0xEBE3: (18, 'first target $EC0A directly follows 18 words at $EBE6'),
}
