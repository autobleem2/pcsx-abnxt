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

	printf(failures == 0 ? "\nAll tests passed.\n" : "\n%d test(s) FAILED.\n", failures);
	return failures == 0 ? 0 : 1;
}
