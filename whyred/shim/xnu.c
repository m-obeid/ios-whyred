/*
 * whyred shim, milestone 2: act as iBoot for XNU on a Gold core.
 * The payload format comes from tools/mkpayload.py (topaz). mkpayload already resolved
 * the kernel's chained fixups, so this copies flat segments, places the ADT and
 * boot_args behind the kernel and jumps with the MMU and caches off.
 */
#include <stdint.h>
#include "shim.h"

/* GICv3 from the whyred DT: interrupt-controller@17a00000 */
#define GICD_BASE		0x17a00000UL
#define GICR_BASE		0x17b00000UL
#define GICR_SIZE		0x100000UL
#define GICR_PE_SIZE		0x20000UL
#define GICR_TYPER		0x08
#define GICR_WAKER		0x14
#define GICR_SGI		0x10000
#define GICR_IGROUPR0		(GICR_SGI + 0x080)
#define GICR_ISENABLER0		(GICR_SGI + 0x100)
#define GICR_IPRIORITYR		(GICR_SGI + 0x400)

/* DT timer: <1 2> phys = INTID 18, <1 3> virt = INTID 19 (same as topaz) */
#define VTIMER_INTID		19
#define PTIMER_INTID		18
#define SGI_INTID		1

#define BOOT_LINE_LENGTH	608
#define PAGE_16K		0x4000UL
#define EMBED_MAX		0x10000000UL	/* upper bound when the payload follows the Image */

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision 2 / Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

extern char _end[];

static int magic_at(uintptr_t a)
{
	static const char want[] = "SWEETXNU";
	const volatile char *m = (const volatile char *)a;
	for (int i = 0; i < 8; i++)
		if (m[i] != want[i])
			return 0;
	return 1;
}

