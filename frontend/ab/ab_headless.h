/*
 * AB_HEADLESS=1 in the environment: the same automated-test-run policy the launcher's Platform class has
 * (autobleem-core's lib_ableem/include/ableem/ui/platform.h) - a hidden window from the very first frame,
 * SDL's dummy audio driver, no input focus taken. R29 - headless test runs on Windows (the owner kept
 * seeing testers' pcsx-abnxt/launcher windows and firewall prompts).
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef AB_HEADLESS_H
#define AB_HEADLESS_H

#ifdef __cplusplus
extern "C" {
#endif

/* reads AB_HEADLESS from the environment: 1 only for exactly "1" - "0", "true", unset, empty are all not
 * headless. Read fresh each call (cheap), so a test can toggle it. */
int ab_headless_requested(void);

/* pure policy, for testing without SDL: does a headless run start its window hidden? */
int ab_headless_starts_hidden(int headless);

/* pure policy, for testing without SDL: the SDL_AUDIODRIVER to force for a headless run ("dummy"), or
 * NULL to leave SDL's own probing alone. */
const char *ab_headless_audio_driver(int headless);

#ifdef __cplusplus
}
#endif

#endif
