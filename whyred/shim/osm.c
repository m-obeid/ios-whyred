/*
 * whyred shim: read-only probe of the Gold cluster clock and voltage state (SDM660 OSM, CPRH,
 * Gold PLL). Never writes these blocks. Each group prints before the next is read, so a bus
 * stall shows where it happened. Addresses: whyred/research/recon/live.dts and Linux
 * clk-cpu-osm-660.c (notes in the session's osm-notes.md).
 */
#include <stdint.h>
#include "shim.h"

#define PERF_PLL        0x17816000UL    /* Gold PLL: mode +0, L +4, status +0x2c (bit 16 lock) */
#define PERF_RCGR       0x17811050UL    /* Gold mux: cmd +0, cfg +4 (src 10:8, div 4:0) */
#define CPRH_PERF       0x179c7aa0UL    /* +0 ctl (bit 0 OSM interface), +4 status */
#define SAW_PERF        0x17812c18UL    /* Gold SAW PMIC status */
#define APM_STATUS      0x179d0018UL
#define EFUSE_BIN       0x00784130UL    /* speed bin 31:29 */
#define OSM_CLK_CBCR    0x179d112cUL    /* OSM block clock (APCS_COMMON) */
#define OSM_PERF        0x179c2000UL    /* Gold OSM block = osm base + 0x2000 */

/* Dependent adds with the I-cache on: ticks of the 19.2 MHz counter for n iterations. */
uint64_t osm_speed_ticks(uint64_t n)
{
	uint64_t sctlr = rsr("sctlr_el1");
	wsr("sctlr_el1", sctlr | (1UL << 12));
	isb();
	uint64_t t = now(), x = 0;
	for (uint64_t i = 0; i < n; i++)
		__asm__ volatile("add %0, %0, #1" : "+r"(x));
	t = now() - t;
	wsr("sctlr_el1", sctlr);
	isb();
	return t;
}

/* The loop is one dependent add per cycle plus loop overhead: about 2 cycles per iteration. */
void osm_print_speed(const char *who, uint64_t ticks, uint64_t n)
{
	uint64_t mhz2 = ticks ? n * 2 * 192 / ticks / 10 : 0;  /* n*2 cycles / (ticks / 19.2 MHz) */
	puts_(who);
	kv(" ticks ", ticks, 8);
	puts_("~");
	dec((int64_t)mhz2);
	puts_(" MHz (2 cyc/iter)");
	con_newline();
}

void osm_probe(void)
{
	puts_("[gold clock, read-only]");
	con_newline();
	kv("pll mode ", rd32(PERF_PLL), 8);
	kv("L ", rd32(PERF_PLL + 4), 8);
	con_newline();
	uint32_t l = rd32(PERF_PLL + 4) & 0xff;
	kv("pll status ", rd32(PERF_PLL + 0x2c), 8);
	puts_("L*19.2=");
	dec((int64_t)(l * 192 / 10));
	puts_(" MHz");
	con_newline();
	kv("rcgr cmd ", rd32(PERF_RCGR), 8);
	kv("cfg ", rd32(PERF_RCGR + 4), 8);
	con_newline();
	kv("cprh ctl ", rd32(CPRH_PERF), 8);
	kv("status ", rd32(CPRH_PERF + 4), 8);
	con_newline();
	uint32_t cs = rd32(CPRH_PERF + 4);
	puts_("cprh corner ");
	dec(cs & 0x3f);
	puts_(" last mV ");
	dec((cs >> 6) & 0xfff);
	con_newline();
	kv("saw pmic ", rd32(SAW_PERF), 8);
	kv("apm ", rd32(APM_STATUS), 8);
	con_newline();
	uint32_t fuse = rd32(EFUSE_BIN);
	kv("efuse ", fuse, 8);
	puts_("bin ");
	dec(fuse >> 29);
	con_newline();
	uint32_t ocb = rd32(OSM_CLK_CBCR);
	kv("osm clk cbcr ", ocb, 8);
	con_newline();
	if (ocb & (1U << 31)) {
		puts_("osm clock off: OSM block not read");
		con_newline();
		return;
	}
	kv("osm enable ", rd32(OSM_PERF + 0x1004), 8);
	kv("desired ", rd32(OSM_PERF + 0x1f10), 8);
	con_newline();
	kv("pstate ", rd32(OSM_PERF + 0x1c00), 8);
	kv("cyc ctl ", rd32(OSM_PERF + 0x1f00), 8);
	kv("cyc ", rd32(OSM_PERF + 0x1f04), 8);
	con_newline();
	for (int i = 0; i < 5; i++) {
		uintptr_t row = OSM_PERF + 0x1150 + 0x20 * i;
		puts_("row ");
		dec(i);
		kv(" ", rd32(row), 8);
		kv("", rd32(row + 4), 8);
		kv("", rd32(row + 8), 8);
		con_newline();
	}
}

