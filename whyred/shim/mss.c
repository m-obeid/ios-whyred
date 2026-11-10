/*
 * Modem (MSS, Hexagon Q6 v62-1-5) self-authenticating boot, step 1a: power the Q6 up and run
 * MBA from the ramdisk (MSS=1 ./build.sh ../firmware/modem/mba.mbn), or with a firmware pack
 * (tools/pack-modem.py) also steps 1b/1c: metadata and segments. Follows Linux
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

/* hyp_assign_phys (SCM 0x0C/0x16), HLOS -> n VMIDs read-write; topaz TopazModem.cpp scm_assign. */
static uint64_t scm_assign(uint64_t addr, uint64_t size, const uint32_t *vmid, unsigned n, uint64_t *res)
{
	volatile uint64_t *p = (volatile uint64_t *)SCM_BUF;
	for (int i = 0; i < 64; i++)
		p[i] = 0;
	p[0] = addr;                                    /* 0x000: mem map {addr, size} */
	p[1] = size;
	*(volatile uint32_t *)(SCM_BUF + 0x40) = VMID_HLOS;     /* 0x040: source VMIDs */
	for (unsigned i = 0; i < n; i++) {             /* 0x080: {vmid, perm, ctx(8), ctx_size, 0} each */
		*(volatile uint32_t *)(SCM_BUF + 0x80 + 24 * i) = vmid[i];
		*(volatile uint32_t *)(SCM_BUF + 0x84 + 24 * i) = PERM_RW;
	}
	p[32] = 4;                                      /* 0x100: extended args: source list size */
	p[33] = SCM_BUF + 0x80;                         /* dest list */
	p[34] = 24 * n;                                 /* dest list size */
	p[35] = 0;
	dsb();
	uint64_t ai = 7 | (1 << 4) | (1 << 8) | (1 << 12);
	uint64_t a[8] = { 0x42000c16, ai, SCM_BUF, 16, SCM_BUF + 0x40, SCM_BUF + 0x100, 0, 0 };
	smc8(a);
	for (unsigned k = 0; a[0] == 1 && k < 100000; k++) {    /* INTERRUPTED: resume with the session id */
		uint64_t b[8] = { 1, ai, SCM_BUF, 16, SCM_BUF + 0x40, SCM_BUF + 0x100, a[6], 0 };
		for (int i = 0; i < 8; i++)
			a[i] = b[i];
		smc8(a);
	}
	*res = a[1];
	return a[0];
}

/* Firmware pack (whyred/tools/pack-modem.py): "WMDM", count, {name[24], off, size}. */
static uintptr_t pack_base;
static int pack_find(const char *name, uintptr_t *data, uint32_t *size)
{
	if (!pack_base || rd32(pack_base) != 0x4d444d57)        /* "WMDM" */
		return 0;
	uint32_t count = rd32(pack_base + 4);
	for (uint32_t i = 0; i < count; i++) {
		uintptr_t e = pack_base + 8 + 32 * i;
		int j = 0;
		for (; name[j]; j++)
			if (*(volatile uint8_t *)(e + j) != (uint8_t)name[j])
				break;
		if (name[j] == 0 && *(volatile uint8_t *)(e + j) == 0) {
			*data = pack_base + rd32(e + 24);
			*size = rd32(e + 28);
			return 1;
		}
	}
	return 0;
}

static void copy_bytes(uintptr_t dst, uintptr_t src, uint32_t n)
{
	uint32_t i = 0;
	for (; i + 4 <= n; i += 4)
		wr32(dst + i, rd32(src + i));
	for (; i < n; i++)
		*(volatile uint8_t *)(dst + i) = *(volatile uint8_t *)(src + i);
}

static void zero_bytes(uintptr_t dst, uint32_t n)
{
	uint32_t i = 0;
	for (; i < n && ((dst + i) & 3); i++)
		*(volatile uint8_t *)(dst + i) = 0;
	for (; i + 4 <= n; i += 4)
		wr32(dst + i, 0);
	for (; i < n; i++)
		*(volatile uint8_t *)(dst + i) = 0;
}

