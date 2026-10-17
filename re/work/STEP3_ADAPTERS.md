# Adapter map (generated)

Each `sub_XXXX` adapter maps registers to its C function. Use it to turn `CALL_MECH(sub_XXXX)` into a direct call.

| adapter | file | C function | adapter body (register convention) |
|---|---|---|---|
| sub_C070 | flow.c | game_main | `game_main();` |
| sub_C295 | flow.c | redraw_screen | `redraw_screen();` |
| sub_C2A2 | flow_hud.c | clear_pads_if_base_lost | `clear_pads_if_base_lost(); A = (base_state != 0x80) ? 0 : base_state; fC = (base_state >= 0x80); if (base_state == 0x80) { fZ = 1; fN = 0; } else SZN(A);` |
| sub_C2B3 | flow_hud.c | new_game | `new_game(); A = 0; SZN(A);` |
| sub_C2BD | flow_hud.c | reset_game_state | `reset_game_state(); A = 0; SZN(A);` |
| sub_C2E6 | flow_hud.c | main_loop | `main_loop();` |
| sub_C31D | flow_hud.c | palette_cycle | `uint8_t f = frame_counter & 0x3F; palette_cycle(); if (f == 0) A = 2; else if (f == 0x20) A = 1; else A = f; SZN(A);` |
| sub_C331 | flow.c | start_stage | `start_stage();` |
| sub_C3B5 | flow.c | attract_setup | `attract_setup();` |
| sub_C41D | flow.c | attract_run | `attract_run();` |
| sub_C44B | flow.c | hiscore_screen | `hiscore_screen();` |
| sub_C49C | screens.c | credits_screen | `credits_screen();` |
| sub_C567 | screens.c | wait_64_frames | `wait_64_frames();` |
| sub_C575 | screens.c | credits_sprite_drop | `credits_sprite_drop();` |
| sub_C5B0 | screens.c | draw_credit_star | `draw_credit_star();` |
| sub_C5D9 | screens.c | game_over_screen | `game_over_screen();` |
| sub_C642 | flow_hud.c | attract_input | `attract_input();` |
| sub_C6C6 | flow_hud.c | draw_cursor_block | `draw_cursor_block();` |
| sub_C6D2 | flow_hud.c | construction_cursor_input | `construction_cursor_input();` |
| sub_C71E | flow_hud.c | clear_kill_counts | `clear_kill_counts(); A = 0; X = 0xFF; SZN(X);` |
| sub_C728 | flow_hud.c | stage_over | `A = stage_over(); SZN(A); fC = 0;` |
| sub_C7AB | flow.c | title_scroll | `title_scroll();` |
| sub_C7C8 | flow_hud.c | draw_hud | `draw_hud();` |
| sub_C830 | flow_hud.c | draw_hud_labels | `draw_hud_labels();` |
| sub_C859 | screens.c | draw_hud_flag | `draw_hud_flag();` |
| sub_C894 | flow_hud.c | hud_enemy_cell | `uint8_t c, r; hud_enemy_cell(A, &c, &r); X = c; Y = r;` |
| sub_C8A2 | flow_hud.c | draw_enemy_icon | `draw_enemy_icon(A);` |
| sub_C8B1 | flow_hud.c | erase_enemy_icon | `erase_enemy_icon(A);` |
| sub_C8C0 | flow_hud.c | draw_enemy_column | `draw_enemy_column();` |
| sub_C8D0 | flow_hud.c | clamp_cursor | `clamp_cursor(); A = tank_y[0]; fC = (A >= 0x18); SZN(A);` |
| sub_C8F9 | sprites.c | draw_pause_banner | `draw_pause_banner();` |
| sub_C947 | sprites.c | draw_game_over_banner | `draw_game_over_banner();` |
| sub_C972 | sprites.c | game_over_slide | `game_over_slide();` |
| sub_C9B0 | flow_hud.c | clear_playfield | `clear_playfield();` |
| sub_C9C0 | flow.c | title_menu | `title_menu();` |
| sub_CA6F | flow.c | - | `max_tank_slot = 5; CALL_MECH(sub_C2B3);` |
| sub_CA74 | flow.c | - | `max_tank_slot = 7; CALL_MECH(sub_C2B3);` |
| sub_CA7E | flow.c | - | `max_tank_slot = 7;` |
| sub_CA85 | flow_hud.c | menu_cursor_y | `menu_cursor_y(); A = tank_y[0]; SZN(A);` |
| sub_CA91 | screens.c | stage_intro_text | `stage_intro_text();` |
| sub_CAF5 | bullets.c | build_eagle_brick | `uint8_t y; build_eagle_brick(&y); Y = y; X = vram_queue_len; A = 0xFF; SZN(A);` |
| sub_CB5D | flow_hud.c | load_custom_level | `load_custom_level();` |
| sub_CB9E | bullets.c | build_eagle_concrete | `uint8_t y; build_eagle_concrete(&y); Y = y; X = vram_queue_len; A = 0xFF; SZN(A);` |
| sub_CC08 | bullets.c | draw_destroyed_eagle | `Y = draw_destroyed_eagle(); A = 0; SZN(Y);` |
| sub_CC27 | screens.c | upload_attributes | `upload_attributes();` |
| sub_CC5C | flow_hud.c | queue_map_row | `queue_map_row(Y); Y = 0x20;` |
| sub_CC90 | screens.c | curtain_close | `curtain_close();` |
| sub_CCB2 | screens.c | curtain_open | `curtain_open();` |
| sub_CCD4 | screens.c | stage_tally | `stage_tally();` |
| sub_CEF7 | screens.c | build_tally_screen | `build_tally_screen();` |
| sub_D0B8 | sprites.c | draw_tally_sprites | `draw_tally_sprites();` |
| sub_D0D9 | gfx_text.c | set_tally_attributes | `set_tally_attributes(); A = 0x05; SZN(A);` |
| sub_D130 | sprites.c | draw_tally_tank | `draw_tally_tank(A, Y);` |
| sub_D138 | gfx_text.c | check_extra_life | `check_extra_life();` |
| sub_D16A | screens.c | clear_screen | `clear_screen();` |
| sub_D17F | screens.c | draw_title_logo | `draw_title_logo();` |
| sub_D276 | screens.c | delay_frames | `delay_frames(X); X = 0;` |
| sub_D400 | flow.c | nmi | `nmi();` |
| sub_D44D | tanks.c | rng_core | `uint8_t c, v; A = rng_core(&c, &v); fC = c; fV = v; SZN(A);` |
| sub_D467 | flow.c | rendering_on | `rendering_on(); A = 0xB0;` |
| sub_D470 | flow.c | rendering_off | `rendering_off(); A = 0x06;` |
| sub_D47E | gfx_text.c | clear_terrain_map | `clear_terrain_map(); A = 0; X = 0; SZN(0);` |
| sub_D491 | flow.c | init_system | `init_system();` |
| sub_D4E3 | gfx_text.c | write_boot_signature | `write_boot_signature(); A = ROM8(0xC040); X = 0xFF; fN = 1; fZ = 0;` |
| sub_D4EF | gfx_text.c | boot_signature_valid | `uint8_t x, c; A = boot_signature_valid(&x, &c); X = x; fC = c; SZN(A);` |
| sub_D502 | flow.c | load_initial_palettes | `load_initial_palettes();` |
| sub_D50E | gfx_text.c | load_bg_palette | `fC = (A >> 4) & 1; X = load_bg_palette(A); A = 0; Y = 0; SZN(A);` |
| sub_D53E | gfx_text.c | load_sprite_palettes | `load_sprite_palettes(); A = ROM8(TBL_SPRITE_PALETTE + 15); X = 16; Y = 0; SZN(0);` |
| sub_D5F5 | flow.c | wait_vblank_flag | `wait_vblank_flag();` |
| sub_D5FB | cells.c | cell_to_addr | `uint8_t lo; A = cell_to_addr(X, Y, &lo); Y = lo;` |
| sub_D612 | cells.c | queue_block_palette | `queue_block_palette(X, Y);` |
| sub_D635 | cells.c | set_block_palette | `Y = set_block_palette(X, Y);` |
| sub_D684 | cells.c | pal_shift_in | `A = pal_shift_in(A);` |
| sub_D689 | cells.c | read_joypads | `read_joypads(); Y = 0;` |
| sub_D6B3 | cells.c | draw_tiles | `Y = draw_tiles(X, Y);` |
| sub_D6DD | cells.c | draw_hud_number | `draw_hud_number(X, Y);` |
| sub_D706 | cells.c | coords_to_map | `coords_to_map(X, Y); X >>= 3; Y = 0;` |
| sub_D709 | cells.c | map_ptr_from_cell | `map_ptr_from_cell(X, Y); Y = 0;` |
| sub_D713 | cells.c | - | `Y >>= 3; X >>= 3;` |
| sub_D71E | cells.c | locate_pixel | `locate_pixel(X, Y); X >>= 3; Y = 0;` |
| sub_D725 | cells.c | quarter_mask | `quarter_mask();` |
| sub_D73C | cells.c | test_quarter | `A = test_quarter(Y); SZN(A);` |
| sub_D743 | cells.c | clear_quarter | `clear_quarter(X, Y); X = cell_x;` |
| sub_D74D | cells.c | clear_quarter_quiet | `clear_quarter_quiet(Y);` |
| sub_D764 | cells.c | set_quarter_quiet | `set_quarter_quiet(Y);` |
| sub_D771 | cells.c | write_cell_direct | `write_cell_direct(Y);` |
| sub_D784 | cells.c | write_cell | `write_cell(A, X, Y); X = cell_x;` |
| sub_D7AA | cells.c | ptr_advance | `ptr_advance(A);` |
| sub_D7B4 | gfx_text.c | render_playfield | `render_playfield(); A = 8; Y = 0; fC = 1; SZN(0);` |
| sub_D7CC | gfx_text.c | reset_terrain_map | `reset_terrain_map(); A = 0; X = tmp_x; Y = 0xFF; fZ = 1; fN = 0;` |
| sub_D80B | gfx_text.c | draw_block | `draw_block(A, X, Y); X = cell_x; A = 0; /* other regs not consumed by callers (contract out is empty) */` |
| sub_D85E | gfx_text.c | draw_glyph_bricks | `draw_glyph_bricks(A, X, Y); A = 0;` |
| sub_D8D2 | gfx_text.c | draw_text_bricks | `draw_text_bricks(); A = 0xFF; Y = text_index; fC = 1; SZN(A);` |
| sub_D8F6 | flow.c | wait_frame | `uint8_t f = frame_counter; wait_frame(); A = f;` |
| sub_D8FD | gfx_text.c | flush_vram_queue | `X = flush_vram_queue(); A = 0; fC = 1; SZN(0);` |
| sub_D934 | gfx_text.c | align_number | `X = align_number(X, Y); Y = ptr_lo; A = 0; SZN(0);` |
| sub_D951 | gfx_text.c | draw_hiscore_bricks | `draw_hiscore_bricks(); A = 0;` |
| sub_D97D | gfx_text.c | update_hiscore | `uint8_t x; Y = update_hiscore(&x); X = x; A = 0; /* X/A not consumed by callers (out = Y only) */` |
| sub_D9BE | gfx_text.c | add_digits | `add_digits(X); X = (uint8_t)(X * 8 + 6 - 7); Y = 0xFF;` |
| sub_D9E1 | gfx_text.c | set_score_popup | `set_score_popup(A); X = 0x35;` |
| sub_D9FE | gfx_text.c | clear_digits | `clear_digits(X); A = 0xFF; SZN(A);` |
| sub_DA13 | gfx_text.c | number_to_digits | `number_to_digits(A); X = 0x35; A = digits[38];` |
| sub_DA2B | sprites.c | draw_sprite_oam | `draw_sprite_oam(X, Y);` |
| sub_DA64 | sprites.c | draw_sprite | `draw_sprite(A, X, Y);` |
| sub_DA73 | sprites.c | draw_sprite16 | `draw_sprite16(A, X, Y);` |
| sub_DA7B | sprites.c | draw_sprite16_at | `draw_sprite16_at(X, Y);` |
| sub_DA93 | gfx_text.c | clear_oam | `clear_oam(); X = 4; A = 0xF0; fZ = 1; fN = 0;` |
| sub_DAAF | tanks.c | sign_of | `A = sign_of(A, fZ, fC);` |
| sub_DB0B | tanks.c | update_engine_sound | `update_engine_sound();` |
| sub_DB38 | tanks.c | player_is_driving | `A = player_is_driving(X); SZN(A);` |
| sub_DB48 | tanks.c | spawn_enemy | `spawn_enemy();` |
| sub_DB75 | tanks.c | player_control | `player_control();` |
| sub_DBF0 | tanks.c | - | `` |
| sub_DBF1 | tanks.c | move_all_tanks | `move_all_tanks();` |
| sub_DC3D | tanks.c | move_tank | `move_tank(X);` |
| sub_DC52 | tanks.c | tank_turning | `tank_turning(X);` |
| sub_DC7C | tanks.c | tank_drive | `tank_drive(X);` |
| sub_DD48 | tanks.c | tank_random_turn | `tank_random_turn(X);` |
| sub_DD6E | tanks.c | clamp_probe_x | `A = clamp_probe_x(A); fC = 0; SZN(A);` |
| sub_DD76 | tanks.c | clamp_probe_y | `A = clamp_probe_y(A); fC = 0; SZN(A);` |
| sub_DD7E | tanks.c | seek_player1 | `seek_player1(X);` |
| sub_DD89 | tanks.c | seek_player2 | `seek_player2(X);` |
| sub_DD94 | tanks.c | seek_eagle | `seek_eagle(X);` |
| sub_DDA2 | tanks.c | heading_to_target | `A = heading_to_target(X);` |
| sub_DDEA | tanks.c | tank_dying | `tank_dying(X);` |
| sub_DE46 | tanks.c | arm_game_over_banner | `arm_game_over_banner(); A = 0; SZN(A);` |
| sub_DE55 | tanks.c | tank_spawn_phase1 | `tank_spawn_phase1(X);` |
| sub_DE64 | tanks.c | tank_spawn_phase2 | `tank_spawn_phase2(X);` |
| sub_DE72 | tanks.c | pick_direction | `pick_direction(X); A = tank_state_at(X); fC = 0; SZN(A);` |
| sub_DEA6 | sprites.c | draw_all_tanks | `draw_all_tanks();` |
| sub_DEB8 | sprites.c | draw_tank | `draw_tank(X);` |
| sub_DECD | sprites.c | draw_explosion | `draw_explosion(X);` |
| sub_DEE2 | sprites.c | draw_explosion_sprite | `draw_explosion_sprite(A, X, Y);` |
| sub_DEF0 | sprites.c | draw_explosion_frame | `draw_explosion_frame(A, X, Y);` |
| sub_DEFD | sprites.c | draw_death_tail | `draw_death_tail(X);` |
| sub_DF33 | sprites.c | draw_explosion_last | `draw_explosion_last(X);` |
| sub_DF46 | sprites.c | draw_explosion_big | `draw_explosion_big();` |
| sub_DF99 | sprites.c | big_explosion_tile | `uint8_t y; X = big_explosion_tile(A, &y); Y = y;` |
| sub_DFB6 | sprites.c | draw_tank_body | `draw_tank_body(X);` |
| sub_E00B | sprites.c | draw_spawn_twinkle | `draw_spawn_twinkle(X);` |
| sub_E02E | bullets.c | update_bullets | `update_bullets(); X = 0; A = 0; SZN(A);` |
| sub_E03C | bullets.c | bullet_dispatch | `bullet_dispatch(X);` |
| sub_E051 | bullets.c | bullet_move | `bullet_move(X); A = 0; SZN(A);` |
| sub_E063 | bullets.c | bullet_step | `bullet_step(X, Y); A = ZPX(B_Y, X); SZN(A);` |
| sub_E076 | bullets.c | bullet_explode_tick | `bullet_explode_tick(X); A = ZPX(B_STATE, X); SZN(A);` |
| sub_E08C | bullets.c | fire_bullet | `fire_bullet(X); A = ZPX(B_FLAGS, X); SZN(A);` |
| sub_E0D8 | sprites.c | draw_bullets | `draw_bullets();` |
| sub_E0E6 | sprites.c | draw_bullet | `draw_bullet(X);` |
| sub_E0FB | sprites.c | draw_bullet_sprite | `draw_bullet_sprite(X);` |
| sub_E112 | sprites.c | draw_bullet_explosion | `draw_bullet_explosion(X);` |
| sub_E122 | bullets.c | player_fire | `player_fire(); X = 0xFF; A = 0xFF; SZN(A);` |
| sub_E162 | bullets.c | enemy_fire | `enemy_fire(); X = 1; A = 1; SZN(A);` |
| sub_E181 | tanks.c | update_tank_terrain | `update_tank_terrain(); A = 0; X = 0xFF;` |
| sub_E1F3 | tanks.c | mark_cell_occupied | `A = mark_cell_occupied(Y); SZN(A);` |
| sub_E1FA | tanks.c | clear_tank_occupancy | `clear_tank_occupancy(); A = 0; X = 0xFF;` |
| sub_E234 | tanks.c | unmark_cell | `A = unmark_cell(Y); SZN(A);` |
| sub_E23B | sprites.c | draw_powerup | `draw_powerup();` |
| sub_E27C | sprites.c | draw_shield | `draw_shield();` |
| sub_E2A9 | bullets.c | shovel_and_base | `shovel_and_base(); A = 0; SZN(A);` |
| sub_E312 | bullets.c | eagle_small_boom | `eagle_small_boom(0xF1);` |
| sub_E317 | bullets.c | eagle_small_boom | `eagle_small_boom(0xF5);` |
| sub_E31C | bullets.c | eagle_small_boom | `eagle_small_boom(0xF9);` |
| sub_E328 | bullets.c | draw_eagle_part | `draw_eagle_part((uint8_t)(A + explosion_tile_offset), X, Y);` |
| sub_E32E | bullets.c | eagle_big_boom | `explosion_tile_offset = 0x00; eagle_big_boom();` |
| sub_E336 | bullets.c | eagle_big_boom | `explosion_tile_offset = 0x10; eagle_big_boom();` |
| sub_E33E | bullets.c | eagle_big_boom | `eagle_big_boom();` |
| sub_E363 | tanks.c | spawn_tank | `spawn_tank(X);` |
| sub_E3B8 | tanks.c | init_tank_type | `init_tank_type(X); A = 0; SZN(A);` |
| sub_E409 | tanks.c | clear_bullet_slots | `clear_bullet_slots(); A = 0; X = 0xFF; fZ = 0; fN = 1;` |
| sub_E413 | tanks.c | clear_tank_slots | `clear_tank_slots(); A = 0; X = 0xFF; fZ = 0; fN = 1;` |
| sub_E420 | tanks.c | set_tank_mode | `A = set_tank_mode(A, X); SZN(A);` |
| sub_E42B | tanks.c | load_enemy_counts | `load_enemy_counts();` |
| sub_E451 | tanks.c | decode_direction | `A = decode_direction(A); SZN(A);` |
| sub_E604 | bullets.c | bullet_vs_terrain | `bullet_vs_terrain(); X = 0xFF; A = 0; SZN(A); fC = 0;` |
| sub_E693 | bullets.c | probe_terrain | `uint8_t yo; A = probe_terrain(X, Y, &yo); Y = yo; SZN(A);` |
| sub_E69A | bullets.c | hit_terrain | `uint8_t yo; A = hit_terrain(Y, &yo); Y = yo; SZN(A);` |
| sub_E70C | bullets.c | bullet_vs_tank | `bullet_vs_tank(); X = 0; A = 0; SZN(A);` |
| sub_E8BE | bullets.c | spawn_powerup | `/* the first rng call needs X preserved; rng restores X itself */ spawn_powerup(); X = slot_idx; Y = cell_w; A = 0; SZN(Y);` |
| sub_E902 | bullets.c | powerup_coord | `A = powerup_coord(A); SZN(A); fC = 0;` |
| sub_E910 | bullets.c | bullet_vs_bullet | `bullet_vs_bullet(); X = 0xFF; A = 0xFF; SZN(A);` |
| sub_E972 | bullets.c | powerup_pickup | `uint8_t xo = X; /* the early returns leave X unchanged */ if (powerup_pickup(&xo)) S = (uint8_t)(S + 2); X = xo;` |
| sub_E9F0 | bullets.c | powerup_helmet | `powerup_helmet(X); A = 0x0A; SZN(A);` |
| sub_E9F5 | bullets.c | powerup_timer | `powerup_timer(); A = 0x0A; SZN(A);` |
| sub_E9FB | bullets.c | powerup_shovel | `uint8_t before = base_state; X = powerup_shovel(X); if (before & 0x80) { A = 0x14; SZN(A); } else { A = before; SZN(A); }` |
| sub_EA07 | bullets.c | powerup_star | `uint8_t before = RAM[0x101 + X]; powerup_star(X); A = RAM[0x101 + X]; if (before == 0x60) A = 0x60; SZN(A); fC = (before == 0x60) ? 1 : (uint8_t)((before + 0x20) > 0xFF);` |
| sub_EA17 | bullets.c | powerup_grenade | `powerup_grenade(); Y = 1; A = 1; SZN(A); fC = 1;` |
| sub_EA3E | bullets.c | powerup_extra_life | `powerup_extra_life(X); A = 1; SZN(A);` |
| sub_EA48 | bullets.c | - | `` |
| sub_EA51 | sound.c | sound_reset | `sound_reset(); A = 0; X = SLOT_COUNT; Y = 0; fC = 1; fZ = 1; fN = 0;` |
| sub_EA7E | sound.c | sound_driver | `sound_driver();` |
| sub_EC0A | sound.c | cmd_end | `cmd_standalone(cmd_end);` |
| sub_EC21 | sound.c | cmd_duty | `cmd_standalone(cmd_duty);` |
| sub_EC33 | sound.c | cmd_vol6 | `cmd_standalone(cmd_vol6);` |
| sub_EC45 | sound.c | cmd_vol4 | `cmd_standalone(cmd_vol4);` |
| sub_EC57 | sound.c | cmd_sweep | `cmd_standalone(cmd_sweep);` |
| sub_EC61 | sound.c | cmd_reg3 | `cmd_standalone(cmd_reg3);` |
| sub_EC6B | sound.c | cmd_reg0 | `cmd_standalone(cmd_reg0);` |
| sub_EC75 | sound.c | cmd_reset_loops | `cmd_standalone(cmd_reset_loops);` |
| sub_EC81 | sound.c | cmd_loop0 | `cmd_standalone(cmd_loop0);` |
| sub_EC85 | sound.c | cmd_loop1 | `cmd_standalone(cmd_loop1);` |
| sub_EC88 | sound.c | cmd_loop2 | `cmd_standalone(cmd_loop2);` |
| sub_EC99 | sound.c | cmd_skip | `cmd_standalone(cmd_skip);` |
| sub_ECA5 | sound.c | cmd_jump | `cmd_standalone(cmd_jump);` |
| sub_ECAF | sound.c | load_script_ptr | `load_script_ptr(); uint8_t x = (uint8_t)(snd_slot << 1); fC = snd_slot >> 7; X = x; A = snd_script_ptr[1]; SZN(A);` |
| sub_ECBE | sound.c | next_byte | `A = next_byte(); SZN(A); Y = 5;` |
| sub_ECD0 | sound.c | - | `uint8_t y = (uint8_t)((A << 1) + 1); uint8_t lo = PULL(); uint8_t hi = PULL(); uint16_t table = (uint16_t)(lo \| (hi << 8)); /* return address - 1 */ uint8_t tlo = bus_read((uint16_t)(table + y)); uint` |
| sub_F000 | flow.c | load_level | `load_level(A);` |
