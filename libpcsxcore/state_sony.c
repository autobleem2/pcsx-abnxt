/*
 * AutoBleem: the save-state layout pcsx-abnxt shares with pcsx-ab.
 *
 * AutoBleem ships two PS1 emulators and the player picks one (the launcher's Options -> "PS1 Emulator"),
 * so a resume point one of them wrote has to be one the other can continue from. The layout they share is
 * pcsx-ab's - Sony's 2018 build of PCSX-ReARMed, which the PlayStation Classic shipped with - and pcsx-ab
 * reads and writes it as it always has. This file makes SaveState()/LoadState() write and read it here.
 *
 * Upstream's layout is the same stream as pcsx-ab's, give or take what either side changed since 2017:
 *   - Sony's GPU header has two more words (ulEventStatus, ulEventCnt) after ulStatus;
 *   - Sony's SPU blob has three more fields (SPUInfo, volume, reverb) between the XA buffer and the rest;
 *   - two event slots mean something else there: 6 is its GPUBUSY (here SPU_IRQ), 13 its CDRPLAY (here
 *     IRQ10) - pcsx-ab plays CD audio on CDRPLAY, upstream on CDREAD together with the reads;
 *   - the CD-ROM struct is laid out the same but some fields hold something else (cdrStateToSony, cdrom.c);
 *   - the MDEC's run-length pointers are saved against psxM + 1 MB there, psxM here;
 *   - pcsx-ab's loader divides by the base root counter's target, which upstream leaves at 0;
 *   - pcsx-ab keeps the GPU's busy bit in its copy of GPUSTAT and only its GPU DMA event sets it again,
 *     where upstream times it with gpuIdleAfter (write_sony);
 *   - the stream ends in Sony's disc-change state (three ints) where upstream saves the pads.
 *
 * SaveState() runs upstream's SaveStateNative() into memory - SaveFuncs pointed at a buffer, the way the
 * libretro core saves to memory - with state_mark() noting where each section starts,
 * and writes it out through the caller's SaveFuncs translated. After pcsx-ab's last field it appends an
 * extension - EXT_MAGIC, a record count, then records of a 4-byte id, a length and the bytes - with the
 * sections the translation could not keep as they were: the registers, the CD-ROM, the root counters, the
 * MDEC, the pads and the interrupt status register. pcsx-ab stops reading before it.
 *
 * LoadState() reads the whole file, rebuilds upstream's stream - from the extension where there is one (a
 * state saved here: exactly what was saved), by translating back where there is none (pcsx-ab's) - and
 * hands it to LoadStateNative() the same way. A file that is not in this layout at all - upstream's own,
 * which is also what pcsx-abnxt wrote before this file, or RetroArch's - goes to LoadStateNative() as it
 * is. Sony's SPU fields hold a pointer, so a 32-bit build writes them 4 bytes shorter than a 64-bit one;
 * either width is read, whichever build this is (a console's stick in a Pi 64 or a PC).
 *
 * A state saved on the HLE BIOS cannot cross over: each emulator keeps its own HLE data in the BIOS area,
 * laid out differently. One of pcsx-ab's is refused, so the game starts from the beginning instead.
 *
 * (C) AutoBleem team, 2026
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psxcommon.h"
#include "misc.h"
#include "r3000a.h"
#include "psxevents.h"
#include "psxcounters.h"
#include "state_sony.h"
#include "../plugins/dfsound/spu_config.h"

#define HEAD_SIZE	(32 + 4 + 1)		/* header, version, HLE flag */
#define PICTURE_SIZE	(128 * 96 * 3)
#define RAM_SIZE	0x200000
#define ROM_SIZE	0x80000
#define HW_SIZE		0x10000
#define MEMORY_SIZE	(RAM_SIZE + ROM_SIZE + HW_SIZE)
#define REGS_SIZE	offsetof(psxRegisters, gteBusyCycle)
#define VRAM_SIZE	(1024 * 512 * 2)
#define SPU_RAM_SIZE	(512 * 1024)
#define SIO_SIZE	286
#define RCNT_SIZE	(4 * sizeof(Rcnt) + 4 * sizeof(u32))
#define MDEC_SIZE	1312
#define DISC_SIZE	(3 * sizeof(int))		/* pcsx-ab's disc_change_state */