/*
 * A firmware file to dst: from the pack when the ramdisk is one (pack-modem.py), else from the
 * stock "modem" partition (FAT, image/NAME) read by ADMA2 straight into dst. 0 on success.
 */
static int fat_ok;
static int fw_get(const char *name, uintptr_t dst, uint32_t max, uint32_t *size)
{
	uintptr_t data;
	uint32_t n, clus;
	if (pack_base) {
		if (!pack_find(name, &data, &n) || n > max)
			return -1;
		copy_bytes(dst, data, n);
		*size = n;
		return 0;
	}
	if (!fat_ok || fat_lookup("image", name, &clus, &n) || n > max)
		return -1;
	*size = n;
	return fat_read(clus, n, dst);
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

static void modem_load(void);

void mss_probe(uintptr_t rd, uintptr_t rd_end)
{
	kv("ramdisk ", rd, 8);
	kv("end ", rd_end, 8);
	con_newline();
	pack_base = rd32(rd) == 0x4d444d57 ? rd : 0;
	/* ABL chooses where the ramdisk lands: a pack must not overlap what this probe writes. */
	static const uintptr_t busy[3][2] = {
		{ 0x8ac00000, 0x92200000 }, { 0x94800000, 0x94a00000 }, { 0xa1000000, 0xa1400000 },
	};
	for (int i = 0; pack_base && i < 3; i++)
		if (rd < busy[i][1] && rd_end > busy[i][0]) {
			kv("!! ramdisk overlaps ", busy[i][0], 8);
			con_newline();
			return;
		}
	if (!pack_base) {
		puts_("firmware from the modem partition: ");
		fat_ok = fat_mount("modem") == 0;
		if (!fat_ok)
			return;
	}
	uint32_t size32;
	if (fw_get("mba.mbn", MBA_PA, 0x100000, &size32)) {
		puts_("!! mba.mbn not found");
		con_newline();
		return;
	}
	uint64_t size = size32;
	kv("mba.mbn bytes ", size, 8);
	kv("first word ", rd32(MBA_PA), 8);
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

	/* 2. MBA is already in its region (1 MB slot): give that memory to the modem's VM. */
	uint64_t res;
	static const uint32_t msa[1] = { VMID_MSS_MSA };
	uint64_t r = scm_assign(MBA_PA, 0x100000, msa, 1, &res);
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
	/* No Q6SS reads from here on: once the core runs, apps reads of 0x4080000 abort (measured,
	 * FAR 0x4080014, ESR 0x96000210). Linux only polls the RMB registers. */

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
	if (st == 1 || st == 2)
		modem_load();
}

/* Signed RMB status: negative values are MBA error codes. */
static int wait_status(uint32_t want, unsigned ms, int32_t *got)
{
	for (unsigned i = 0; i <= ms; i++) {
		int32_t v = (int32_t)rd32(RMB + RMB_MBA_STATUS);
		if (v == (int32_t)want || v < 0) {
			*got = v;
			return v == (int32_t)want ? 0 : -1;
		}
		delay_ms(1);
	}
	*got = (int32_t)rd32(RMB + RMB_MBA_STATUS);
	return -1;
}

#define MDATA_PA        (MBA_PA + 0x100000)     /* after MBA in pil_mba_region, as Linux allocates */
#define MODEM_PA        0x8ac00000UL            /* modem_fw_region */
#define MODEM_SIZE      0x7600000UL             /* lowest..highest loadable segment of modem.mdt */
#define MDT_COPY        0xa1200000UL            /* free RAM below the XNU base */

/*
 * Step 1b: pil_msa_auth_modem_mdt. Step 1c: pil_boot's segment loop (sequential, "qcom,sequential-
 * fw-load") with pil_msa_mba_verify_blob after each, then pil_msa_mba_auth.
 */
static void modem_load(void)
{
	/* Our own copy for the program headers: MDATA_PA belongs to the modem after the assign. */
	uintptr_t mdt = MDT_COPY;
	uint32_t mdt_size;
	if (fw_get("modem.mdt", mdt, 0x100000, &mdt_size)) {
		puts_("!! modem.mdt not found");
		con_newline();
		return;
	}
	copy_bytes(MDATA_PA, mdt, mdt_size);
	dsb();
	static const uint32_t msa[1] = { VMID_MSS_MSA };
	uint64_t res, r = scm_assign(MDATA_PA, (mdt_size + 0xfff) & ~0xfffU, msa, 1, &res);
	kv("mdt ", mdt_size, 4);
	kv("assign ", r, 2);
	wr32(RMB + RMB_PMI_CODE_LENGTH, 0);
	wr32(RMB + RMB_PMI_META_DATA, MDATA_PA);
	wr32(RMB + RMB_MBA_COMMAND, 1);                 /* CMD_META_DATA_READY */
	dsb();
	int32_t st;
	int bad = wait_status(3, 10000, &st);
	kv("meta status ", (uint32_t)st, 8);
	puts_(bad ? "FAILED" : "HEADERS OK");
	con_newline();
	if (bad)
		return;

	/* pil_assign_mem_to_subsys_and_linux: the whole region to MSS_MSA and HLOS. */
	static const uint32_t both[2] = { VMID_MSS_MSA, VMID_HLOS };
	r = scm_assign(MODEM_PA, MODEM_SIZE, both, 2, &res);
	kv("region assign ", r, 2);
	con_newline();
	if (r != 0)
		return;

	uint32_t phoff = rd32(mdt + 28), phnum = *(volatile uint16_t *)(mdt + 44);
	uint32_t loaded = 0, bytes = 0;
	for (uint32_t i = 0; i < phnum; i++) {
		uintptr_t ph = mdt + phoff + 32 * i;
		uint32_t type = rd32(ph), pa = rd32(ph + 12), filesz = rd32(ph + 16), memsz = rd32(ph + 20);
		uint32_t flags = rd32(ph + 24);
		if (type != 1 || ((flags >> 24) & 7) == 2 || memsz == 0)
			continue;                       /* PT_LOAD and not the hash segment */
		if (pa < MODEM_PA || pa + memsz > MODEM_PA + MODEM_SIZE) {
			kv("!! segment outside region ", i, 2);
			con_newline();
			return;
		}
		if (filesz) {
			char name[10] = "modem.b00";
			name[7] = (char)('0' + i / 10);
			name[8] = (char)('0' + i % 10);
			uint32_t got;
			if (fw_get(name, pa, filesz, &got) || got != filesz) {
				puts_("!! missing, short or unreadable ");
				puts_(name);
				con_newline();
				return;
			}
		}
		zero_bytes(pa + filesz, memsz - filesz);
		dsb();
		/* pil_msa_mba_verify_blob */
		uint32_t len = rd32(RMB + RMB_PMI_CODE_LENGTH);
		if (len == 0) {
			wr32(RMB + RMB_PMI_CODE_START, pa);
			wr32(RMB + RMB_MBA_COMMAND, 2);         /* CMD_LOAD_READY */
		}
		wr32(RMB + RMB_PMI_CODE_LENGTH, len + memsz);
		dsb();
		st = (int32_t)rd32(RMB + RMB_MBA_STATUS);
		if (st < 0) {
			kv("!! MBA error at segment ", i, 2);
			kv("status ", (uint32_t)st, 8);
			con_newline();
			return;
		}
		loaded++;
		bytes += filesz;
		con_putc('.');
	}
	con_newline();
	kv("segments ", loaded, 2);
	kv("bytes ", bytes, 8);
	con_newline();
	bad = wait_status(4, 10000, &st);               /* STATUS_AUTH_COMPLETE */
	kv("auth status ", (uint32_t)st, 8);
	puts_(bad ? "FAILED" : "MODEM AUTHENTICATED: RUNNING");
	con_newline();
	rmb_dump("final");
}
