# Package `sound` notes

Files: `sound.c`, `sound.lifted` (EA51 EA7E EC0A EC33 EC6B EC75 EC81 EC85 EC88 EC99 ECA5 ECAF ECBE).
Verified: difftest 13 pass; lockstep identical on all 7 movies.

## Names (observed unless marked)
- `$EA51 sound_reset`: writes $0F to $4015, $C0 to $4017, then zeroes sound_req[0..27] and voice byte 0 of the 28 records ($031C+8n). Leaves snd_ptr = $03FC.
- `$EA7E sound_driver` (NMI): per-frame driver, see below.
- `$ECAF load_script_ptr`: $F2/$F3 = word at dat_ECFE[$F4*2] (script address of the current slot).
- `$ECBE next_script_byte`: A = ($F2)[V5]; V5++ (V5 = voice byte 5). Also leaves Y=5.
- Command handlers (inline table $EBE6, switch on cmd-$E8):
  - $EC0A cmd $E8 `cmd_end`: sound_req[slot]=0, V0=0, V5-=1
  - $EC21 cmd $E9 `cmd_duty`: V1 = (V1&$3F)|n
  - $EC33 cmd $EA `cmd_vol6`: V1 = (V1&$C0)|n
  - $EC45 cmd $EB `cmd_vol4`: V1 = (V1&$F0)|n
  - $EC57 cmd $EC `cmd_sweep`: V2 = n
  - $EC61 cmd $ED `cmd_reg3`: V4 = n
  - $EC6B cmd $EE `cmd_reg0`: V1 = n
  - $EC75 cmd $EF `cmd_reset_loops`: $F6..$F8 = 0
  - $EC81/$EC85/$EC88 cmds $F0/$F1/$F2 `cmd_loop(n)`: operands (count, pos). $F6+n++; if count != counter then V5 = pos, else counter=0 and pos operand skipped. $EC85 is `ldx #1` + `.byte $2C` (BIT abs) skipping the `ldx #2` at $EC88.
  - $EC99 cmds $F3-$F8 `cmd_skip`: no-ops that skip 1 operand byte (V5++). They are never used in the data.
  - $ECA5 cmd $F9 `cmd_jump`: V5 = next byte (absolute offset in the script).
- RAM: `$F2/$F3` script_ptr, `$F4` snd_slot (current slot), `$F5` snd_slot_limit ($1C, or 1 while `paused`), `$F6-$F8` loop_count, `$F9-$FC` chan_busy (zero page, indexed with wrap), `$FD/$FE` temps. snd_ptr ($F0/$F1) walks the voice records.
- `$0300+n sound_req` state: 0 idle, 1 requested (set by game), 2 playing. `$031C+8n sound_voices`: V0..V7 (below).

## Voice record (8 bytes at $031C+8*slot)
V0 hardware channel (1 sq1, 2 sq2, 3 triangle, 4 noise); becomes ch+4 (5..8) once the APU registers were copied, 0 = free. V1..V4 are the values for APU regs +0..+3 of that channel (V1 volume/duty, V2 sweep, V3 period lo, V4 period hi + length). V5 script position, V6 current note duration (frames), V7 frames remaining.

## Driver algorithm ($EA7E)
1. Slot limit = 28 ($F5), or 1 if paused ($6D != 0): while paused only slot 0 is serviced (it is the pause jingle) and other slots are frozen.
2. Pass 1, slots in order (so low slot = high priority): if sound_req!=0 and V0!=0: V0>=5 -> mark channel V0-5 busy; V0 in 1..4 and channel free -> mark busy, V0+=4, copy V1..V4 to $4000+4*ch.
3. Pass 2: every channel not busy is silenced: write $10 to sq1, sq2 and noise volume regs, $00 to the triangle linear counter ($4008). Done each frame.
4. Pass 3 for each active slot: req==1 -> set 2, read the 4-5 byte header, run script; req>=2 -> V7--, at 0 run script.
5. Script interpreter ($EB88): byte b: >= $E8 command; $60 rest (no register change); $61-$E7 set duration V6 = b-$60; $00-$5F note: period = table[b>>3] >> (b&7) written to V3 / low 3 bits of V4, V0 -= 4 if >=5 (so pass 1 reloads the registers next frame). After a note or a rest V7 = V6 and the slot is finished for this frame.

