/*
 * whyred shim, milestone 1: probe the hardware and report on the splash screen.
 * Runs with the MMU off, so every access is Device memory: no unaligned access, no atomics.
 */
#include <stdint.h>
#include "shim.h"
#include "font.h"

/* Addresses from whyred/research/recon/whyred-facts.md */
#define MDP_BASE		0x0c900000UL	/* qcom,mdss_mdp mdp_phys */
#define IMEM_RESTART_REASON	0x146bf65cUL	/* msm-imem@146bf000 restart_reason@65c */
#define RESTART_BOOTLOADER	0x77665500U
#define PSHOLD			0x010ac000UL

/* MDP5 SSPP register offsets */
#define SSPP_SRC_SIZE		0x00
#define SSPP_SRC0_ADDR		0x14
#define SSPP_SRC_YSTRIDE0	0x24
#define SSPP_SRC_FORMAT		0x30
#define SSPP_SRC_UNPACK		0x34

#define REBOOT_AFTER_S		90

extern char _head[];
extern void secondary_entry(void);

/* The compiler emits calls to these for struct copies even when freestanding. */
void *memset(void *d, int c, unsigned long n)
{
	volatile uint8_t *p = d;
	while (n--)
		*p++ = (uint8_t)c;
	return d;
}

void *memcpy(void *d, const void *s, unsigned long n)
{
	volatile uint8_t *p = d;
	const volatile uint8_t *q = s;
	while (n--)
		*p++ = *q++;
	return d;
}

uint64_t smc(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t x0 __asm__("x0") = fn;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3;
	__asm__ volatile("smc #0"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
			 :
			 : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
			   "x12", "x13", "x14", "x15", "x16", "x17", "memory");
	return x0;
}

uint64_t now(void)
{
	__asm__ volatile("isb");
	return rsr("cntvct_el0");
}

void delay_ms(uint64_t ms)
{
	uint64_t end = now() + rsr("cntfrq_el0") / 1000 * ms;
	while (now() < end)
		;
}

/* ---- framebuffer console ---- */

/* Defaults until the MDP probe finds the live scanout setup. */
uintptr_t fb_base = SPLASH_BASE;
uint32_t fb_w = 1080, fb_h = 2160, fb_cpp = 3, fb_stride = 1080 * 3;

#define SCALE	2
#define CW	(8 * SCALE)
#define CH	(16 * SCALE)
#define MARGIN	12

static uint32_t cx, cy;

static void put_px(uint32_t x, uint32_t y, uint8_t g)
{
	volatile uint8_t *p = (volatile uint8_t *)(fb_base + (uintptr_t)y * fb_stride + (uintptr_t)x * fb_cpp);
	for (uint32_t i = 0; i < fb_cpp; i++)
		p[i] = g;
}

/* Fill rows [y0, y1) with one byte value. Works for any pixel format. */
static void fill_rows(uint32_t y0, uint32_t y1, uint8_t g)
{
	uint32_t word = g * 0x01010101U;
	uint32_t row_bytes = fb_w * fb_cpp;
	for (uint32_t y = y0; y < y1 && y < fb_h; y++) {
		uintptr_t row = fb_base + (uintptr_t)y * fb_stride;
		uint32_t i = 0;
		if ((row & 3) == 0)
			for (; i + 4 <= row_bytes; i += 4)
				wr32(row + i, word);
		for (; i < row_bytes; i++)
			*(volatile uint8_t *)(row + i) = g;
	}
}

/* Before the probe: fill the whole splash carve-out so the screen changes whatever the format. */
static void fill_splash(uint8_t g)
{
	uint32_t word = g * 0x01010101U;
	for (uintptr_t a = SPLASH_BASE; a < SPLASH_BASE + 1080UL * 2160 * 4; a += 4)
		wr32(a, word);
}

static void con_clear(void)
{
	fill_rows(0, fb_h, 0);
	cx = cy = 0;
}

void con_newline(void)
{
	cx = 0;
	cy++;
	if (MARGIN + (cy + 1) * CH > fb_h)
		cy = 0;
}

