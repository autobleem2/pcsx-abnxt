/*
 * AutoBleem: the save-state layout pcsx-abnxt shares with pcsx-ab - see state_sony.c.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef __STATE_SONY_H__
#define __STATE_SONY_H__

#ifdef __cplusplus
extern "C" {
#endif

/* upstream's SaveState()/LoadState() (misc.c), renamed: upstream's own layout, through SaveFuncs */
int SaveStateNative(const char *file);
int LoadStateNative(const char *file);

/* where each section of upstream's stream starts - SaveStateNative() marks them in this order */
enum state_section {
	STATE_HEAD,	/* header, version, HLE flag */
	STATE_PICTURE,	/* the old screen picture's space (upstream keeps its origin_info there) */
	STATE_MEMORY,	/* RAM, BIOS, scratchpad + hardware registers */
	STATE_REGS,
	STATE_GPU,
	STATE_SPU,
	STATE_SIO,
	STATE_CDR,
	STATE_HW,
	STATE_RCNT,
	STATE_MDEC,
	STATE_NDRC,
	STATE_PAD,
	STATE_END,
	STATE_SECTIONS
};
void state_mark(enum state_section s);

/* the autosave: upstream's layout into a buffer in RAM (never a file, the stick stays quiet); one only,
 * each save replaces it. 0 on success; StateAutoAge() is the milliseconds' tick it was made at, -1 for
 * none yet */
int SaveStateAuto(unsigned int now_ms);
int LoadStateAuto(void);
long long StateAutoAge(void);

/* cdrom.c: the CD-ROM section (cdr, then the FIFO offset) and what pcsx-ab's fields hold */
int cdrFreezeSize(void);
int cdrStateToSony(void *section);	/* 1 when CD audio is playing */
void cdrStateFromSony(void *section);

/* psxcounters.c: the base counter's target in pcsx-ab (its loader divides by it) */
unsigned int psxRcntSonyBaseTarget(void);

#ifdef __cplusplus
}
#endif

#endif