#define ISTAT_OFFSET	0x1070				/* I_STAT in the hardware registers */
#define DMA2_CHCR_OFFSET 0x10a8				/* the GPU's DMA channel control */
#define GPUSTAT_OFFSET	0x1814				/* the emulator's copy of GPUSTAT */
#define SONY_GPU_EXTRA	(2 * sizeof(u32))		/* ulEventStatus, ulEventCnt */
#define SONY_PSXINT_GPUBUSY 6
#define SONY_PSXINT_CDRPLAY 13
#define SONY_RC_COUNT_TO_TARGET 0x0008
#define MDEC_V_RL	8				/* the offsets saved for rl, rl_end, block_buffer_pos */
#define MDEC_V_RL_END	12
#define MDEC_V_BLOCK	16
#define SONY_MDEC_BASE	0x100000

#define NDRC_MAGIC	"ariblks"			/* the dynarec's block list, same in both */
#define EXT_MAGIC	"ABNXTEX1"

/* what Sony's SPUFreeze_t has after the XA buffer; laid out by the compiler as pcsx-ab's own build lays
 * it out on the same target (the pointer makes it 12 bytes on a 32-bit ARM, 16 on a 64-bit one). A state
 * from the other width is read too - see sony_to_native. */
struct sony_spu_extra {
	unsigned char *SPUInfo;
	int volume;
	int reverb;
};
#define SONY_SPU_EXTRA_32	12
#define SONY_SPU_EXTRA_64	16
/* pcsx-ab's SPU blob around them: the header, SPU RAM and the XA buffer before, and after them its
 * SPUOSSFreeze_t - 24 bytes of registers and 24 channels of 488 (SPUCHAN_orig, as here) */
#define SONY_SPU_FIXED		(sizeof(SPUFreeze_t) + SPU_RAM_SIZE + sizeof(xa_decode_t))
#define SONY_SPU_OSS_SIZE	(24 + 24 * 488)

_Static_assert(offsetof(psxRegisters, gteBusyCycle) == 792, "psxRegisters no longer matches pcsx-ab's");
_Static_assert(sizeof(GPUFreeze_t) == 8 + 1024, "GPUFreeze_t no longer matches pcsx-ab's");
_Static_assert(sizeof(SPUFreeze_t) == 8 + 4 + 4 + 0x200, "SPUFreeze_t no longer matches pcsx-ab's");
_Static_assert(sizeof(Rcnt) == 28, "Rcnt no longer matches pcsx-ab's");
_Static_assert(PSXINT_SPU_IRQ == SONY_PSXINT_GPUBUSY && PSXINT_IRQ10 == SONY_PSXINT_CDRPLAY,
	"the event slots moved - regs_to_sony/regs_from_sony need a look");

/* --- a SaveFuncs stream over memory ------------------------------------------------------------------ */

struct mem_stream {
	char name[16];		/* first, so the "file name" LoadStateNative prints is this */
	u8 *buf;
	size_t pos, len, cap;
	int oom;
};

static int mem_reserve(struct mem_stream *m, size_t need)
{
	u8 *p;
	size_t cap;

	if (need <= m->cap)
		return 0;
	cap = m->cap ? m->cap : 0x4a0000;
	while (cap < need)
		cap *= 2;
	p = realloc(m->buf, cap);
	if (p == NULL) {
		m->oom = 1;
		return -1;
	}
	m->buf = p;
	m->cap = cap;
	return 0;
}

static void *mem_open(const char *name, const char *mode)
{
	struct mem_stream *m = (struct mem_stream *)name;
	(void)mode;
	m->pos = 0;
	return m;
}

static int mem_read(void *file, void *buf, u32 len)
{
	struct mem_stream *m = file;
	if (m->pos >= m->len)
		return 0;
	if (len > m->len - m->pos)
		len = m->len - m->pos;
	memcpy(buf, m->buf + m->pos, len);
	m->pos += len;
	return len;
}

static int mem_write(void *file, const void *buf, u32 len)
{
	struct mem_stream *m = file;
	if (mem_reserve(m, m->pos + len) != 0)
		return -1;
	memcpy(m->buf + m->pos, buf, len);
	m->pos += len;
	if (m->len < m->pos)
		m->len = m->pos;
	return len;
}

