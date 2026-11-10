// SPDX-License-Identifier: GPL-2.0
/*
 * Read-only dump of the SDM660 CPU clock/voltage hardware as Linux configured it (OSM, CPRH,
 * cluster PLLs and muxes), for the iOS port. Reads only register offsets the Linux drivers
 * define (clk-cpu-osm-660.c, cpr3-regulator.c): sweeping whole windows took synchronous external
 * aborts on unimplemented or TrustZone-only offsets. A marker precedes each group so a fault
 * shows its location in pstore. Never writes.
 */
#include <linux/module.h>
#include <linux/io.h>

static void dump(const char *grp, phys_addr_t base, const u32 *offs, int n)
{
	void __iomem *b = ioremap(base, 0x4000);
	int i;

	pr_info("OSMDUMP > %s %08llx\n", grp, (unsigned long long)base);
	if (!b)
		return;
	for (i = 0; i < n; i++)
		pr_info("OSMDUMP %s +%04x %08x\n", grp, offs[i], readl_relaxed(b + offs[i]));
	iounmap(b);
}

static const u32 osm_cfg[] = {
	0x1004, 0x100c, 0x101c, 0x1020, 0x1024, 0x1028, 0x102c, 0x1030, 0x1034, 0x1038, 0x103c,
	0x1040, 0x1048, 0x1070, 0x1074, 0x1078, 0x107c, 0x1084, 0x1088, 0x108c, 0x1094, 0x1098,
	0x109c, 0x10a0, 0x10a4, 0x10a8, 0x10ac, 0x10b0, 0x10b4, 0x10b8, 0x10bc, 0x10c0,
	0x1c00, 0x1c74, 0x1f00, 0x1f04, 0x1f10, 0x1f38, 0x1f3c,
};
static const u32 pll[] = { 0x0, 0x4, 0xc, 0x10, 0x1c, 0x2c };
static const u32 rcg[] = { 0x0, 0x4 };
static const u32 one[] = { 0x0 };
static const u32 cprh[] = { 0x4, 0x8, 0xc, 0x14, 0x3aa0, 0x3aa4, 0x3aa8 };	/* +0x3000 aborts */

static int __init osmdump_init(void)
{
	u32 lut[40 * 5], corners[40];
	int c, i;

	for (i = 0; i < 40; i++) {
		lut[5 * i + 0] = 0x1150 + 0x20 * i;     /* INDEX */
		lut[5 * i + 1] = 0x1154 + 0x20 * i;     /* FREQ */
		lut[5 * i + 2] = 0x1158 + 0x20 * i;     /* VOLT */
		lut[5 * i + 3] = 0x115c + 0x20 * i;     /* OVERRIDE */
		lut[5 * i + 4] = 0x1164 + 0x20 * i;     /* SPARE */
		corners[i] = 0x3a00 + 4 * i;            /* CPRH_REG_CORNER(i) */
	}
	dump("perfcl_pll", 0x17816000, pll, ARRAY_SIZE(pll));
	dump("perfcl_rcg", 0x17811050, rcg, ARRAY_SIZE(rcg));
	dump("pwrcl_pll", 0x17916000, pll, ARRAY_SIZE(pll));
	dump("pwrcl_rcg", 0x17911050, rcg, ARRAY_SIZE(rcg));
	dump("perfcl_saw", 0x17812c18, one, 1);
	dump("osm_clk", 0x179d112c, one, 1);
	for (c = 0; c < 2; c++) {
		dump(c ? "osm_perf" : "osm_pwr", 0x179c0000 + 0x2000 * c, osm_cfg, ARRAY_SIZE(osm_cfg));
		dump(c ? "osm_perf_lut" : "osm_pwr_lut", 0x179c0000 + 0x2000 * c, lut, ARRAY_SIZE(lut));
	}
	dump("cprh", 0x179c4000, cprh, ARRAY_SIZE(cprh));
	dump("cprh_corner", 0x179c4000, corners, ARRAY_SIZE(corners));
	pr_info("OSMDUMP end\n");
	return 0;
}

static void __exit osmdump_exit(void) { }

module_init(osmdump_init);
module_exit(osmdump_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only dump of SDM660 OSM/CPRH state");
