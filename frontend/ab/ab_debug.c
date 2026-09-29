/*
 * The debug driver: the TCP line server that drives the emulator for automated tests. See the header.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET ab_sock_t;
#define AB_SOCK_CLOSE closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int ab_sock_t;
#define INVALID_SOCKET (-1)
#define AB_SOCK_CLOSE close
#endif

#include "../../libpcsxcore/misc.h"
#include "../libpicofe/input.h"
#include "../libpicofe/menu.h"
#include "../libpicofe/plat_autobleem.h"
#include "../main.h"
#include "../menu.h"
#include "../plugin_lib.h"
#include "ab_debug.h"

#define AB_DEBUG_LINE 512

static const char *screen_name = "boot";
static int keyboard_dev_id = -1;

void ab_debug_screen(const char *name)
{
	screen_name = name;	/* a pointer to a literal: the reader is one thread behind at worst */
}

/* the key names are in_sdl2's own ("escape", "f9", "eject"), so a script says what a bind says */
static int key_code(const char *name)
{
	if (keyboard_dev_id < 0)
		keyboard_dev_id = in_name_to_id("sdl:keys");
	if (keyboard_dev_id < 0)
		return -1;
	return in_get_key_code(keyboard_dev_id, name);
}

/* SDL's queue, not the emulator's state: the key goes the way a real one does, and the main thread
 * picks it up where it polls */
static void push_key(int scancode, int down)
{
	SDL_Event e;

	memset(&e, 0, sizeof(e));
	e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.type = e.type;
	e.key.timestamp = SDL_GetTicks();
	e.key.windowID = plat_ab_window != NULL ? SDL_GetWindowID(plat_ab_window) : 0;
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.repeat = 0;
	e.key.keysym.scancode = scancode;
	e.key.keysym.sym = SDL_GetKeyFromScancode(scancode);
	SDL_PushEvent(&e);
}

/* a menu presents only when it redraws, and it redraws when something happens - an expose is what
 * libpicofe's in_menu_wait takes as "draw yourself again" (PBTN_RDRAW), and a running game ignores it */
static void push_expose(void)
{
	SDL_Event e;

	memset(&e, 0, sizeof(e));
	e.type = SDL_WINDOWEVENT;
	e.window.timestamp = SDL_GetTicks();
	e.window.windowID = plat_ab_window != NULL ? SDL_GetWindowID(plat_ab_window) : 0;
	e.window.event = SDL_WINDOWEVENT_EXPOSED;
	SDL_PushEvent(&e);
}

/* the next frame presented, which the expose asks for; what was read back before it if none comes in
 * three seconds (a game can go that long between presents while the BIOS boots), -1 if that is nothing */
static int shot(const char *path)
{
	unsigned int serial = plat_ab_shot_request();
	int i;

	push_expose();
	for (i = 0; i < 300 && plat_ab_shot_serial() == serial; i++)
		SDL_Delay(10);
	return plat_ab_shot_save(path);
}

/* the next blank-separated word of *p, terminated in place; NULL at the end of the line */
static char *next_word(char **p)
{
	char *word = *p + strspn(*p, " \t");
	char *end = word + strcspn(word, " \t");

	if (word[0] == 0)
		return NULL;
	if (end[0] != 0)
		*end++ = 0;
	*p = end;
	return word;
}