static long mem_seek(void *file, long offs, int whence)
{
	struct mem_stream *m = file;
	switch (whence) {
	case SEEK_SET: m->pos = offs; break;
	case SEEK_CUR: m->pos += offs; break;
	case SEEK_END: m->pos = m->len + offs; break;
	default: return -1;
	}
	return m->pos;
}

static void mem_close(void *file)
{
	(void)file;
}

static const struct PcsxSaveFuncs mem_funcs = {
	mem_open, mem_read, mem_write, mem_seek, mem_close
};

static void mem_put(struct mem_stream *m, const void *data, size_t len)
{
	mem_write(m, data, len);
}

/* --- the section marks ------------------------------------------------------------------------------- */

static struct mem_stream capture = { "(state)" };	/* its buffer is kept between saves */
static size_t marks[STATE_SECTIONS];
static int capturing;

void state_mark(enum state_section s)
{
	if (capturing)
		marks[s] = capture.pos;
}

/* --- little helpers ---------------------------------------------------------------------------------- */

static u32 get32(const u8 *p)
{
	u32 v;
	memcpy(&v, p, sizeof(v));
	return v;
}

static void set32(u8 *p, u32 v)
{
	memcpy(p, &v, sizeof(v));
}

static void clear_event(psxRegisters *r, int e)
{
	r->interrupt &= ~(1u << e);
	r->intCycle[e].sCycle = r->intCycle[e].cycle = 0;
}

static void move_event(psxRegisters *r, int from, int to)
{
	r->intCycle[to] = r->intCycle[from];
	r->interrupt |= 1u << to;
	clear_event(r, from);
}

/* Upstream's registers -> pcsx-ab's events. A pending SPU IRQ has been raised in I_STAT already (it is only
 * ever a few cycles away); IRQ10 is the light gun's, and the gun raises the next one itself. */
static void regs_to_sony(psxRegisters *r, int cdda)
{
	u32 pending = r->interrupt;

	clear_event(r, PSXINT_SPU_IRQ);
	clear_event(r, PSXINT_IRQ10);
	if (cdda && (pending & (1u << PSXINT_CDREAD)))
		move_event(r, PSXINT_CDREAD, SONY_PSXINT_CDRPLAY);
}

/* pcsx-ab's events -> upstream's. Its GPUBUSY has no event here (gpuIdleAfter does that, which
 * LoadStateNative resets); its CD audio goes on CDREAD, where upstream plays it. */
static void regs_from_sony(psxRegisters *r)
{
	clear_event(r, SONY_PSXINT_GPUBUSY);
	if (r->interrupt & (1u << SONY_PSXINT_CDRPLAY)) {
		if (r->interrupt & (1u << PSXINT_CDREAD))
			clear_event(r, SONY_PSXINT_CDRPLAY);
		else
			move_event(r, SONY_PSXINT_CDRPLAY, PSXINT_CDREAD);
	}
	clear_event(r, SONY_PSXINT_CDRPLAY);
}

/* --- saving ------------------------------------------------------------------------------------------ */

static int sections_as_expected(const u8 *n)
{
	size_t spu_len = marks[STATE_SIO] - marks[STATE_SPU];

	return marks[STATE_HEAD] == 0
		&& marks[STATE_PICTURE] - marks[STATE_HEAD] == HEAD_SIZE
		&& marks[STATE_MEMORY] - marks[STATE_PICTURE] == PICTURE_SIZE
		&& marks[STATE_REGS] - marks[STATE_MEMORY] == MEMORY_SIZE
		&& marks[STATE_GPU] - marks[STATE_REGS] == REGS_SIZE
		&& marks[STATE_SPU] - marks[STATE_GPU] == sizeof(GPUFreeze_t) + VRAM_SIZE
		&& spu_len > 4 + sizeof(SPUFreeze_t) + SPU_RAM_SIZE + sizeof(xa_decode_t)
		&& get32(n + marks[STATE_SPU]) == spu_len - 4
		&& marks[STATE_CDR] - marks[STATE_SIO] == SIO_SIZE
		&& marks[STATE_HW] - marks[STATE_CDR] == (size_t)cdrFreezeSize()
		&& marks[STATE_RCNT] == marks[STATE_HW]
		&& marks[STATE_MDEC] - marks[STATE_RCNT] == RCNT_SIZE
		&& marks[STATE_NDRC] - marks[STATE_MDEC] == MDEC_SIZE
		&& marks[STATE_PAD] >= marks[STATE_NDRC]
		&& marks[STATE_END] >= marks[STATE_PAD]
		&& marks[STATE_END] == capture.len;
}