## Script data format ($ECFE table, scripts at $ED36-$EF6E)
- `dat_ECE6`: 12 big-endian period words (hi, lo): 07F2 0780 0714 06AE 0643 05F4 059E 054E 0502 04BA 0476 0436 (index = note>>3, apu timer values; A,Bb... descending period = rising pitch).
- `dat_ECFE`: 28 words, little endian, address of each slot's script.
- Script: header [channel, V1, V2, V4] (+ V3 if channel==4 noise), then commands/notes terminated by $E8.
- Slot -> channel/script address (observed from the table; meaning from callers per ../docs/memory_map.md where given, derived otherwise):
  0 sq2 $EE2C pause jingle (sq2) | 1 sq1 $ED36, 2 tri $ED5D, 3 sq2 $ED88: stage-start jingle parts (3 voices) | 4 sq1 $EE9F, 5 sq2 $EEAE: extra life (slots 4,5) | 6 sq2 $EE19 powerup collected | 7 noise $EDB3 eagle destroyed | 8 sq2 $EDD5 | 9 sq2 $EE48 powerup appeared | 10 noise $EE03 enemy destroyed | 11 sq2 $EDF4 eagle destroyed | 12 tri $EE5E brick/concrete destroyed | 13 sq2 $EE56 clang off concrete | 14 sq2 $EE67 shot bounced off armour | 15 sq1 $EE12 player shot | 16 sq1 $EE95 ice slide | 17 sq2 $EE8C, 18 sq2 $EE82 (engine variants: 17/18) | 19 sq2 $EE73 | 20 noise $EE7A | 21 sq1 $EEC1, 22 sq2 $EEDF, 23 tri $EF0A hiscore jingle (3 voices; $0315 polled for done) | 24 sq1 $EF3C, 25 sq2 $EF4F, 26 tri $EF62 (3-voice jingle, likely game over) | 27 sq2 $EE3A.
  The slot numbers in the memory map ($0300 table) are confirmed to be the same indices (sound_req[n] <-> slot n); sound function labels beyond that are derived from channel and caller notes only, not tested by listening.
- $EF6F-$EFFF is $FF filler.

## QUIRKs
- (derived from the code, not tested) A new request for a lower-priority sound on a busy channel stays queued (sound_req=1, V0 in 1..4) but its script has not started yet; sound_req becomes 2 at the first script step regardless, so it starts when its own tick comes even if it lost the channel (it then plays silently: pass 1 never loads its registers while another slot holds the channel; pass 2 only silences unused channels).
- `cmd_end` (E8) does not touch the APU; silencing is by pass 2 of the next frame.
- Commands $FA-$FF index beyond the 18-entry table and would jump into code. The C leaves such a slot finished instead; the shipped scripts never contain them (not reproduced, untestable).
- Pause: slots 1..27 are not serviced and their channels get silenced each frame; note timers freeze.
- ECBE begins with a useless `lda $F4`.
- Commands $F3-$F8 and operand-skipping are present but unused by data.

## Not lifted
- $EC21 (E9), $EC45 (EB), $EC57 (EC), $EC61 (ED): no shipped script reaches them (2026-10-15: a walk of all 28 scripts following jumps and both loop exits finds only $E8 $EA $EE $EF $F0 $F1 $F2 $F9). Lifted; difftest runs them from the entry states of $EC33/$EC6B (same dispatcher, same context): all pass. Earlier note: The C implementations (`cmd_duty`, `cmd_vol4`, `cmd_sweep`, `cmd_reg3`) are used inside the driver switch and are checked only by being read against the listing.
- $ECD0: untestable; left out because the unlifted mechanical copies of $EC21/$EC45/$EC57/$EC61 still contain `JSR $ECD0` (dead code). Once those are lifted ECD0 is unreachable and can be added.

## Unknowns
- Exact in-game meaning of slots 8, 17-20, 24-27 (not cross-checked with callers).
- Whether the mech copies of unreached handlers can ever execute (no evidence they can).