static void handle(char *line, char *out, size_t out_size)
{
	char *cmd, *arg, *rest = line;
	int code, ms;

	cmd = next_word(&rest);
	if (cmd == NULL || strcmp(cmd, "ping") == 0) {
		snprintf(out, out_size, "ok");
		return;
	}

	if (strcmp(cmd, "press") == 0 || strcmp(cmd, "key") == 0 ||
	    strcmp(cmd, "down") == 0 || strcmp(cmd, "up") == 0) {
		arg = next_word(&rest);
		if (arg == NULL) {
			snprintf(out, out_size, "err no key");
			return;
		}
		code = key_code(arg);
		if (code < 0) {
			snprintf(out, out_size, "err unknown key %s", arg);
			return;
		}
		if (strcmp(cmd, "down") == 0 || strcmp(cmd, "up") == 0)
			push_key(code, strcmp(cmd, "down") == 0);
		else {
			arg = next_word(&rest);
			ms = arg != NULL ? atoi(arg) : 0;
			if (ms <= 0)
				ms = 60;
			push_key(code, 1);
			SDL_Delay(ms);
			push_key(code, 0);
		}
		snprintf(out, out_size, "ok");
		return;
	}
	if (strcmp(cmd, "wait") == 0) {
		arg = next_word(&rest);
		ms = arg != NULL ? atoi(arg) : 0;
		if (ms > 0)
			SDL_Delay(ms > 30000 ? 30000 : ms);
		snprintf(out, out_size, "ok");
		return;
	}
	if (strcmp(cmd, "frames") == 0) {
		snprintf(out, out_size, "ok %u", plat_ab_frame_count());
		return;
	}
	if (strcmp(cmd, "fps") == 0) {
		/* the last second's counters (plugin_lib's pl_frame_limit): the frames the game drew, the emulated
		 * vsyncs - under 60 (50) the emulator is slower than the console - and the CPU load; fps and cpu are
		 * only counted while the HUD shows them (pcsx.cfg g_opts = 3), -1 otherwise */
		snprintf(out, out_size, "ok fps=%d vsync=%.1f cpu=%d",
			(g_opts & OPT_SHOWFPS) ? pl_rearmed_cbs.flips_per_sec : -1, pl_rearmed_cbs.vsps_cur,
			(g_opts & OPT_SHOWCPU) ? pl_rearmed_cbs.cpu_usage : -1);
		return;
	}
	if (strcmp(cmd, "screen") == 0) {
		snprintf(out, out_size, "ok %s", screen_name);
		return;
	}
	if (strcmp(cmd, "row") == 0) {
		snprintf(out, out_size, "ok %s", menu_sel_name);
		return;
	}
	if (strcmp(cmd, "status") == 0) {
		snprintf(out, out_size, "ok game=%s screen=%s row=%s frames=%u ready=%d quit=%d window=%dx%d",
			CdromId[0] != 0 ? CdromId : "-", screen_name, menu_sel_name, plat_ab_frame_count(),
			ready_to_go, g_emu_want_quit, plat_ab_win_w, plat_ab_win_h);
		return;
	}
	if (strcmp(cmd, "shot") == 0) {
		arg = rest + strspn(rest, " \t");	/* the rest of the line: a path may have blanks */
		if (arg[0] == 0)
			snprintf(out, out_size, "err no path");
		else if (shot(arg) != 0)
			snprintf(out, out_size, "err no frame (%s)", SDL_GetError());
		else
			snprintf(out, out_size, "ok %s", arg);
		return;
	}
	if (strcmp(cmd, "quit") == 0) {
		SDL_Event e;
		memset(&e, 0, sizeof(e));
		e.type = SDL_QUIT;
		e.quit.timestamp = SDL_GetTicks();
		SDL_PushEvent(&e);
		snprintf(out, out_size, "ok");
		return;
	}
	snprintf(out, out_size, "err unknown command %s", cmd);
}

static void serve(ab_sock_t client)
{
	char buf[AB_DEBUG_LINE * 2], line[AB_DEBUG_LINE], out[AB_DEBUG_LINE];
	size_t len = 0;

	for (;;) {
		char *nl;
		int n;

		nl = memchr(buf, '\n', len);
		if (nl == NULL) {
			if (len == sizeof(buf))	/* a line longer than the buffer: drop it */
				len = 0;
			n = recv(client, buf + len, (int)(sizeof(buf) - len), 0);
			if (n <= 0)
				return;
			len += n;
			continue;
		}
		n = (int)(nl - buf);
		if (n > 0 && buf[n - 1] == '\r')
			n--;
		if (n >= (int)sizeof(line))
			n = sizeof(line) - 1;
		memcpy(line, buf, n);
		line[n] = 0;
		len -= nl - buf + 1;
		memmove(buf, nl + 1, len);

		handle(line, out, sizeof(out) - 1);
		n = (int)strlen(out);
		out[n++] = '\n';
		if (send(client, out, n, 0) < 0)
			return;
	}
}

static int server_thread(void *listener_)
{
	ab_sock_t listener = (ab_sock_t)(intptr_t)listener_;

	for (;;) {
		ab_sock_t client = accept(listener, NULL, NULL);
		if (client == INVALID_SOCKET)
			continue;
		serve(client);
		AB_SOCK_CLOSE(client);
	}
	return 0;
}

void ab_debug_start(void)
{
	const char *env = getenv("AB_DEBUG_PORT");
	struct sockaddr_in addr;
	ab_sock_t listener;
	unsigned long loopback;
	int port, one = 1;

	if (env == NULL || (port = atoi(env)) <= 0)
		return;
#ifdef _WIN32
	{
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
			fprintf(stderr, "ab_debug: WSAStartup failed\n");
			return;
		}
	}
#endif
	listener = socket(AF_INET, SOCK_STREAM, 0);
	if (listener == INVALID_SOCKET) {
		fprintf(stderr, "ab_debug: socket failed\n");
		return;
	}
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)port);
	/* not addr.sin_addr.s_addr: libpcsxcore/misc.h #undefs s_addr (a PSX EXE header field of that name),
	 * and on Windows that member only exists as the macro */
	loopback = htonl(INADDR_LOOPBACK);
	memcpy(&addr.sin_addr, &loopback, sizeof(loopback));
	if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listener, 1) != 0) {
		fprintf(stderr, "ab_debug: cannot listen on 127.0.0.1:%d\n", port);
		AB_SOCK_CLOSE(listener);
		return;
	}
	plat_ab_frame_cache(1);
	if (SDL_CreateThread(server_thread, "ab_debug", (void *)(intptr_t)listener) == NULL) {
		fprintf(stderr, "ab_debug: SDL_CreateThread failed: %s\n", SDL_GetError());
		AB_SOCK_CLOSE(listener);
		return;
	}
	printf("ab_debug: listening on 127.0.0.1:%d\n", port);
}