static void put_record(void *f, const char *id, const void *data, u32 len)
{
	SaveFuncs.write(f, id, 4);
	SaveFuncs.write(f, &len, sizeof(len));
	SaveFuncs.write(f, data, len);
}

/* upstream's stream n (capture.buf, sections at marks[]) -> pcsx-ab's, through SaveFuncs */
static int write_sony(const char *file, const u8 *n)
{
	const u8 *hw = n + marks[STATE_MEMORY] + RAM_SIZE + ROM_SIZE;
	const u8 *spu = n + marks[STATE_SPU];
	const u8 *part2;
	struct sony_spu_extra extra;
	psxRegisters regs;
	SPUFreeze_t spu_hdr;
	Rcnt rcnt[4];
	u8 gpu_extra[SONY_GPU_EXTRA], disc[DISC_SIZE], mdec[MDEC_SIZE];
	u8 *cdr, *hw_sony;
	u32 istat, native_istat, spu_size, part2_len, records, extra_len;
	int cdda;
	void *f;

	cdr = malloc(cdrFreezeSize());
	hw_sony = malloc(HW_SIZE);
	if (cdr == NULL || hw_sony == NULL) {
		free(cdr);
		free(hw_sony);
		return -1;
	}
	memcpy(cdr, n + marks[STATE_CDR], cdrFreezeSize());
	cdda = cdrStateToSony(cdr);

	memset(&regs, 0, sizeof(regs));
	memcpy(&regs, n + marks[STATE_REGS], REGS_SIZE);

	/* the hardware registers as pcsx-ab reads them. A pending SPU IRQ goes into I_STAT now. And pcsx-ab
	 * takes the GPU's busy bit from its copy of GPUSTAT, clearing it when a GPU DMA starts and setting it
	 * again only from that DMA's event - where upstream times it with gpuIdleAfter and leaves the copy as
	 * it happens to be. A state saved in one of upstream's short busy moments would keep pcsx-ab's GPU
	 * busy for good (a game waiting for it never goes on), so it is idle here unless a DMA is running. */
	memcpy(hw_sony, hw, HW_SIZE);
	native_istat = istat = get32(hw + ISTAT_OFFSET);
	if (regs.interrupt & (1u << PSXINT_SPU_IRQ))
		istat |= SWAPu32(0x200);
	set32(hw_sony + ISTAT_OFFSET, istat);
	if (!(get32(hw + DMA2_CHCR_OFFSET) & SWAPu32(0x01000000)))
		set32(hw_sony + GPUSTAT_OFFSET, get32(hw + GPUSTAT_OFFSET) | SWAPu32(PSXGPU_nBUSY));
	regs_to_sony(&regs, cdda);

	f = SaveFuncs.open(file, "wb");
	if (f == NULL) {
		free(cdr);
		free(hw_sony);
		return -1;
	}

	/* header, picture, RAM, BIOS, the hardware registers as above */
	SaveFuncs.write(f, n, marks[STATE_MEMORY] + RAM_SIZE + ROM_SIZE);
	SaveFuncs.write(f, hw_sony, HW_SIZE);
	SaveFuncs.write(f, &regs, REGS_SIZE);

	/* the GPU: ulFreezeVersion, ulStatus, Sony's two event words, the control registers, VRAM */
	memset(gpu_extra, 0, sizeof(gpu_extra));
	SaveFuncs.write(f, n + marks[STATE_GPU], 8);
	SaveFuncs.write(f, gpu_extra, sizeof(gpu_extra));
	SaveFuncs.write(f, n + marks[STATE_GPU] + 8, sizeof(GPUFreeze_t) - 8 + VRAM_SIZE);

	/* the SPU: the size (twice), the ports, its RAM, the XA buffer, Sony's three fields, the rest. pcsx-ab
	 * takes its output volume and the reverb switch from the state, so they are ours. */
	spu_size = get32(spu) + sizeof(extra);
	memcpy(&spu_hdr, spu + 4, sizeof(spu_hdr));
	spu_hdr.Size = spu_size;
	part2 = spu + 4 + sizeof(spu_hdr) + SPU_RAM_SIZE;
	part2_len = get32(spu) - sizeof(spu_hdr) - SPU_RAM_SIZE;
	memset(&extra, 0, sizeof(extra));
	extra.volume = spu_config.iVolume;
	extra.reverb = spu_config.iUseReverb;
	SaveFuncs.write(f, &spu_size, sizeof(spu_size));
	SaveFuncs.write(f, &spu_hdr, sizeof(spu_hdr));
	SaveFuncs.write(f, spu + 4 + sizeof(spu_hdr), SPU_RAM_SIZE);
	SaveFuncs.write(f, part2, sizeof(xa_decode_t));
	SaveFuncs.write(f, &extra, sizeof(extra));
	SaveFuncs.write(f, part2 + sizeof(xa_decode_t), part2_len - sizeof(xa_decode_t));

	SaveFuncs.write(f, n + marks[STATE_SIO], SIO_SIZE);
	SaveFuncs.write(f, cdr, cdrFreezeSize());

	/* the root counters: pcsx-ab's base counter counts to its target */
	memcpy(rcnt, n + marks[STATE_RCNT], sizeof(rcnt));
	rcnt[3].mode = SONY_RC_COUNT_TO_TARGET;
	rcnt[3].target = psxRcntSonyBaseTarget();
	SaveFuncs.write(f, rcnt, sizeof(rcnt));
	SaveFuncs.write(f, n + marks[STATE_RCNT] + sizeof(rcnt), RCNT_SIZE - sizeof(rcnt));

	/* the MDEC: pcsx-ab counts the run-length pointers from psxM + 1 MB, and has no use for ours into the
	 * block buffer (it saved a pointer difference there) */
	memcpy(mdec, n + marks[STATE_MDEC], MDEC_SIZE);
	set32(mdec + MDEC_V_RL, get32(mdec + MDEC_V_RL) - SONY_MDEC_BASE);
	set32(mdec + MDEC_V_RL_END, get32(mdec + MDEC_V_RL_END) - SONY_MDEC_BASE);
	set32(mdec + MDEC_V_BLOCK, 0);
	SaveFuncs.write(f, mdec, MDEC_SIZE);

	SaveFuncs.write(f, n + marks[STATE_NDRC], marks[STATE_PAD] - marks[STATE_NDRC]);

	/* no disc change under way, as far as pcsx-ab is concerned */
	memset(disc, 0, sizeof(disc));
	SaveFuncs.write(f, disc, sizeof(disc));

	/* the extension: what the translation changed, as it was, and how wide Sony's SPU fields came out */
	records = 7;
	extra_len = sizeof(extra);
	SaveFuncs.write(f, EXT_MAGIC, 8);
	SaveFuncs.write(f, &records, sizeof(records));
	put_record(f, "SPUX", &extra_len, sizeof(extra_len));
	put_record(f, "ISTA", &native_istat, sizeof(native_istat));
	put_record(f, "REGS", n + marks[STATE_REGS], REGS_SIZE);
	put_record(f, "CDR ", n + marks[STATE_CDR], cdrFreezeSize());
	put_record(f, "RCNT", n + marks[STATE_RCNT], RCNT_SIZE);
	put_record(f, "MDEC", n + marks[STATE_MDEC], MDEC_SIZE);
	put_record(f, "PAD ", n + marks[STATE_PAD], marks[STATE_END] - marks[STATE_PAD]);

	SaveFuncs.close(f);
	free(cdr);
	free(hw_sony);
	return 0;
}

