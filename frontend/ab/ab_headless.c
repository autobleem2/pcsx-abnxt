/*
 * ab_headless - see the header.
 */
#include "ab_headless.h"

#include <stdlib.h>
#include <string.h>

int ab_headless_requested(void)
{
	const char *v = getenv("AB_HEADLESS");
	return v != NULL && strcmp(v, "1") == 0;
}

int ab_headless_starts_hidden(int headless)
{
	return headless != 0;
}

const char *ab_headless_audio_driver(int headless)
{
	return headless ? "dummy" : NULL;
}