/* MMU off: everything is Device memory, so copy in aligned 8-byte words. */
static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	const volatile uint64_t *s = (const volatile uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

/* Value of a /chosen property holding one u32 or u64 cell group. */
static int fdt_chosen_num(uintptr_t dtb, const char *prop, uint64_t *out)
{
	const volatile uint8_t *b = (const volatile uint8_t *)dtb;
	if (be32(b) != 0xd00dfeed)
		return 0;
	const volatile uint8_t *p = b + be32(b + 8);
	const volatile uint8_t *strs = b + be32(b + 12);
	const volatile uint8_t *end = b + be32(b + 4);
	int depth = 0, in_chosen = 0;

	while (p < end) {
		uint32_t tok = be32(p);
		p += 4;
		if (tok == 1) {
			depth++;
			if (depth == 2 && streq(p, "chosen"))
				in_chosen = 1;
			while (*p)
				p++;
			p = align4(p + 1);
		} else if (tok == 2) {
			if (depth == 2)
				in_chosen = 0;
			depth--;
		} else if (tok == 3) {
			uint32_t len = be32(p);
			const volatile uint8_t *data = p + 8;
			if (in_chosen && depth == 2 && streq(strs + be32(p + 4), prop)) {
				if (len == 4)
					*out = be32(data);
				else if (len == 8)
					*out = (uint64_t)be32(data) << 32 | be32(data + 4);
				else
					return 0;
				return 1;
			}
			p = align4(data + len);
		} else if (tok != 4) {
			break;
		}
	}
	return 0;
}

/* The payload follows the Image (4K aligned) or arrives as the boot image ramdisk. */
int payload_find(uintptr_t dtb, uintptr_t *start, uintptr_t *end)
{
	uintptr_t emb = ((uintptr_t)_end + 0xfff) & ~0xfffUL;
	if (magic_at(emb)) {
		*start = emb;
		*end = emb + EMBED_MAX;
		return 1;
	}
	uint64_t lo, hi;
	if (!fdt_chosen_num(dtb, "linux,initrd-start", &lo) || !fdt_chosen_num(dtb, "linux,initrd-end", &hi))
		return 0;
	if ((lo & 7) || !magic_at(lo))
		return 0;
	*start = lo;
	*end = hi;
	return 1;
}

static uintptr_t gicr_for(uint32_t aff)
{
	for (uintptr_t off = 0; off < GICR_SIZE; off += GICR_PE_SIZE) {
		uint64_t typer = rd64(GICR_BASE + off + GICR_TYPER);
		if ((uint32_t)(typer >> 32) == aff)
			return GICR_BASE + off;
		if (typer & (1 << 4))	/* Last */
			break;
	}
	return 0;
}

static void gicr_setup(uintptr_t r)
{
	uint32_t waker = rd32(r + GICR_WAKER);
	if (waker & 2) {
		wr32(r + GICR_WAKER, waker & ~2U);
		for (int i = 0; i < 100000 && (rd32(r + GICR_WAKER) & 4); i++)
			;
	}
	wr32(r + GICR_IGROUPR0, 0xffffffff);	/* NS view: secure bits are WI */
	for (int id = 0; id < 32; id++)
		wr8(r + GICR_IPRIORITYR + id, 0xa0);
	wr32(r + GICR_ISENABLER0, (1U << VTIMER_INTID) | (1U << PTIMER_INTID) | (1U << SGI_INTID));
}

/*
 * topaz found XNU hangs after initCPU on Gold without this: distributor Group 1 + ARE_NS,
 * every Gold redistributor awake with SGIs/PPIs in Group 1, and the CPU interface enabled.
 */
void gic_prepare(void)
{
	wsr("icc_sre_el1", rsr("icc_sre_el1") | 1);
	isb();
	uint32_t was = rd32(GICD_BASE);
	wr32(GICD_BASE, was | 0x13);
	for (int i = 0; i < 1000000 && (rd32(GICD_BASE) & (1U << 31)); i++)
		;
	kv("gicd ", was, 8);
	kv("-> ", rd32(GICD_BASE), 8);
	con_newline();

	for (uint32_t aff = 0x100; aff <= 0x103; aff++) {
		uintptr_t r = gicr_for(aff);
		puts_("gicr ");
		hex(aff, 3);
		if (!r) {
			puts_(" MISSING");
			con_newline();
			continue;
		}
		gicr_setup(r);
		kv(" at ", r, 8);
		kv("en ", rd32(r + GICR_ISENABLER0), 8);
		con_newline();
	}

	wsr("icc_ctlr_el1", rsr("icc_ctlr_el1") & ~2ULL);	/* EOImode 0 */
	wsr("icc_pmr_el1", 0xf0);
	wsr("icc_bpr1_el1", 0);
	wsr("icc_igrpen1_el1", 1);
	isb();
}

const char *xnu_boot(uintptr_t start, uintptr_t end)
{
	const struct payload *p = (const struct payload *)start;
	if (p->version != 1 || (p->adt_off & 7))
		return "bad payload header";

	uint64_t pa_of_va = p->phys_base - p->virt_base;	/* pa = va + this */
	kv("payload ", start, 9);
	kv("end ", end, 9);
	con_newline();
	kv("phys ", p->phys_base, 9);
	kv("size ", p->mem_size, 9);
	con_newline();
	kv("virt ", p->virt_base, 16);
	con_newline();
	kv("entry ", p->entry_va, 16);
	con_newline();

	if (end > p->phys_base && start < p->phys_base + p->mem_size)
		return "payload overlaps kernel memory";
	if ((uintptr_t)_end > p->phys_base && (uintptr_t)_end < p->phys_base + p->mem_size)
		return "shim overlaps kernel memory";

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va;
		copy64(dst, start + s->file_off, s->file_size);
		uint64_t done = (s->file_size + 7) & ~7UL;
		if (s->vmsize > done)
			zero64(dst + done, s->vmsize - done);
	}
	kv("segs ", p->nsegs, 2);
	kv("kend ", p->kernel_end + pa_of_va, 9);
	con_newline();

	/* ADT, then boot_args, behind the kernel. Everything below topOfKernelData is the kernel's. */
	uintptr_t adt_pa = p->kernel_end + pa_of_va;
	copy64(adt_pa, start + p->adt_off, p->adt_size);
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uintptr_t top = ba_pa + PAGE_16K;

	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = fb_base;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = fb_stride;
	ba->Video.v_width = fb_w;
	ba->Video.v_height = fb_h;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = adt_pa - pa_of_va;	/* XNU reads it with the MMU on: a VA */
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = p->mem_size;
	kv("bootargs ", ba_pa, 9);
	kv("top ", top, 9);
	con_newline();

	uintptr_t entry_pa = p->entry_va + pa_of_va;
	kv("jump ", entry_pa, 9);
	puts_("(fbcon clears the screen)");
	con_newline();
	delay_ms(1500);

	/* Everything was written with the MMU and caches off. Drop stale I-cache lines too. */
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((void (*)(uint64_t))entry_pa)(ba_pa);
	return "xnu returned";
}