static int write_native(const char *file, const u8 *n, size_t len)
{
	void *f = SaveFuncs.open(file, "wb");
	if (f == NULL)
		return -1;
	SaveFuncs.write(f, n, len);
	SaveFuncs.close(f);
	return 0;
}

int SaveState(const char *file)
{
	struct PcsxSaveFuncs outer = SaveFuncs;
	int ret;

	capture.pos = capture.len = 0;
	capture.oom = 0;
	memset(marks, 0, sizeof(marks));
	capturing = 1;
	SaveFuncs = mem_funcs;
	ret = SaveStateNative((const char *)&capture);
	SaveFuncs = outer;
	capturing = 0;
	if (ret != 0 || capture.oom) {
		SysPrintf("savestate: could not build the state (%d%s)\n", ret, capture.oom ? ", out of memory" : "");
		return -1;
	}

	if (!sections_as_expected(capture.buf)) {
		/* a merge changed a section's size: better an upstream state than none */
		SysPrintf("savestate: sections not as pcsx-ab's, writing upstream's layout\n");
		return write_native(file, capture.buf, capture.len);
	}
	return write_sony(file, capture.buf);
}

/* --- loading ----------------------------------------------------------------------------------------- */

struct records {
	const u8 *istat, *regs, *cdr, *rcnt, *mdec, *spu_extra, *pad;
	u32 pad_len;
};