/* SCM over SMC64 (Linux scm.c, ARMv8 convention): x0 function, x1 argument info, x2.. arguments. */
struct scm_ret { uint64_t x0, x1, x2, x3; };

static struct scm_ret
scm_call(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t x0 __asm__("x0") = fn;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3;
	register uint64_t x4 __asm__("x4") = 0;
	register uint64_t x5 __asm__("x5") = 0;
	register uint64_t x6 __asm__("x6") = 0;
	__asm__ volatile("smc #0"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6)
			 :
			 : "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
	struct scm_ret r = { x0, x1, x2, x3 };
	return r;
}

#define SCM_IS_CALL_AVAIL       0xC2000601UL
#define SCM_IO_READ             0xC2000501UL

/* Stage A of the Gold clock port: check the spec's assumptions in the ABL state. Read-only. */
void osm_probe2(void)
{
	con_newline();
	puts_("[stage A: tz/pll/saw/scm, read-only]");
	con_newline();
	kv("spm_cc_ctrl ", rd32(OSM_PERF + 0x1028), 8);
	puts_("(tz seq setup: 000f0000)");
	con_newline();
	kv("seq1 mirror ", rd32(OSM_PERF + 0x1048), 8);
	con_newline();
	kv("pll user ", rd32(PERF_PLL + 0xc), 8);
	kv("cfg_lo ", rd32(PERF_PLL + 0x10), 8);
	kv("test_hi ", rd32(PERF_PLL + 0x1c), 8);
	con_newline();
	kv("saw avs ctl ", rd32(0x17812904UL), 8);
	kv("limit ", rd32(0x17812908UL), 8);
	con_newline();
	kv("apm ctl ", rd32(0x179d0010UL), 8);
	kv("osm rcg cfg ", rd32(0x179d1130UL), 8);
	con_newline();
	kv("gcc rbcpr cbcr ", rd32(0x148008UL), 8);
	kv("hmss_gpll0 cfg ", rd32(0x148060UL), 8);
	con_newline();
	struct scm_ret v = scm_call(SCM_IS_CALL_AVAIL, 1, 0x82000601UL, 0);
	kv("scm avail x0 ", v.x0, 8);
	kv("x1 ", v.x1, 8);
	con_newline();
	static const uint32_t seq[] = { 0x179c23dc, 0x179c23e0, 0x179c23e4, 0x179c23e8, 0x179c2380,
	    0x179c2420, 0x179c233c, 0x179c237c, 0x179c2430 };
	for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
		struct scm_ret r = scm_call(SCM_IO_READ, 1, seq[i], 0);
		kv("seq ", seq[i], 8);
		kv("-> ", r.x1, 8);
		kv("st ", r.x0, 8);
		con_newline();
	}
}

/* ---- stage B: bring up CPRH (open loop) and OSM for the Gold cluster, as Linux does ---- */

#define CPRH            0x179c4000UL
#define SCM_IO_WRITE    0xC2000502UL

