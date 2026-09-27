/*
 * A standalone smoke test for ab_config.c's ab_pad_order() - AB_PAD_ORDER parsing (C11, Options -> "Swap
 * Player 1 / Player 2"). ab_config.c itself pulls in a large slice of the emulator (Config, psxRegs, ...)
 * and cannot be built as a standalone program the way ab_pad_battery.c can - the repo has no test harness
 * yet (unlike autobleem-core's doctest suites) to link just one translation unit's dependencies out. So,
 * in the same spirit as test_pad_battery.c next to it, this is a small, self-contained C program: a
 * verbatim copy of ab_pad_order()'s body under a test-only name, exercised the same way the real function
 * would be. **Keep this in step with ab_config.c's ab_pad_order() - a change to one is a change to both,
 * in the same commit.** Identical in every case to pcsx-ab's frontend/test_ab_pad_order.c (C11).
 *
 *   gcc -o test_ab_pad_order test_ab_pad_order.c && ./test_ab_pad_order
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>

static int failures;

static void expect(int cond, const char *what)
{
	if (!cond) {
		printf("FAIL: %s\n", what);
		failures++;
	} else {
		printf("ok:   %s\n", what);
	}
}

/* verbatim copy of ab_config.c's ab_pad_order(), against a plain getenv() instead of ab_env() (which is
 * ab_config.c-local and just as trivial: getenv() with "" treated as unset) */
static void test_ab_pad_order(int order[2])
{
	const char *v = getenv("AB_PAD_ORDER");
	int a = -1, b = -1;

	if (v != NULL && v[0] == 0)
		v = NULL;
	order[0] = 0;
	order[1] = 1;
	if (v == NULL)
		return;
	if (sscanf(v, "%d,%d", &a, &b) != 2)
		return;
	if ((a != 0 && a != 1) || (b != 0 && b != 1) || a == b)
		return;
	order[0] = a;
	order[1] = b;
}

/* verbatim copy of plat_sdl2.c's pads_changed() gate (Marcus's review fix): the swap only takes effect
 * with two or more pads connected - with fewer, the identity order is forced regardless of AB_PAD_ORDER,
 * so a lone pad is always player 1. Keep this in step with plat_sdl2.c's pads_changed() too. */
static void test_effective_pad_order(int pad_count, int order[2])
{
	order[0] = 0;
	order[1] = 1;
	if (pad_count >= 2)
		test_ab_pad_order(order);
}

/* --- C11 round 3 (Marcus's review): buttons and rumble must land on the same physical pad the analog
 * sticks do, for every port, pad count and swap setting. Three call sites decide this:
 *   - analog: plat_sdl2.c's pads_changed() - in_adev[port] <- in_sdl2gc_dev_id(pad_order[port] + 1)
 *   - buttons: plugin_lib.c's update_input() splits actions[IN_BINDTYPE_PLAYER12] into in_keystate[0]/[1];
 *     the bit that lands there for a given pad is fixed by libpicofe's in_sdl2gc.c:296 from state->player
 *     (the pad's own SDL acceptance index + 1 - that submodule is not ours to edit), so BEFORE round 3
 *     in_keystate[port] always carried the pad at acceptance index == port, whatever pad_order said
 *   - rumble: plat_sdl2.c:380's in_sdl2gc_rumble(pad + 1, ...) - "pad" is the PS1 port, so BEFORE round 3
 *     the player argument was always port + 1, also ignoring pad_order
 * `buttons_pad_for_port`/`rumble_pad_for_port` below are verbatim copies of production's CURRENT shape,
 * kept in step with plugin_lib.c's update_input() and plat_sdl2.c's rumble call - a change to one is a
 * change to both, in the same commit. */
static void analog_pad_for_port(int pad_count, int swap_requested, int port_pad[2])
{
	int order[2] = { 0, 1 };
	if (pad_count >= 2 && swap_requested) {
		order[0] = 1;
		order[1] = 0;
	}
	port_pad[0] = order[0];
	port_pad[1] = order[1];
}

/* verbatim copy of plugin_lib.c's update_input(), as of the round-3 fix: the raw per-pad bits are always
 * identity (in_sdl2gc.c:296's state->player, untouched by us - that submodule is not ours to edit), then
 * update_input() swaps in_keystate[0]/[1] when ab_pads_swapped is set - the same gate pads_changed() uses
 * to compute pad_order (pad_count >= 2 && the swap was requested). KEEP THIS FUNCTION IN STEP WITH
 * plugin_lib.c's update_input() - a change to one is a change to both, in the same commit. */
static void buttons_pad_for_port(int pad_count, int swap_requested, int port_pad[2])
{
	int ab_pads_swapped_ = pad_count >= 2 && swap_requested;
	port_pad[0] = ab_pads_swapped_ ? 1 : 0;
	port_pad[1] = ab_pads_swapped_ ? 0 : 1;
}

