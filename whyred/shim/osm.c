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
