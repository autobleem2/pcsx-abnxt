/*
 * A standalone smoke test for ab_headless.c, in the same spirit as test_pad_battery.c: no console, no SDL,
 * just the environment. Compile and run it directly:
 *
 *   gcc -o test_ab_headless ab_headless.c test_ab_headless.c && ./test_ab_headless
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ab_headless.h"

static int failures;

static void expect(int cond, const char *what)
{
	if (!cond) {
		fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

int main(void)
{
	expect(ab_headless_starts_hidden(1), "starts_hidden(1) is true");
	expect(!ab_headless_starts_hidden(0), "starts_hidden(0) is false");

	expect(ab_headless_audio_driver(1) != NULL && strcmp(ab_headless_audio_driver(1), "dummy") == 0,
	       "audio_driver(1) is \"dummy\"");
	expect(ab_headless_audio_driver(0) == NULL, "audio_driver(0) is NULL");

#ifdef _WIN32
	_putenv_s("AB_HEADLESS", "");
#else
	unsetenv("AB_HEADLESS");
#endif
	expect(!ab_headless_requested(), "unset AB_HEADLESS is not headless");

#ifdef _WIN32
	_putenv_s("AB_HEADLESS", "1");
#else
	setenv("AB_HEADLESS", "1", 1);
#endif
	expect(ab_headless_requested(), "AB_HEADLESS=1 is headless");

#ifdef _WIN32
	_putenv_s("AB_HEADLESS", "0");
#else
	setenv("AB_HEADLESS", "0", 1);
#endif
	expect(!ab_headless_requested(), "AB_HEADLESS=0 is not headless");

#ifdef _WIN32
	_putenv_s("AB_HEADLESS", "true");
#else
	setenv("AB_HEADLESS", "true", 1);
#endif
	expect(!ab_headless_requested(), "AB_HEADLESS=true is not headless");

	if (failures) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	printf("ok\n");
	return 0;
}
