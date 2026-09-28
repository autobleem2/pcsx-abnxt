#ifndef __PLUGIN_LIB_H__
#define __PLUGIN_LIB_H__

enum {
	DKEY_SELECT = 0,
	DKEY_L3,
	DKEY_R3,
	DKEY_START,
	DKEY_UP,
	DKEY_RIGHT,
	DKEY_DOWN,
	DKEY_LEFT,
	DKEY_L2,
	DKEY_R2,
	DKEY_L1,
	DKEY_R1,
	DKEY_TRIANGLE,
	DKEY_CIRCLE,
	DKEY_CROSS,
	DKEY_SQUARE,
};
extern int in_type[8];
// a platform that draws the scanlines itself (over the scaled picture) sets this; the rows above stay
extern int pl_scanlines_by_plat;
// the largest frame the platform's buffer takes: a 512x240 frame at hq3x, or 640x480 doubled (the
// 2x enhancement, scale2x) - the platform allocates its frame buffers to this
#define PL_VOUT_MAX_W 1600
#define PL_VOUT_MAX_H 1024
extern int multitap1;
extern int multitap2;
extern int in_analog_left[8][2];
extern int in_analog_right[8][2];
extern unsigned short in_keystate[8];
extern int in_mouse[8][2];

/* the analog sticks by player: [0]/[1] player 1's left/right, [2]/[3] player 2's */
extern int in_adev[4], in_adev_axis[4][2];
extern int in_adev_is_nublike[4];
extern int in_enable_vibration;

/* C11, round 3 (Marcus's review): plat_sdl2.c's pads_changed() already swaps in_adev[] (the analog sticks)
 * by pad_order, but the digital buttons follow libpicofe's own "player" acceptance-order numbering
 * (in_sdl2gc.c's IN_BINDTYPE_PLAYER12 split - the lower 16 bits are player 1's buttons, the upper 16
 * player 2's - set by in_sdl2gc_probe()'s state->player, which we do not touch in that submodule). Without
 * this, pad A's buttons would drive port 1 while its sticks drove port 2 whenever the swap is active. Set
 * by pads_changed() to the *same* gate it itself applies (pad_count >= 2, one source of truth for "is the
 * swap actually in effect right now"); update_input() (plugin_lib.c) swaps in_keystate[0]/[1] when it's
 * set, after they are filled from actions[IN_BINDTYPE_PLAYER12] - the one point both a pad's buttons and
 * the keyboard's default binds (also IN_BINDTYPE_PLAYER12, lower half) go through, so a keyboard player on
 * a dev host is swapped onto port 2 along with the pads while this is set. Acceptable there (no real
 * console has two players sharing the pcsx-abnxt window's keyboard); documented, not fixed, per Marcus. */
extern int ab_pads_swapped;

extern void *pl_vout_buf;

extern int g_layer_x, g_layer_y;
extern int g_layer_w, g_layer_h;

void  pl_start_watchdog(void);
void *pl_prepare_screenshot(int *w, int *h, int *bpp);
void  pl_init(void);
void  pl_switch_dispmode(void);
void  pl_force_clear(void);

void  pl_timing_prepare(int is_pal);
void  pl_frame_limit(void);
void  pl_update_layer_size(int w, int h, int fw, int fh);

