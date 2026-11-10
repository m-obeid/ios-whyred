/*
 * Modem (MSS, Hexagon Q6 v62-1-5) self-authenticating boot, step 1a: power the Q6 up and run
 * MBA from the ramdisk (MSS=1 ./build.sh ../firmware/modem/mba.mbn). Follows Linux
 * drivers/soc/qcom/pil-msa.c (pil_mss_reset_load_mba, pil_mss_reset) and pil-q6v5.c
 * (__pil_q6v55_reset, qdsp6v62_1_5) with whyred's DT (qcom,mss@4080000).
 * Success: RMB PBL status 1, then MBA status 1 or 2 (XPU unlocked).
 */
#include "shim.h"

#define GCC             0x00100000UL
#define MSS_RESTART     0x00179000UL    /* GCC_MSS_RESTART (DT restart_reg) */
#define Q6              0x04080000UL    /* qdsp6_base */
#define RMB             0x04180000UL    /* rmb_base */
#define MBA_PA          0x94800000UL    /* pil_mba_region (2 MB) */
#define SCM_BUF         0xa1000000UL    /* free RAM below the XNU base, for SCM arguments */

#define QDSP6SS_RESET           0x014
#define QDSP6SS_GFMUX_CTL       0x020
#define QDSP6SS_PWR_CTL         0x030
#define QDSP6V6SS_MEM_PWR_CTL   0x034
#define QDSP6SS_XO_CBCR         0x038
#define QDSP6V62SS_BHS_STATUS   0x0c4
#define QDSP6SS_DBG_CFG         0x018

#define RMB_MBA_IMAGE           0x00
#define RMB_PBL_STATUS          0x04
#define RMB_MBA_COMMAND         0x08
#define RMB_MBA_STATUS          0x0c
#define RMB_PMI_META_DATA       0x10
#define RMB_PMI_CODE_START      0x14
#define RMB_PMI_CODE_LENGTH     0x18
#define RMB_PROTOCOL_VERSION    0x1c
#define RMB_MBA_DEBUG           0x20

#define VMID_HLOS               3
#define VMID_MSS_MSA            0xf
#define PERM_RW                 (4 | 2)