void con_putc(char c)
{
	if (c == '\n') {
		con_newline();
		return;
	}
	if (MARGIN + (cx + 1) * CW > fb_w)
		con_newline();
	if (c >= 0x20 && c < 0x7f) {
		const unsigned char *g = font8x16[c - 0x20];
		uint32_t x0 = MARGIN + cx * CW, y0 = MARGIN + cy * CH;
		for (uint32_t r = 0; r < 16; r++)
			for (uint32_t b = 0; b < 8; b++)
				if (g[r] & (0x80 >> b))
					for (uint32_t sy = 0; sy < SCALE; sy++)
						for (uint32_t sx = 0; sx < SCALE; sx++)
							put_px(x0 + b * SCALE + sx, y0 + r * SCALE + sy, 0xff);
	}
	cx++;
}

void con_putc_safe(char c)
{
	con_putc(c >= 0x20 && c < 0x7f ? c : '.');
}

void puts_(const char *s)
{
	while (*s)
		con_putc(*s++);
}

void hex(uint64_t v, int digits)
{
	for (int i = digits - 1; i >= 0; i--)
		con_putc("0123456789abcdef"[(v >> (i * 4)) & 0xf]);
}

void dec(int64_t v)
{
	char buf[24];
	int n = 0;
	if (v < 0) {
		con_putc('-');
		v = -v;
	}
	do {
		buf[n++] = '0' + v % 10;
		v /= 10;
	} while (v);
	while (n)
		con_putc(buf[--n]);
}

void kv(const char *k, uint64_t v, int digits)
{
	puts_(k);
	hex(v, digits);
	con_putc(' ');
}

/* ---- MDP probe ---- */

static const uint32_t pipe_off[] = { 0x5000, 0x7000, 0x25000, 0x27000, 0x29000 };

struct pipe { uint32_t size, addr, stride, fmt, unpack; };
static struct pipe pipes[5];
static int fb_pipe = -1;

static void mdp_probe(void)
{
	for (int i = 0; i < 5; i++) {
		uintptr_t b = MDP_BASE + pipe_off[i];
		pipes[i].size = rd32(b + SSPP_SRC_SIZE);
		pipes[i].addr = rd32(b + SSPP_SRC0_ADDR);
		pipes[i].stride = rd32(b + SSPP_SRC_YSTRIDE0);
		pipes[i].fmt = rd32(b + SSPP_SRC_FORMAT);
		pipes[i].unpack = rd32(b + SSPP_SRC_UNPACK);
	}
	for (int i = 0; i < 5; i++) {
		struct pipe *p = &pipes[i];
		uint32_t w = p->size & 0xffff, h = p->size >> 16;
		uint32_t cpp = ((p->fmt >> 9) & 3) + 1;
		uint32_t stride = p->stride & 0xffff;
		if (p->addr < 0x80000000U || w == 0 || h == 0 || stride < w * cpp)
			continue;
		fb_base = p->addr;
		fb_w = w;
		fb_h = h;
		fb_cpp = cpp;
		fb_stride = stride;
		fb_pipe = i;
		return;
	}
}

/* ---- device tree ---- */

