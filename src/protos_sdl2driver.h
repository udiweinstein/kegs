#ifdef INCLUDE_RCSID_C
#endif

/* sdl2driver.c */
int main(int argc, char **argv);
void xdriver_end(void);
void sdl_video_init_sdl(void);
void sdl_video_init(void);
void sdl_init_window(Sdl_window_info *win_info_ptr, Kimage *kimage_ptr, const char *name_str);
void sdl_create_window(Sdl_window_info *win_info_ptr);
void sdl_update_display(Sdl_window_info *win_info_ptr);
void sdl_maybe_save_splash(Sdl_window_info *win_info_ptr);
void sdl_check_debugwin(void);
void sdl_toggle_fullscreen(void);
void sdl_do_paste(void);
void sdl_do_copy(void);
void sdl_update_modifier_state(Kimage *kimage_ptr);
void sdl_handle_key(SDL_KeyboardEvent *kev);
int sdl_buttons_from_state(Uint32 state);
void sdl_update_mouse_abs(int raw_x, int raw_y, int buttons, int valid);
int sdl_input_events(void);
void g_adb_mainwin_has_focus_set(int has_focus);