/* SMC with eight registers in and out (Qualcomm SCM, SMCCC with extra args in x5). */
static void smc8(uint64_t a[8])
{
	register uint64_t x0 __asm__("x0") = a[0], x1 __asm__("x1") = a[1], x2 __asm__("x2") = a[2],
	    x3 __asm__("x3") = a[3], x4 __asm__("x4") = a[4], x5 __asm__("x5") = a[5],
	    x6 __asm__("x6") = a[6], x7 __asm__("x7") = a[7];
	__asm__ volatile("smc #0"
	    : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
	    :
	    : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
	a[0] = x0; a[1] = x1; a[2] = x2; a[3] = x3; a[4] = x4; a[5] = x5; a[6] = x6; a[7] = x7;
}

/* hyp_assign_phys (SCM 0x0C/0x16), HLOS -> one VMID read-write; topaz TopazModem.cpp scm_assign. */
static uint64_t scm_assign(uint64_t addr, uint64_t size, uint32_t vmid, uint64_t *res)
{
	volatile uint64_t *p = (volatile uint64_t *)SCM_BUF;
	for (int i = 0; i < 64; i++)
		p[i] = 0;
	p[0] = addr;                                    /* 0x000: mem map {addr, size} */
	p[1] = size;
	*(volatile uint32_t *)(SCM_BUF + 0x40) = VMID_HLOS;     /* 0x040: source VMIDs */
	*(volatile uint32_t *)(SCM_BUF + 0x80) = vmid;          /* 0x080: {vmid, perm, ctx(8), ctx_size, 0} */
	*(volatile uint32_t *)(SCM_BUF + 0x84) = PERM_RW;
	p[32] = 4;                                      /* 0x100: extended args: source list size */
	p[33] = SCM_BUF + 0x80;                         /* dest list */
	p[34] = 24;                                     /* dest list size */
	p[35] = 0;
	dsb();
	uint64_t ai = 7 | (1 << 4) | (1 << 8) | (1 << 12);
	uint64_t a[8] = { 0x42000c16, ai, SCM_BUF, 16, SCM_BUF + 0x40, SCM_BUF + 0x100, 0, 0 };
	smc8(a);
	for (unsigned n = 0; a[0] == 1 && n < 100000; n++) {    /* INTERRUPTED: resume with the session id */
		uint64_t b[8] = { 1, ai, SCM_BUF, 16, SCM_BUF + 0x40, SCM_BUF + 0x100, a[6], 0 };
		for (int i = 0; i < 8; i++)
			a[i] = b[i];
		smc8(a);
	}
	*res = a[1];
	return a[0];
}

static void rmb_dump(const char *label)
{
	puts_(label);
	kv(" pbl ", rd32(RMB + RMB_PBL_STATUS), 8);
	kv("mba ", rd32(RMB + RMB_MBA_STATUS), 8);
	kv("cmd ", rd32(RMB + RMB_MBA_COMMAND), 8);
	kv("ver ", rd32(RMB + RMB_PROTOCOL_VERSION), 8);
	kv("dbg ", rd32(RMB + RMB_MBA_DEBUG), 8);
	con_newline();
}

static void q6_dump(const char *label)
{
	puts_(label);
	kv(" reset ", rd32(Q6 + QDSP6SS_RESET), 8);
	kv("pwr ", rd32(Q6 + QDSP6SS_PWR_CTL), 8);
	kv("mem ", rd32(Q6 + QDSP6V6SS_MEM_PWR_CTL), 8);
	kv("gfmux ", rd32(Q6 + QDSP6SS_GFMUX_CTL), 8);
	kv("bhs ", rd32(Q6 + QDSP6V62SS_BHS_STATUS), 8);
	con_newline();
}

/* Branch clock on (enable bit), then wait for CBCR bit 31 (clock off) to clear. Prints only failures. */
static int clk_on(const char *name, uintptr_t en, uint32_t bit, uintptr_t cbcr)
{
	wr32(en, rd32(en) | bit);
	dsb();
	for (int i = 0; i < 1000; i++) {
		if (!(rd32(cbcr) & (1u << 31)))
			return 0;
		delay_ms(1);
	}
	puts_("!! clock off: ");
	puts_(name);
	con_newline();
	return 1;
}

/* One letter per step of the power-up, so the last letter on screen names a faulting access. */
static void step(char c)
{
	con_putc(c);
}

static int wait_nonzero(uintptr_t reg, unsigned ms, uint32_t *out)
{
	for (unsigned i = 0; i <= ms; i++) {
		uint32_t v = rd32(reg);
		if (v) {
			*out = v;
			return 0;
		}
		delay_ms(1);
	}
	*out = 0;
	return -1;
}

void mss_probe(uintptr_t mba, uintptr_t mba_end)
{
	uint64_t size = mba_end - mba;
	kv("mba.mbn bytes ", size, 8);
	kv("first word ", rd32(mba), 8);
	con_newline();

	/* 1. Clocks, DT order: iface, bus, mem, gpll0_mss, snoc_axi, mnoc_axi. MSS registers abort
	 *    (synchronous external abort) until cfg_ahb runs: dump only after this. */
	int bad = clk_on("cfg_ahb", GCC + 0x8a000, 1, GCC + 0x8a000);
	bad += clk_on("bimc_q6", GCC + 0x4401c, 1, GCC + 0x4401c);
	bad += clk_on("rom", GCC + 0x52004, 1u << 10, GCC + 0x38004);
	wr32(GCC + 0x5200c, rd32(GCC + 0x5200c) | (1u << 2));  /* gpll0_out_msscc (halt check: delay) */
	bad += clk_on("snoc", GCC + 0x8a03c, 1, GCC + 0x8a03c);
	bad += clk_on("mnoc", GCC + 0x8a004, 1, GCC + 0x8a004);
	puts_(bad ? "clocks: SOME OFF" : "clocks: all on");
	con_newline();
	q6_dump("before");
	rmb_dump("before");

	/* 2. MBA into its region (1 MB slot), then give that memory to the modem's VM. */
	if (size > 0x100000) {
		puts_("!! mba.mbn larger than 1 MB");
		con_newline();
		return;
	}
	for (uint64_t o = 0; o < size; o += 4)
		wr32(MBA_PA + o, rd32(mba + o));
	dsb();
	kv("copied, check ", rd32(MBA_PA), 8);
	uint64_t res;
	uint64_t r = scm_assign(MBA_PA, 0x100000, VMID_MSS_MSA, &res);
	kv("assign ret ", r, 8);
	kv("res ", res, 8);
	con_newline();
	if (r != 0) {
		puts_("!! hyp_assign failed");
		con_newline();
		return;
	}

	step('A');
	/* 3. MSS restart pulse (pil_mss_assert_resets / deassert_resets). */
	uint32_t dbg = rd32(Q6 + QDSP6SS_DBG_CFG);
	wr32(MSS_RESTART, 1);
	dsb();
	delay_ms(1);
	wr32(MSS_RESTART, 0);
	dsb();
	delay_ms(1);
	wr32(Q6 + QDSP6SS_DBG_CFG, dbg);

	step('B');
	/* 4. RMB: MBA image address, no debug policy. */
	wr32(RMB + RMB_MBA_IMAGE, MBA_PA);
	wr32(RMB + RMB_PMI_CODE_START, 0);
	wr32(RMB + RMB_PMI_CODE_LENGTH, 0);
	dsb();

	/* 5. __pil_q6v55_reset, qdsp6v62_1_5 path. */
	step('C');
	uint32_t v = rd32(Q6 + QDSP6SS_RESET);
	wr32(Q6 + QDSP6SS_RESET, v | 0x7);              /* CORE_ARES | BUS_ARES_ENA | STOP_CORE */
	wr32(Q6 + QDSP6SS_XO_CBCR, rd32(Q6 + QDSP6SS_XO_CBCR) | 1);
	int i;
	for (i = 0; i < 1000 && (rd32(Q6 + QDSP6SS_XO_CBCR) & (1u << 31)); i++)
		delay_ms(1);
	step('D');
	kv("xo cbcr ", rd32(Q6 + QDSP6SS_XO_CBCR), 8);
	v = rd32(Q6 + QDSP6SS_PWR_CTL) | (1u << 24);   /* BHS_ON */
	wr32(Q6 + QDSP6SS_PWR_CTL, v);
	dsb();
	for (i = 0; i < 1000 && !(rd32(Q6 + QDSP6V62SS_BHS_STATUS) & 1); i++)
		delay_ms(1);
	kv("bhs ", rd32(Q6 + QDSP6V62SS_BHS_STATUS), 8);
	con_newline();
	step('E');
	v |= 1u << 25;                                  /* LDO_BYP */
	wr32(Q6 + QDSP6SS_PWR_CTL, v);
	v = rd32(Q6 + QDSP6SS_PWR_CTL) & ~(1u << 22);   /* CLAMP_QMC_MEM off */
	wr32(Q6 + QDSP6SS_PWR_CTL, v);
	v |= (1u << 18) | (1u << 19);                   /* L2DATA_STBY_N | SLP_RET_N */
	wr32(Q6 + QDSP6SS_PWR_CTL, v);
	step('F');
	v = rd32(Q6 + QDSP6V6SS_MEM_PWR_CTL);
	for (int b = 29; b >= 0; b--) {                 /* memory banks one at a time */
		v |= 1u << b;
		wr32(Q6 + QDSP6V6SS_MEM_PWR_CTL, v);
		for (volatile int d = 0; d < 2000; d++)
			;
	}
	step('G');
	v = rd32(Q6 + QDSP6SS_PWR_CTL) & ~(1u << 21);   /* CLAMP_WL off */
	wr32(Q6 + QDSP6SS_PWR_CTL, v);
	v &= ~(1u << 20);                               /* CLAMP_IO off */
	wr32(Q6 + QDSP6SS_PWR_CTL, v);
	step('H');
	v = rd32(Q6 + QDSP6SS_RESET) & ~0x3u;           /* CORE_ARES and STOP_CORE off: run */
	wr32(Q6 + QDSP6SS_RESET, v);
	step('I');
	wr32(Q6 + QDSP6SS_GFMUX_CTL, rd32(Q6 + QDSP6SS_GFMUX_CTL) | (1u << 1));  /* CLK_ENA */
	dsb();
	step('J');
	con_newline();
	q6_dump("q6 up");

	/* 6. pil_msa_wait_for_mba_ready: PBL, then MBA. */
	uint32_t st;
	if (wait_nonzero(RMB + RMB_PBL_STATUS, 1000, &st)) {
		puts_("!! PBL timed out");
		con_newline();
		rmb_dump("pbl");
		return;
	}
	kv("PBL status ", st, 8);
	puts_(st == 1 ? "(success) " : "(UNEXPECTED) ");
	if (wait_nonzero(RMB + RMB_MBA_STATUS, 1000, &st)) {
		puts_("!! MBA timed out");
		con_newline();
		rmb_dump("mba");
		return;
	}
	kv("MBA status ", st, 8);
	puts_(st == 1 || st == 2 ? "XPU UNLOCKED: MBA RUNS" : "(UNEXPECTED)");
	con_newline();
	rmb_dump("after");
}