static int read_records(const u8 *p, size_t len, struct records *r)
{
	u32 count, i, rlen;

	memset(r, 0, sizeof(*r));
	if (len < 12 || memcmp(p, EXT_MAGIC, 8) != 0)
		return 0;
	count = get32(p + 8);
	p += 12;
	len -= 12;
	for (i = 0; i < count; i++) {
		if (len < 8)
			return -1;
		rlen = get32(p + 4);
		if (len - 8 < rlen)
			return -1;
		if (!memcmp(p, "ISTA", 4) && rlen == 4)
			r->istat = p + 8;
		else if (!memcmp(p, "REGS", 4) && rlen == REGS_SIZE)
			r->regs = p + 8;
		else if (!memcmp(p, "CDR ", 4) && rlen == (u32)cdrFreezeSize())
			r->cdr = p + 8;
		else if (!memcmp(p, "RCNT", 4) && rlen == RCNT_SIZE)
			r->rcnt = p + 8;
		else if (!memcmp(p, "MDEC", 4) && rlen == MDEC_SIZE)
			r->mdec = p + 8;
		else if (!memcmp(p, "SPUX", 4) && rlen == 4)
			r->spu_extra = p + 8;
		else if (!memcmp(p, "PAD ", 4)) {
			r->pad = p + 8;
			r->pad_len = rlen;
		}
		p += 8 + rlen;
		len -= 8 + rlen;
	}
	return 1;
}

/* pcsx-ab's stream in s -> upstream's in n. 1 = done, 0 = not pcsx-ab's layout (use s as it is), -1 = do not
 * load it */