uint32_t be32(const volatile uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

int streq(const volatile uint8_t *a, const char *b)
{
	while (*b)
		if (*a++ != (uint8_t)*b++)
			return 0;
	return *a == 0;
}

static int starts(const volatile uint8_t *a, const char *b)
{
	while (*b)
		if (*a++ != (uint8_t)*b++)
			return 0;
	return 1;
}

const volatile uint8_t *align4(const volatile uint8_t *p)
{
	return (const volatile uint8_t *)(((uintptr_t)p + 3) & ~(uintptr_t)3);
}

static void fdt_report(uintptr_t dtb)
{
	const volatile uint8_t *b = (const volatile uint8_t *)dtb;
	puts_("DTB ");
	hex(dtb, 9);
	if (be32(b) != 0xd00dfeed) {
		puts_(" BAD MAGIC\n");
		return;
	}
	uint32_t total = be32(b + 4);
	puts_(" size ");
	dec(total);
	con_newline();

	const volatile uint8_t *p = b + be32(b + 8);
	const volatile uint8_t *strs = b + be32(b + 12);
	const volatile uint8_t *end = b + total;
	int depth = 0, mem_depth = 0;

	while (p < end) {
		uint32_t tok = be32(p);
		p += 4;
		if (tok == 1) {
			depth++;
			if (depth == 2 && starts(p, "memory"))
				mem_depth = depth;
			while (*p)
				p++;
			p = align4(p + 1);
		} else if (tok == 2) {
			if (mem_depth == depth)
				mem_depth = 0;
			depth--;
		} else if (tok == 3) {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			const volatile uint8_t *data = p + 8;
			const volatile uint8_t *name = strs + nameoff;
			if (depth == 1 && streq(name, "model")) {
				puts_("model ");
				for (uint32_t i = 0; i + 1 < len && i < 80; i++)
					con_putc((char)data[i]);
				con_newline();
			}
			if (mem_depth && depth == mem_depth && streq(name, "reg")) {
				for (uint32_t i = 0; i + 16 <= len; i += 16) {
					uint64_t base = (uint64_t)be32(data + i) << 32 | be32(data + i + 4);
					uint64_t size = (uint64_t)be32(data + i + 8) << 32 | be32(data + i + 12);
					puts_("mem ");
					hex(base, 9);
					puts_(" +");
					hex(size, 9);
					con_newline();
				}
			}
			p = align4(data + len);
		} else if (tok == 4) {
			continue;
		} else {
			break;	/* 9 = end, anything else = corrupt */
		}
	}
}

/* ---- secondary CPUs ---- */

struct cpu_report {
	volatile uint64_t done, midr, mpidr, mmfr0, el, t;
};
static struct cpu_report rep[8];

/* Set by the boot core before it wakes Gold 0x100 (slot 4) to boot XNU. */
static volatile uint64_t handoff_start, handoff_end;

static void __attribute__((noreturn)) gold_boot(void)
{
	uint64_t mmfr0 = rsr("id_aa64mmfr0_el1");
	con_newline();
	puts_("[gold]");
	con_newline();
	kv("midr ", rsr("midr_el1"), 8);
	kv("mpidr ", rsr("mpidr_el1") & 0xffffff, 3);
	puts_(((mmfr0 >> 20) & 0xf) ? "16K=YES" : "16K=NO");
	con_newline();
	if ((mmfr0 >> 20) & 0xf) {
		gic_prepare();
		puts_("!! ");
		puts_(xnu_boot(handoff_start, handoff_end));
	} else {
		puts_("!! no 16K granule, not booting");
	}
	con_newline();
	for (;;)
		__asm__ volatile("wfe");
}

void secondary_main(uint64_t slot)
{
	if (slot == 4 && handoff_start)
		gold_boot();
	struct cpu_report *r = &rep[slot & 7];
	r->t = now();
	r->midr = rsr("midr_el1");
	r->mpidr = rsr("mpidr_el1");
	r->mmfr0 = rsr("id_aa64mmfr0_el1");
	r->el = (rsr("CurrentEL") >> 2) & 3;
	dsb();
	r->done = 1;
	dsb();
	/* Park, do not CPU_OFF: turning off the last Gold core powers down the cluster. */
	for (;;)
		__asm__ volatile("wfe");
}

static const char *tgran16(uint64_t mmfr0)
{
	uint32_t f = (mmfr0 >> 20) & 0xf;
	return f == 0 ? "16K=NO " : f == 1 ? "16K=YES" : f == 2 ? "16K=52b" : "16K=?? ";
}

static void cpu_line(const char *who, uint64_t mpidr, uint64_t midr, uint64_t mmfr0, uint64_t el)
{
	puts_(who);
	hex(mpidr & 0xffffff, 3);
	puts_(" midr ");
	hex(midr, 8);
	puts_(" EL");
	dec((int64_t)el);
	con_putc(' ');
	puts_(tgran16(mmfr0));
	puts_(" mmfr0 ");
	hex(mmfr0, 16);
	con_newline();
}

static void start_cpus(void)
{
	static const uint32_t mpidrs[8] = { 0x000, 0x001, 0x002, 0x003, 0x100, 0x101, 0x102, 0x103 };
	uint64_t self = rsr("mpidr_el1") & 0xffffff;
	uintptr_t entry = (uintptr_t)secondary_entry;
	uint64_t freq = rsr("cntfrq_el0");
	int64_t ret[8];
	uint64_t t0[8];

	/* Wake all cores back to back, as XNU will, then wait up to 1 s for reports. */
	for (int i = 0; i < 8; i++) {
		ret[i] = 1;
		if (mpidrs[i] == self)
			continue;
		t0[i] = now();
		ret[i] = (int64_t)smc(PSCI_CPU_ON, mpidrs[i], entry, (uint64_t)i);
	}
	uint64_t until = now() + freq;
	for (;;) {
		int pending = 0;
		for (int i = 0; i < 8; i++)
			if (ret[i] == 0 && !rep[i].done)
				pending = 1;
		if (!pending || now() > until)
			break;
	}

	for (int i = 0; i < 8; i++) {
		if (ret[i] == 1)
			continue;
		if (ret[i] != 0 || !rep[i].done) {
			puts_("cpu ");
			hex(mpidrs[i], 3);
			puts_(" CPU_ON=");
			dec(ret[i]);
			puts_(rep[i].done ? "" : " no report in 1 s");
			con_newline();
			continue;
		}
		cpu_line("cpu ", rep[i].mpidr, rep[i].midr, rep[i].mmfr0, rep[i].el);
		puts_("     up after ");
		dec((int64_t)((rep[i].t - t0[i]) * 1000000 / freq));
		puts_(" us");
		con_newline();
	}
}

/* ---- pixel format test ---- */

/* One bar per byte position of a pixel. The colour each bar shows gives the channel order. */
static void channel_bars(void)
{
	uint32_t y0 = MARGIN + (cy + 1) * CH;
	if (y0 + 4 * CH > fb_h)
		return;
	for (uint32_t k = 0; k < fb_cpp && k < 4; k++) {
		uint32_t x0 = MARGIN + k * 260;
		for (uint32_t y = y0; y < y0 + 2 * CH; y++)
			for (uint32_t x = x0; x < x0 + 220; x++) {
				volatile uint8_t *p = (volatile uint8_t *)(fb_base + (uintptr_t)y * fb_stride + (uintptr_t)x * fb_cpp);
				for (uint32_t i = 0; i < fb_cpp; i++)
					p[i] = i == k ? 0xff : 0x00;
			}
	}
	cy += 3;
	con_newline();
	puts_("bars = byte 0, 1, 2(, 3) of a pixel");
	con_newline();
}

/* ---- reset ---- */

static void __attribute__((noreturn)) reboot_to_fastboot(void)
{
	wr32(IMEM_RESTART_REASON, RESTART_BOOTLOADER);
	dsb();
	smc(PSCI_SYSTEM_RESET, 0, 0, 0);
	wr32(PSHOLD, 0);
	for (;;)
		__asm__ volatile("wfe");
}

/* ---- exceptions ---- */

void exc_handler(uint64_t slot)
{
	uint64_t el = (rsr("CurrentEL") >> 2) & 3;
	uint64_t esr, elr, far;
	if (el == 2) {
		esr = rsr("esr_el2");
		elr = rsr("elr_el2");
		far = rsr("far_el2");
	} else {
		esr = rsr("esr_el1");
		elr = rsr("elr_el1");
		far = rsr("far_el1");
	}
	con_newline();
	puts_("!! EXCEPTION slot ");
	dec((int64_t)slot);
	puts_(" mpidr ");
	hex(rsr("mpidr_el1") & 0xffffff, 3);
	con_newline();
	kv("esr ", esr, 8);
	con_newline();
	kv("elr ", elr, 16);
	puts_("(off ");
	hex(elr - (uintptr_t)_head, 6);
	puts_(")");
	con_newline();
	kv("far ", far, 16);
	con_newline();
	puts_("halted. hold power + vol- for fastboot");
	for (;;)
		__asm__ volatile("wfe");
}

/* ---- main ---- */

void shim_main(uint64_t dtb)
{
	/* Stage marker: a dark grey screen that never changes means the MDP probe hung. */
	fill_splash(0x30);
	mdp_probe();
	con_clear();

	puts_("ios-whyred shim m2");
	con_newline();

	uintptr_t ps, pe;
	if (payload_find(dtb, &ps, &pe)) {
		puts_("xnu payload found, handing off to Gold 0x100");
		con_newline();
		handoff_end = pe;
		handoff_start = ps;
		dsb();
		int64_t r = (int64_t)smc(PSCI_CPU_ON, 0x100, (uintptr_t)secondary_entry, 4);
		if (r == 0)
			for (;;)	/* Silver parks with DAIF masked. Never CPU_OFF: see whyred-facts.md */
				__asm__ volatile("wfi");
		handoff_start = 0;
		puts_("!! CPU_ON 0x100 failed: ");
		dec(r);
		con_newline();
	} else {
#ifdef EMMC_PROBE
		puts_("emmc probe (read-only)");
		con_newline();
		con_newline();
		emmc_probe();
		con_newline();
		puts_("reset in ");
		dec(REBOOT_AFTER_S);
		puts_(" s (hold vol- for fastboot)");
		con_newline();
		delay_ms(REBOOT_AFTER_S * 1000);
		reboot_to_fastboot();
#endif
		puts_("no xnu payload: diagnostics only");
		con_newline();
	}
	kv("EL", (rsr("CurrentEL") >> 2) & 3, 1);
	kv("load", (uintptr_t)_head, 9);
	con_newline();
	kv("sctlr", rsr("sctlr_el1"), 8);
	kv("cntfrq", rsr("cntfrq_el0"), 8);
	con_newline();
	con_newline();

	puts_("[cpu]");
	con_newline();
	cpu_line("boot ", rsr("mpidr_el1"), rsr("midr_el1"), rsr("id_aa64mmfr0_el1"), (rsr("CurrentEL") >> 2) & 3);
	kv("isar0 ", rsr("id_aa64isar0_el1"), 16);
	con_newline();
	kv("mmfr1 ", rsr("id_aa64mmfr1_el1"), 16);
	con_newline();
	con_newline();

	puts_("[psci]");
	con_newline();
	kv("ver ", smc(PSCI_VERSION, 0, 0, 0), 8);
	con_newline();
	puts_("feat cpu_on ");
	dec((int64_t)smc(PSCI_FEATURES, PSCI_CPU_ON, 0, 0));
	puts_(" reset ");
	dec((int64_t)smc(PSCI_FEATURES, PSCI_SYSTEM_RESET, 0, 0));
	puts_(" reset2 ");
	dec((int64_t)smc(PSCI_FEATURES, PSCI_SYSTEM_RESET2, 0, 0));
	con_newline();
	con_newline();

	puts_("[mdp]");
	con_newline();
	kv("hw ", rd32(MDP_BASE), 8);
	kv("mdp ", rd32(MDP_BASE + 0x1000), 8);
	con_newline();
	for (int i = 0; i < 5; i++) {
		struct pipe *p = &pipes[i];
		if (p->size == 0 && p->addr == 0)
			continue;
		puts_(i < 2 ? "vig" : "dma");
		dec(i < 2 ? i : i - 2);
		con_putc(' ');
		kv("a", p->addr, 8);
		kv("sz", p->size, 8);
		con_newline();
		puts_("     ");
		kv("st", p->stride, 8);
		kv("f", p->fmt, 8);
		kv("u", p->unpack, 8);
		con_newline();
	}
	puts_(fb_pipe < 0 ? "fb: no live pipe, defaults" : "fb: from live pipe");
	con_newline();
	kv("fb ", fb_base, 8);
	puts_("w ");
	dec(fb_w);
	puts_(" h ");
	dec(fb_h);
	puts_(" cpp ");
	dec(fb_cpp);
	puts_(" st ");
	dec(fb_stride);
	con_newline();
	con_newline();

	puts_("[dtb]");
	con_newline();
	fdt_report(dtb);
	con_newline();

	puts_("[smp]");
	con_newline();
	start_cpus();
	con_newline();

	channel_bars();
	con_newline();

	puts_("reboot to fastboot in ");
	dec(REBOOT_AFTER_S);
	puts_(" s");
	con_newline();
	delay_ms(REBOOT_AFTER_S * 1000);
	reboot_to_fastboot();
}