static void wr(uintptr_t a, uint32_t v) { wr32(a, v); dsb(); }

static int rcg_update(uintptr_t cmd)
{
	wr(cmd, rd32(cmd) | 1u);
	for (int i = 0; i < 100000; i++)
		if (!(rd32(cmd) & 1u))
			return 0;
	return -1;
}

static void gold_state(const char *label)
{
	uint32_t cs = rd32(CPRH_PERF + 4);
	puts_(label);
	kv(" L ", rd32(PERF_PLL + 4) & 0xff, 2);
	kv("mux ", rd32(PERF_RCGR + 4), 3);
	kv("cprh corner ", cs & 0x3f, 2);
	puts_("mV ");
	dec((cs >> 6) & 0xfff);         /* CPRH_REG_STATUS [17:6]: last voltage, mV (Linux: 0xcc504 = 788) */
	kv(" saw ", rd32(SAW_PERF), 8);
	con_newline();
}

static int scm_write(uint32_t pa, uint32_t v)
{
	for (int tries = 0; tries < 100; tries++) {
		struct scm_ret r = scm_call(SCM_IO_WRITE, 2, pa, v);
		if (r.x0 == 0)
			return 0;
		if (r.x0 != 1) {
			kv("!! scm write ", pa, 8);
			kv("err ", r.x0, 8);
			con_newline();
			return -1;
		}
	}
	return -1;
}

/* Values from whyred/research/recon/osmdump-linux*.txt (Linux on this phone, speed bin 3). */
static const uint32_t cprh_corner[8] = {
	0x0e00414b, 0x0e00414b, 0x0e1d4f59, 0x0e3e616b, 0x0e44616e, 0x0e697282, 0x0e837d87, 0x20007676,
};
static const uint32_t lut[5][4] = {     /* FREQ, VOLT, OVERRIDE, SPARE; rows 4..39 repeat row 4 */
	{ 0x0004000f, 0x000002bc, 0x01200020, 1 },
	{ 0x0404003a, 0x000102bc, 0x052e002e, 1 },
	{ 0x04040049, 0x000202f4, 0x073a003a, 2 },
	{ 0x0404005b, 0x0003033c, 0x09480048, 2 },
	{ 0x0404005e, 0x00040348, 0x094b004b, 2 },
};