static int sony_to_native(const u8 *s, size_t len, struct mem_stream *n)
{
	const size_t spu_off = HEAD_SIZE + PICTURE_SIZE + MEMORY_SIZE + REGS_SIZE
		+ sizeof(GPUFreeze_t) + SONY_GPU_EXTRA + VRAM_SIZE;
	const size_t gpu_off = HEAD_SIZE + PICTURE_SIZE + MEMORY_SIZE + REGS_SIZE;
	size_t sio_off, cdr_off, rcnt_off, mdec_off, ndrc_off, ndrc_len, disc_off, o;
	u32 spu_size, native_size;
	const u8 *spu, *part2;
	struct records rec;
	psxRegisters regs;
	SPUFreeze_t spu_hdr;
	u8 mdec[MDEC_SIZE];
	u8 *cdr;
	u32 extra;
	int ext;

	if (len < spu_off + 4 + sizeof(SPUFreeze_t) || strncmp((const char *)s, "STv4 PCSX", 9) != 0)
		return 0;
	spu = s + spu_off;
	spu_size = get32(spu);
	if (memcmp(spu + 4, "PBOSS", 6) != 0)
		return 0;	/* upstream's layout: its SPU blob starts 8 bytes earlier */
	if (spu_size < sizeof(SPUFreeze_t) + SPU_RAM_SIZE + sizeof(xa_decode_t) + SONY_SPU_EXTRA_64
	    || spu_size > len - spu_off - 4) {
		SysPrintf("savestate: a damaged SPU section\n");
		return -1;
	}
	sio_off = spu_off + 4 + spu_size;
	cdr_off = sio_off + SIO_SIZE;
	rcnt_off = cdr_off + cdrFreezeSize();
	mdec_off = rcnt_off + RCNT_SIZE;
	ndrc_off = mdec_off + MDEC_SIZE;
	if (len < ndrc_off) {
		SysPrintf("savestate: the state ends early\n");
		return -1;
	}
	ndrc_len = 0;
	if (len - ndrc_off >= 12 && memcmp(s + ndrc_off, NDRC_MAGIC, 8) == 0) {
		s32 blocks = (s32)get32(s + ndrc_off + 8);
		ndrc_len = 12 + (blocks > 0 ? (size_t)blocks : 0);
		if (ndrc_len > len - ndrc_off)
			ndrc_len = len - ndrc_off;
	}
	disc_off = ndrc_off + ndrc_len;
	o = disc_off + DISC_SIZE;
	ext = o < len ? read_records(s + o, len - o, &rec) : (memset(&rec, 0, sizeof(rec)), 0);
	if (ext < 0) {
		SysPrintf("savestate: a damaged extension\n");
		return -1;
	}

	if (s[HEAD_SIZE - 1] && !ext) {
		SysPrintf("savestate: pcsx-ab's state on the HLE BIOS, which cannot be continued here\n");
		return -1;
	}

	/* how wide Sony's three SPU fields are depends on the pointer size of the build that wrote the state: a
	 * console's or a 32-bit Pi's state on a 64-bit build (a Pi 64, a PC) has them 4 bytes shorter */
	if (rec.spu_extra)
		extra = get32(rec.spu_extra);
	else if (spu_size == SONY_SPU_FIXED + SONY_SPU_EXTRA_32 + SONY_SPU_OSS_SIZE)
		extra = SONY_SPU_EXTRA_32;
	else if (spu_size == SONY_SPU_FIXED + SONY_SPU_EXTRA_64 + SONY_SPU_OSS_SIZE)
		extra = SONY_SPU_EXTRA_64;
	else
		extra = sizeof(struct sony_spu_extra);
	if (extra != SONY_SPU_EXTRA_32 && extra != SONY_SPU_EXTRA_64) {
		SysPrintf("savestate: a damaged SPU section\n");
		return -1;
	}
	if (extra != sizeof(struct sony_spu_extra))
		SysPrintf("savestate: a %d-bit build's state\n", extra == SONY_SPU_EXTRA_32 ? 32 : 64);

	/* header, picture, RAM, BIOS, hardware registers - I_STAT as it was, for one of ours */
	mem_put(n, s, gpu_off - REGS_SIZE);
	if (rec.istat)
		memcpy(n->buf + HEAD_SIZE + PICTURE_SIZE + RAM_SIZE + ROM_SIZE + ISTAT_OFFSET, rec.istat, 4);

	if (rec.regs)
		mem_put(n, rec.regs, REGS_SIZE);
	else {
		memset(&regs, 0, sizeof(regs));
		memcpy(&regs, s + gpu_off - REGS_SIZE, REGS_SIZE);
		regs_from_sony(&regs);
		mem_put(n, &regs, REGS_SIZE);
	}

	/* the GPU without Sony's two words */
	mem_put(n, s + gpu_off, 8);
	mem_put(n, s + gpu_off + 8 + SONY_GPU_EXTRA, sizeof(GPUFreeze_t) - 8 + VRAM_SIZE);

	/* the SPU without Sony's three fields */
	native_size = spu_size - extra;
	memcpy(&spu_hdr, spu + 4, sizeof(spu_hdr));
	spu_hdr.Size = native_size;
	part2 = spu + 4 + sizeof(spu_hdr) + SPU_RAM_SIZE;
	mem_put(n, &native_size, sizeof(native_size));
	mem_put(n, &spu_hdr, sizeof(spu_hdr));
	mem_put(n, spu + 4 + sizeof(spu_hdr), SPU_RAM_SIZE);
	mem_put(n, part2, sizeof(xa_decode_t));
	mem_put(n, part2 + sizeof(xa_decode_t) + extra, native_size - sizeof(spu_hdr) - SPU_RAM_SIZE - sizeof(xa_decode_t));

	mem_put(n, s + sio_off, SIO_SIZE);

	if (rec.cdr)
		mem_put(n, rec.cdr, cdrFreezeSize());
	else {
		cdr = malloc(cdrFreezeSize());
		if (cdr == NULL)
			return -1;
		memcpy(cdr, s + cdr_off, cdrFreezeSize());
		cdrStateFromSony(cdr);
		mem_put(n, cdr, cdrFreezeSize());
		free(cdr);
	}

	mem_put(n, rec.rcnt ? rec.rcnt : s + rcnt_off, RCNT_SIZE);

	if (rec.mdec)
		mem_put(n, rec.mdec, MDEC_SIZE);
	else {
		memcpy(mdec, s + mdec_off, MDEC_SIZE);
		set32(mdec + MDEC_V_RL, get32(mdec + MDEC_V_RL) + SONY_MDEC_BASE);
		set32(mdec + MDEC_V_RL_END, get32(mdec + MDEC_V_RL_END) + SONY_MDEC_BASE);
		set32(mdec + MDEC_V_BLOCK, get32(mdec + MDEC_V_BLOCK) ? 1 : 0);	/* "a block was pending" */
		mem_put(n, mdec, MDEC_SIZE);
	}

	mem_put(n, s + ndrc_off, ndrc_len);
	if (rec.pad)
		mem_put(n, rec.pad, rec.pad_len);	/* none from pcsx-ab: the pads stay as they are */

	return n->oom ? -1 : 1;
}