// for communication with gpulib
struct rearmed_cbs {
	void  (*pl_get_layer_pos)(int *x, int *y, int *w, int *h);
	int   (*pl_vout_open)(void);
	void  (*pl_vout_set_mode)(int w, int h, int raw_w, int raw_h, int bpp);
	void  (*pl_vout_flip)(const void *vram, int vram_offset, int bgr24,
			      int x, int y, int w, int h, int dims_changed);
	void  (*pl_vout_close)(void);
	void  (*cspace_blit)(void *dst, const void *src, int bytes);
	void *(*mmap)(unsigned int size);
	void  (*munmap)(void *ptr, unsigned int size);
	// only used by some frontends
	void  (*pl_vout_set_raw_vram)(void *vram);
	void  (*pl_set_gpu_caps)(int caps);
	// emulation related
	void  (*gpu_state_change)(int what, int cycles);
	// some stats, for display by some plugins
	int flips_per_sec, cpu_usage;
	float vsps_cur; // currect vsync/s
	// these are for gles plugin
	unsigned int screen_w, screen_h;
	void *gles_display, *gles_surface;
	// gpu options
	int   frameskip;
	int   fskip_advice;
	int   fskip_force;
	int   fskip_dirty;
	unsigned int *gpu_frame_count;
	unsigned int *gpu_hcnt;
	unsigned int flip_cnt; // increment manually if not using pl_vout_flip
	unsigned char only_16bpp; // platform is 16bpp-only
	unsigned char dithering; // 0 off, 1 on, 2 force
	unsigned char scale_hires;
	unsigned char alt_flip;
	int   thread_rendering; // -1 auto, 0 off, 1 on
	struct {
		int   allow_interlace; // 0 off, 1 on, 2 guess
		int   enhancement_enable;
		int   enhancement_no_main;
		int   enhancement_tex_adj;
		int   enhancement_no_seams;	// AutoBleem: 1 = the enhanced pass's texel rounding at 0.25 (psx_gpu_parse.c)
	} gpu_neon;
	struct {
		int   dwActFixes;
		float fFrameRateHz;
		int   dwFrameRateTicks;
	} gpu_peops;
	struct {
		int old_renderer;
		int ilace_force;
		int lighting;
		int fast_lighting;
		int blending;
	} gpu_unai;
	struct {
		int   dwActFixes;
		int   bDrawDither, iFilterType, iFrameTexType;
		int   iUseMask, bOpaquePass, bAdvancedBlend, bUseFastMdec;
		int   iVRamSize, iTexGarbageCollection;
	} gpu_peopsgl;
	// misc
	int gpu_caps;
	int screen_centering_type;
	int screen_centering_type_default;
	int screen_centering_x;
	int screen_centering_y;
	int screen_centering_h_adj;
	int show_overscan;
};

extern struct rearmed_cbs pl_rearmed_cbs;

enum centering_type { C_AUTO = 0, C_INGAME, C_BORDERLESS, C_MANUAL };

enum gpu_plugin_caps {
	GPU_CAP_OWNS_DISPLAY = (1 << 0),
	GPU_CAP_SUPPORTS_2X = (1 << 1),
};

// platform hooks
extern void (*pl_plat_clear)(void);
extern void (*pl_plat_blit)(int doffs, const void *src,
			    int w, int h, int sstride, int bgr24);
extern void (*pl_plat_hud_print)(int x, int y, const char *str, int bpp);

/* EMU-15 part 2: hud_msg / FPS / CPU load / SPU channel notices used to be drawn by plugin_lib.c's
 * print_hud() straight into the PSX-resolution frame, where the scanline overlay (drawn later, over the
 * presented/scaled frame in libpicofe/plat_sdl2.c) covered them - the same bug the low-battery icon had.
 * A platform that draws its own scanlines over the scaled picture (currently only plat_sdl2.c) draws these
 * too, as part of its HUD overlay, after the scanlines - these three are that overlay's source of truth for
 * what to show, in the same priority and format print_hud always used. Every other platform (plat_sdl.c's
 * SDL 1.2 "sdl" platform, plat_dummy, any future one that never sets pl_hud_by_plat) gets exactly the old
 * behaviour: print_hud() below draws them itself, straight into pl_vout_buf, built from these same
 * accessors so the text and its priority are defined once either way. */
const char *ab_hud_msg_line(void);
const char *ab_hud_cpu_line(void);
#define AB_HUD_CHANS_N 24
int ab_hud_active_chans(unsigned short *out, int max);

// like pl_scanlines_by_plat above: a platform that draws hud_msg/FPS/CPU load/the SPU channel bar itself,
// after its own scanline overlay, sets this - plugin_lib.c's print_hud() then draws none of them into
// pl_vout_buf. Default 0 (drawn the old way) so a platform that never sets it (plat_sdl.c, plat_dummy) is
// unaffected.
extern int pl_hud_by_plat;

// a platform that smooths the frame on the GPU (plat_autobleem.c) sets this: for a soft_filter it answers 1
// to, plugin_lib keeps the frame at 1x and leaves the smoothing to the platform. NULL: the CPU does it all.
extern int (*pl_plat_smooths)(int soft_filter);

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

#endif /* __PLUGIN_LIB_H__ */