/* verbatim copy of plat_sdl2.c:380's rumble call, as of the round-3 fix:
 * in_sdl2gc_rumble(ab_pad_order_state[pad] + 1, ...) - the same pad_order pads_changed() computes for
 * in_adev, one source of truth. KEEP THIS FUNCTION IN STEP WITH plat_sdl2.c's rumble call. */
static int rumble_pad_for_port(int port, int pad_count, int swap_requested)
{
	int order[2];
	analog_pad_for_port(pad_count, swap_requested, order);
	return order[port];
}

static void set_env(const char *value)
{
#ifdef _WIN32
	if (value == NULL)
		_putenv_s("AB_PAD_ORDER", "");
	else
		_putenv_s("AB_PAD_ORDER", value);
#else
	if (value == NULL)
		unsetenv("AB_PAD_ORDER");
	else
		setenv("AB_PAD_ORDER", value, 1);
#endif
}

int main(void)
{
	int order[2];

	set_env(NULL);
	test_ab_pad_order(order);
	expect(order[0] == 0 && order[1] == 1, "unset: identity order {0, 1}");

	set_env("1,0");
	test_ab_pad_order(order);
	expect(order[0] == 1 && order[1] == 0, "\"1,0\": swapped order {1, 0}");

	set_env("0,1");
	test_ab_pad_order(order);
	expect(order[0] == 0 && order[1] == 1, "\"0,1\": identity order {0, 1}");

	set_env("");
	test_ab_pad_order(order);
	expect(order[0] == 0 && order[1] == 1, "empty: identity order {0, 1}");

	set_env("1");
	test_ab_pad_order(order);
	expect(order[0] == 0 && order[1] == 1, "\"1\": malformed, identity order {0, 1}");

	set_env("garbage");
	test_ab_pad_order(order);
	expect(order[0] == 0 && order[1] == 1, "\"garbage\": malformed, identity order {0, 1}");

	set_env("2,3");
	test_ab_pad_order(order);
	expect(order[0] == 0 && order[1] == 1, "\"2,3\": out of range, identity order {0, 1}");

	set_env("0,0");
	test_ab_pad_order(order);
	expect(order[0] == 0 && order[1] == 1, "\"0,0\": not a permutation, identity order {0, 1}");

	set_env("1,1");
	test_ab_pad_order(order);
	expect(order[0] == 0 && order[1] == 1, "\"1,1\": not a permutation, identity order {0, 1}");

	set_env("1,0,extra");
	test_ab_pad_order(order);
	expect(order[0] == 1 && order[1] == 0, "\"1,0,extra\": trailing text ignored, swapped order {1, 0}");

	/* Marcus's review fix: the swap only takes effect with two or more pads */
	set_env("1,0");
	test_effective_pad_order(0, order);
	expect(order[0] == 0 && order[1] == 1, "0 pads, swap requested: identity order (nothing to swap)");

	test_effective_pad_order(1, order);
	expect(order[0] == 0 && order[1] == 1, "1 pad, swap requested: identity order - a lone pad stays player 1");

	test_effective_pad_order(2, order);
	expect(order[0] == 1 && order[1] == 0, "2 pads, swap requested: swapped order");

	test_effective_pad_order(3, order);
	expect(order[0] == 1 && order[1] == 0, "3 pads, swap requested: swapped order (still applies)");

	set_env(NULL);
	test_effective_pad_order(2, order);
	expect(order[0] == 0 && order[1] == 1, "2 pads, no swap requested: identity order");

	/* Marcus's round-3 review: buttons and rumble must land on the same physical pad the analog sticks
	 * do, for every port, pad count and swap setting - not just the analog sticks themselves */
	{
		int pc, sw, port, analog[2], buttons[2];
		char what[160];
		for (pc = 0; pc <= 3; pc++) {
			for (sw = 0; sw <= 1; sw++) {
				analog_pad_for_port(pc, sw, analog);
				buttons_pad_for_port(pc, sw, buttons);
				for (port = 0; port < 2; port++) {
					snprintf(what, sizeof(what),
						"pads=%d swap=%d port=%d: buttons follow the same pad as the analog sticks",
						pc, sw, port);
					expect(buttons[port] == analog[port], what);

					snprintf(what, sizeof(what),
						"pads=%d swap=%d port=%d: rumble targets the same pad as the analog sticks",
						pc, sw, port);
					expect(rumble_pad_for_port(port, pc, sw) == analog[port], what);
				}
			}
		}
	}

	printf(failures == 0 ? "\nAll tests passed.\n" : "\n%d test(s) FAILED.\n", failures);
	return failures == 0 ? 0 : 1;
}