int osm_bringup(unsigned target)
{
	con_newline();
	puts_("[stage B: cprh open loop + osm, target row ");
	dec(target);
	puts_("]");
	con_newline();
	gold_state("start ");

	/* 1. hmss_gpll0 branch to 300 MHz: Gold (on GPLL0) halves before anything else changes. */
	wr(0x148060, 0x103);
	if (rcg_update(0x14805c))
		return puts_("!! gpll0 rcg"), con_newline(), -1;
	/* 2. OSM block clock 200 MHz; 3. CPR clock on. */
	wr(0x179d1130, 0x102);
	if (rcg_update(0x179d112c))
		return puts_("!! osm rcg"), con_newline(), -1;
	wr(0x148008, rd32(0x148008) | 1u);
	gold_state("clk   ");

	/* 4. CPRH, open loop: Linux's values with MARGIN_ADJ_CTL bit 4 (hardware closed loop) clear. */
	wr(CPRH + 0x00c, 0x17700);
	wr(CPRH + 0x014, 0x38c);
	wr(CPRH + 0x004, 0x1c1e);                       /* loop off while configuring */
	wr(CPRH + 0x0bc, 0x13);
	wr(CPRH + 0x808, 0x4202);
	wr(CPRH + 0x80c, 0xff7f);
	wr(CPRH + 0x700, 0x4);
	wr(CPRH + 0x7a4, 0x21);
	wr(CPRH + 0x7a8, 0x00844b00);                   /* settle count 33 */
#ifdef CPRH_CL
	wr(CPRH + 0x85c, 0x1de);                        /* thread 0 RO 7 target quotient (Linux dump) */
	wr(CPRH + 0x7f8, 0x4090);                       /* PMIC step 4, hardware closed loop */
#else
	wr(CPRH + 0x7f8, 0x4080);                       /* PMIC step 4, open loop */
#endif
	for (int i = 0; i < 8; i++)
		wr(CPRH + 0x3a00 + 4 * i, cprh_corner[i]);
	wr(0x17812904, 0x01010031);                     /* SAW AVS ctl and limit, as spm_devices.c */
	wr(0x17812908, 0x04580458);
	wr(CPRH + 0x3aa0, 0x080a00c9);                  /* OSM interface on */
	wr(CPRH + 0x004, 0x1c1f);                       /* loop enable */
	gold_state("cprh  ");

	/* 5. OSM normal-world registers (perf block). */
	uintptr_t o = OSM_PERF;
	wr(o + 0x1f00, 0x113);
	for (int i = 0; i < 40; i++) {
		const uint32_t *r = lut[i < 4 ? i : 4];
		uintptr_t row = o + 0x1150 + 0x20 * i;
		wr(row + 0x0, (uint32_t)i);
		wr(row + 0x4, r[0]);
		wr(row + 0x8, r[1]);
		wr(row + 0xc, r[2]);
		wr(row + 0x14, r[3]);
	}
	wr(o + 0x101c, 0x00c800c8);
	wr(o + 0x100c, 0);
	wr(o + 0x1024, 0);
	wr(o + 0x1020, 0);
	wr(o + 0x102c, 0xffffffff);
	wr(o + 0x1030, 0xffffffff);
	wr(o + 0x1034, 0);
	wr(o + 0x1070, rd32(o + 0x1070) | 0x3f);
	static const uint32_t t3[3] = { 0x03e807d0, 0x07d007d0, 0xc8 };
	for (int b = 0; b < 3; b++)     /* CC, DCVS, PS boost timers */
		for (int k = 0; k < 3; k++)
			wr(o + 0x1074 + 0x10 * b + 4 * k, t3[k]);
	wr(o + 0x10a0, 5);
	wr(o + 0x10ac, 0x00640064);
	wr(o + 0x10b0, 0x00320032);
	wr(o + 0x10bc, 1);
	wr(o + 0x10a8, 1);
	wr(o + 0x10b8, 0x64);
	wr(o + 0x10a4, rd32(o + 0x10a4) | 0x80020008);
	wr(o + 0x10c0, rd32(o + 0x10c0) | 1);
	wr(o + 0x1048, 0x27);
	gold_state("osm   ");

	/* 6. TrustZone sequencer: MEM-ACC and APM thresholds, through SCM. */
	static const uint32_t seq[9][2] = {
		{ 0x179c23dc, 1 }, { 0x179c23e0, 2 }, { 0x179c23e4, 0 }, { 0x179c23e8, 1 },
		{ 0x179c2380, 0x3a }, { 0x179c2420, 7 }, { 0x179c233c, 0x27 }, { 0x179c237c, 0x26 },
		{ 0x179c2430, 0x9f9 },
	};
	for (int i = 0; i < 9; i++)
		if (scm_write(seq[i][0], seq[i][1]))
			return -1;
	puts_("scm writes ok");
	con_newline();

	/* 7. Enable at row 0, then the target row. */
	wr(o + 0x1f10, 0);
	delay_ms(1);
	wr(o + 0x1004, 1);
	delay_ms(5);
	gold_state("row 0 ");
	for (unsigned r = 1; r <= target; r++) {
		wr(o + 0x1f10, r);
		delay_ms(5);
		puts_("row ");
		dec(r);
		gold_state(" ");
	}
#ifdef CPRH_CL
	/* Closed loop: the voltage should settle from the open-loop ceiling toward Linux's ~788 mV. */
	for (int i = 0; i < 5; i++) {
		delay_ms(200);
		gold_state("cl    ");
	}
#endif
	return 0;
}
