/* GENERATED (step 3 groundwork): prototypes of the lifted C functions, one per
 * routine, so packages call each other directly instead of through the
 * register-level adapters (sub_XXXX + CALL_MECH). Home file in the comment. */
#ifndef BC_LIFTED_H
#define BC_LIFTED_H
#include <stdint.h>

void build_eagle_brick(uint8_t *yout);      /* bullets.c */
void build_eagle_concrete(uint8_t *yout);   /* bullets.c */
void bullet_dispatch(uint8_t x);            /* bullets.c */
void bullet_explode_tick(uint8_t x);        /* bullets.c */
void bullet_move(uint8_t x);                /* bullets.c */
void bullet_step(uint8_t x, uint8_t y);     /* bullets.c */
void bullet_vs_bullet(void);                /* bullets.c */
void bullet_vs_tank(void);                  /* bullets.c */
void bullet_vs_terrain(void);               /* bullets.c */
uint8_t draw_destroyed_eagle(void);         /* bullets.c */
void draw_eagle_part(uint8_t tile, uint8_t x, uint8_t y); /* bullets.c */
void eagle_big_boom(void);                  /* bullets.c */
void eagle_small_boom(uint8_t tile);        /* bullets.c */
void enemy_fire(void);                      /* bullets.c */
void fire_bullet(uint8_t x);                /* bullets.c */
uint8_t hit_terrain(uint8_t y, uint8_t *yout); /* bullets.c */
void player_fire(void);                     /* bullets.c */
uint8_t powerup_coord(uint8_t n);           /* bullets.c */
void powerup_extra_life(uint8_t x);         /* bullets.c */
void powerup_grenade(void);                 /* bullets.c */
void powerup_helmet(uint8_t x);             /* bullets.c */
int powerup_pickup(uint8_t *xout);          /* bullets.c */
uint8_t powerup_shovel(uint8_t x);          /* bullets.c */
void powerup_star(uint8_t x);               /* bullets.c */
void powerup_timer(void);                   /* bullets.c */
uint8_t probe_terrain(uint8_t px, uint8_t py, uint8_t *yout); /* bullets.c */
void shovel_and_base(void);                 /* bullets.c */
void spawn_powerup(void);                   /* bullets.c */
void update_bullets(void);                  /* bullets.c */
uint8_t cell_to_addr(uint8_t col, uint8_t row, uint8_t *lo); /* cells.c */
void clear_quarter(uint8_t x, uint8_t y);   /* cells.c */
void clear_quarter_quiet(uint8_t y);        /* cells.c */
void coords_to_map(uint8_t px, uint8_t py); /* cells.c */
void draw_hud_number(uint8_t col, uint8_t row); /* cells.c */
uint8_t draw_tiles(uint8_t col, uint8_t row); /* cells.c */
void locate_pixel(uint8_t px, uint8_t py);  /* cells.c */
void map_ptr_from_cell(uint8_t col, uint8_t row); /* cells.c */
uint8_t pal_shift_in(uint8_t a);            /* cells.c */
void ptr_advance(uint8_t n);                /* cells.c */
void quarter_mask(void);                    /* cells.c */
void queue_block_palette(uint8_t col, uint8_t row); /* cells.c */
void read_joypads(void);                    /* cells.c */
uint8_t set_block_palette(uint8_t col, uint8_t row); /* cells.c */
void set_quarter_quiet(uint8_t y);          /* cells.c */
uint8_t test_quarter(uint8_t y);            /* cells.c */
void write_cell(uint8_t v, uint8_t x, uint8_t y); /* cells.c */
void write_cell_direct(uint8_t y);          /* cells.c */
int attract_run(void);                      /* flow.c */
void attract_setup(void);                   /* flow.c */
void bc_reset_lifted(void);                 /* flow.c */
void game_main(void);                       /* flow.c */
void hiscore_screen(void);                  /* flow.c */
void init_system(void);                     /* flow.c */
void load_initial_palettes(void);           /* flow.c */
void load_level(uint8_t n);                 /* flow.c */
void nmi(void);                             /* flow.c */
void redraw_screen(void);                   /* flow.c */
void rendering_off(void);                   /* flow.c */
void rendering_on(void);                    /* flow.c */
void start_stage(void);                     /* flow.c */
int title_menu(void);                       /* flow.c */
void title_scroll(void);                    /* flow.c */
void wait_frame(void);                      /* flow.c */
void wait_vblank_flag(void);                /* flow.c */
void attract_input(void);                   /* flow_hud.c */
void clamp_cursor(void);                    /* flow_hud.c */
void clear_kill_counts(void);               /* flow_hud.c */
void clear_pads_if_base_lost(void);         /* flow_hud.c */
void clear_playfield(void);                 /* flow_hud.c */
void construction_cursor_input(void);       /* flow_hud.c */
void draw_cursor_block(void);               /* flow_hud.c */
void draw_enemy_column(void);               /* flow_hud.c */
void draw_enemy_icon(uint8_t n);            /* flow_hud.c */
void draw_hud(void);                        /* flow_hud.c */
void draw_hud_labels(void);                 /* flow_hud.c */
void erase_enemy_icon(uint8_t n);           /* flow_hud.c */
void hud_enemy_cell(uint8_t n, uint8_t *col, uint8_t *row); /* flow_hud.c */
void load_custom_level(void);               /* flow_hud.c */
void main_loop(void);                       /* flow_hud.c */
void menu_cursor_y(void);                   /* flow_hud.c */
void new_game(void);                        /* flow_hud.c */
void palette_cycle(void);                   /* flow_hud.c */
void queue_map_row(uint8_t row);            /* flow_hud.c */
void reset_game_state(void);                /* flow_hud.c */
uint8_t stage_over(void);                   /* flow_hud.c */
void add_digits(uint8_t buf);               /* gfx_text.c */
uint8_t align_number(uint8_t x, uint8_t y); /* gfx_text.c */
uint8_t boot_signature_valid(uint8_t *xout, uint8_t *cout); /* gfx_text.c */
void check_extra_life(void);                /* gfx_text.c */
void clear_digits(uint8_t x);               /* gfx_text.c */
void clear_oam(void);                       /* gfx_text.c */
void clear_terrain_map(void);               /* gfx_text.c */
void draw_block(uint8_t type, uint8_t px, uint8_t py); /* gfx_text.c */
void draw_glyph_bricks(uint8_t tile, uint8_t px, uint8_t py); /* gfx_text.c */
void draw_hiscore_bricks(void);             /* gfx_text.c */
void draw_text_bricks(void);                /* gfx_text.c */
uint8_t flush_vram_queue(void);             /* gfx_text.c */
uint8_t load_bg_palette(uint8_t row);       /* gfx_text.c */
void load_sprite_palettes(void);            /* gfx_text.c */
void number_to_digits(uint8_t n);           /* gfx_text.c */
void render_playfield(void);                /* gfx_text.c */
void reset_terrain_map(void);               /* gfx_text.c */
void set_score_popup(uint8_t bcd);          /* gfx_text.c */
void set_tally_attributes(void);            /* gfx_text.c */
uint8_t update_hiscore(uint8_t *xout);      /* gfx_text.c */
void write_boot_signature(void);            /* gfx_text.c */
void build_tally_screen(void);              /* screens.c */
void clear_screen(void);                    /* screens.c */
void credits_screen(void);                  /* screens.c */
void credits_sprite_drop(void);             /* screens.c */
void curtain_close(void);                   /* screens.c */
void curtain_open(void);                    /* screens.c */
void delay_frames(uint8_t n);               /* screens.c */
void draw_credit_star(void);                /* screens.c */
void draw_hud_flag(void);                   /* screens.c */
void draw_title_logo(void);                 /* screens.c */
void game_over_screen(void);                /* screens.c */
void stage_intro_text(void);                /* screens.c */
void stage_tally(void);                     /* screens.c */
void upload_attributes(void);               /* screens.c */
void wait_64_frames(void);                  /* screens.c */
void sound_driver(void);                    /* sound.c */
void sound_reset(void);                     /* sound.c */
uint8_t big_explosion_tile(uint8_t q, uint8_t *y); /* sprites.c */
void draw_all_tanks(void);                  /* sprites.c */
void draw_bullet(uint8_t slot);             /* sprites.c */
void draw_bullet_explosion(uint8_t slot);   /* sprites.c */
void draw_bullet_sprite(uint8_t slot);      /* sprites.c */
void draw_bullets(void);                    /* sprites.c */
void draw_death_tail(uint8_t slot);         /* sprites.c */
void draw_explosion(uint8_t slot);          /* sprites.c */
void draw_explosion_big(void);              /* sprites.c */
void draw_explosion_frame(uint8_t frame, uint8_t x, uint8_t y); /* sprites.c */
void draw_explosion_last(uint8_t slot);     /* sprites.c */
void draw_explosion_sprite(uint8_t state, uint8_t x, uint8_t y); /* sprites.c */
void draw_game_over_banner(void);           /* sprites.c */
void draw_pause_banner(void);               /* sprites.c */
void draw_powerup(void);                    /* sprites.c */
void draw_shield(void);                     /* sprites.c */
void draw_spawn_twinkle(uint8_t slot);      /* sprites.c */
void draw_sprite(uint8_t facing, uint8_t x, uint8_t y); /* sprites.c */
void draw_sprite16(uint8_t facing, uint8_t x, uint8_t y); /* sprites.c */
void draw_sprite16_at(uint8_t x, uint8_t y); /* sprites.c */
void draw_sprite_oam(uint8_t x, uint8_t y); /* sprites.c */
void draw_tally_sprites(void);              /* sprites.c */
void draw_tally_tank(uint8_t tile, uint8_t y); /* sprites.c */
void draw_tank(uint8_t slot);               /* sprites.c */
void draw_tank_body(uint8_t slot);          /* sprites.c */
void game_over_slide(void);                 /* sprites.c */
void arm_game_over_banner(void);            /* tanks.c */
uint8_t clamp_probe_x(uint8_t v);           /* tanks.c */
uint8_t clamp_probe_y(uint8_t v);           /* tanks.c */
void clear_bullet_slots(void);              /* tanks.c */
void clear_tank_occupancy(void);            /* tanks.c */
void clear_tank_slots(void);                /* tanks.c */
uint8_t decode_direction(uint8_t buttons);  /* tanks.c */
uint8_t heading_to_target(uint8_t x);       /* tanks.c */
void init_tank_type(uint8_t x);             /* tanks.c */
void load_enemy_counts(void);               /* tanks.c */
uint8_t mark_cell_occupied(uint8_t y);      /* tanks.c */
void move_all_tanks(void);                  /* tanks.c */
void move_tank(uint8_t x);                  /* tanks.c */
void pick_direction(uint8_t x);             /* tanks.c */
void player_control(void);                  /* tanks.c */
uint8_t player_is_driving(uint8_t x);       /* tanks.c */
uint8_t rng(void);                          /* tanks.c */
uint8_t rng_core(uint8_t *carry_out, uint8_t *ovf_out); /* tanks.c */
void seek_eagle(uint8_t x);                 /* tanks.c */
void seek_player1(uint8_t x);               /* tanks.c */
void seek_player2(uint8_t x);               /* tanks.c */
uint8_t set_tank_mode(uint8_t mode, uint8_t x); /* tanks.c */
uint8_t sign_of(uint8_t a, uint8_t zero, uint8_t carry); /* tanks.c */
void spawn_enemy(void);                     /* tanks.c */
void spawn_tank(uint8_t x);                 /* tanks.c */
void tank_drive(uint8_t x);                 /* tanks.c */
void tank_dying(uint8_t x);                 /* tanks.c */
void tank_random_turn(uint8_t x);           /* tanks.c */
void tank_spawn_phase1(uint8_t x);          /* tanks.c */
void tank_spawn_phase2(uint8_t x);          /* tanks.c */
void tank_turning(uint8_t x);               /* tanks.c */
uint8_t unmark_cell(uint8_t y);             /* tanks.c */
void update_engine_sound(void);             /* tanks.c */
void update_tank_terrain(void);             /* tanks.c */

#endif