/* the whole file, through the caller's SaveFuncs (gzread, or a buffer's reader that wants all or nothing) */
static int read_all(const char *file, struct mem_stream *m)
{
	u32 chunk = 1u << 18;
	void *f;
	int r;

	f = SaveFuncs.open(file, "rb");
	if (f == NULL)
		return -1;
	while (chunk > 0) {
		if (mem_reserve(m, m->len + chunk) != 0)
			break;
		r = SaveFuncs.read(f, m->buf + m->len, chunk);
		if (r > 0)
			m->len += r;
		else
			chunk >>= 1;
	}
	SaveFuncs.close(f);
	return m->oom ? -1 : 0;
}

int LoadState(const char *file)
{
	struct PcsxSaveFuncs outer = SaveFuncs;
	struct mem_stream in = { "(state)" }, native = { "(state)" };
	struct mem_stream *use;
	int ret;

	if (read_all(file, &in) != 0) {
		free(in.buf);
		return -1;
	}
	ret = sony_to_native(in.buf, in.len, &native);
	if (ret < 0) {
		free(in.buf);
		free(native.buf);
		return -1;
	}
	use = ret > 0 ? &native : &in;
	if (ret == 0)
		SysPrintf("savestate: not pcsx-ab's layout, loading it as upstream's\n");

	SaveFuncs = mem_funcs;
	ret = LoadStateNative((const char *)use);
	SaveFuncs = outer;

	free(in.buf);
	free(native.buf);
	return ret;
}

/* --- the autosave (state_sony.h) --------------------------------------------------------------------- */

static struct mem_stream autosave = { "(autosave)" };
static int autosave_valid;
static unsigned int autosave_ms;

int SaveStateAuto(unsigned int now_ms)
{
	struct PcsxSaveFuncs outer = SaveFuncs;
	int ret;

	autosave.pos = autosave.len = 0;
	autosave.oom = 0;
	SaveFuncs = mem_funcs;
	ret = SaveStateNative((const char *)&autosave);
	SaveFuncs = outer;
	if (ret != 0 || autosave.oom) {
		SysPrintf("autosave: could not build the state (%d%s)\n", ret, autosave.oom ? ", out of memory" : "");
		autosave_valid = 0;
		return -1;
	}
	autosave_valid = 1;
	autosave_ms = now_ms;
	return 0;
}

int LoadStateAuto(void)
{
	struct PcsxSaveFuncs outer = SaveFuncs;
	int ret;

	if (!autosave_valid)
		return -1;
	SaveFuncs = mem_funcs;
	ret = LoadStateNative((const char *)&autosave);
	SaveFuncs = outer;
	return ret;
}

long long StateAutoAge(void)
{
	return autosave_valid ? (long long)autosave_ms : -1;
}
