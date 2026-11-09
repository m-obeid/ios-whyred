/*
 * whyred shim, milestone 3a: read-only eMMC probe on the SDCC1 state ABL leaves behind.
 * Polled PIO through the standard SDHCI registers, no DMA, no interrupts. Never writes the card.
 */
#include <stdint.h>
#include "shim.h"

#define GCC_BASE		0x00100000UL
#define GCC_SDCC1_APPS_CBCR	(GCC_BASE + 0x16004)
#define GCC_SDCC1_AHB_CBCR	(GCC_BASE + 0x16008)
#define CBCR_CLK_OFF		(1U << 31)

#define HC			0x0c0c4000UL	/* sdhci@c0c4000 hc_mem */
#define CQ			0x0c0c5000UL	/* cqhci_mem */

/* SDHCI standard registers */
#define BLKSZ_CNT		0x04	/* block size [11:0], SDMA boundary [14:12], block count [31:16] */
#define ARG			0x08
#define XFER_CMD		0x0c	/* transfer mode [15:0], command [31:16] */
#define RESP0			0x10
#define DATA			0x20
#define PRESENT			0x24
#define HOSTCTL			0x28	/* host control 1, power, block gap, wakeup */
#define CLKCTL			0x2c	/* clock control [15:0], timeout [23:16], software reset [31:24] */
#define INTSTS			0x30	/* normal [15:0], error [31:16] */
#define INTEN			0x34
#define SIGEN			0x38
#define HOSTCTL2		0x3c	/* auto CMD error [15:0], host control 2 [31:16] */
#define ADMA_ERR		0x54
#define ADMA_ADDR_LO		0x58
#define ADMA_ADDR_HI		0x5c
#define CAPS0			0x40
#define CAPS1			0x44
#define VERSION			0xfc	/* host version in [31:16] */

#define INT_CMD_DONE		(1U << 0)
#define INT_XFER_DONE		(1U << 1)
#define INT_READ_READY		(1U << 5)
#define INT_ERROR		(1U << 15)

/* transfer mode bits */
#define TM_DMA			(1U << 0)
#define TM_BLKCNT_EN		(1U << 1)
#define TM_AUTO_CMD12		(1U << 2)
#define TM_READ			(1U << 4)
#define TM_MULTI		(1U << 5)

/* command register: index [13:8], data present 5, index check 4, CRC check 3, 48-bit response 2 */
#define CMD_R1(idx)		(((uint32_t)(idx) << 8) | 0x1a)
#define CMD_R1_DATA(idx)	(((uint32_t)(idx) << 8) | 0x3a)

/* The card's relative address is whatever ABL assigned: Linux uses 1, Qualcomm LK uses 2. */
static uint32_t rca = 2;

static uint8_t sector[512];

static uint32_t hc_rd(uint32_t off) { return rd32(HC + off); }
static void hc_wr(uint32_t off, uint32_t v) { wr32(HC + off, v); }

/* Poll INTSTS for any of the bits in want, or an error, for up to ms milliseconds. */
static int wait_int(uint32_t want, uint64_t ms, uint32_t *sts)
{
	uint64_t until = now() + rsr("cntfrq_el0") / 1000 * ms;
	uint32_t s;
	do {
		s = hc_rd(INTSTS);
		if (s & (want | INT_ERROR))
			break;
	} while (now() < until);
	*sts = s;
	return (s & INT_ERROR) || !(s & want) ? -1 : 0;
}

static void reset_cmd_dat(void)
{
	hc_wr(CLKCTL, hc_rd(CLKCTL) | (0x6U << 24));
	for (int i = 0; i < 100000 && (hc_rd(CLKCTL) & (0x6U << 24)); i++)
		;
}

static int fail(const char *what, uint32_t sts)
{
	puts_("!! ");
	puts_(what);
	kv(" sts ", sts, 8);
	con_newline();
	hc_wr(INTSTS, 0xffffffff);
	reset_cmd_dat();
	return -1;
}

static int idle_lines(void)
{
	for (int i = 0; i < 1000000; i++)
		if (!(hc_rd(PRESENT) & 3))	/* CMD and DAT inhibit clear */
			return 0;
	return -1;
}

static int cmd13_quiet(uint32_t r, uint32_t *status, uint32_t *sts)
{
	if (idle_lines())
		return -1;
	hc_wr(INTSTS, 0xffffffff);
	hc_wr(ARG, r << 16);
	hc_wr(XFER_CMD, CMD_R1(13) << 16);
	if (wait_int(INT_CMD_DONE, 50, sts)) {
		hc_wr(INTSTS, 0xffffffff);
		reset_cmd_dat();
		return -1;
	}
	hc_wr(INTSTS, INT_CMD_DONE);
	*status = hc_rd(RESP0);
	return 0;
}

static int cmd13(uint32_t *status)
{
	uint32_t sts;
	if (idle_lines())
		return fail("lines busy", hc_rd(PRESENT));
	hc_wr(INTSTS, 0xffffffff);
	hc_wr(ARG, rca << 16);
	hc_wr(XFER_CMD, CMD_R1(13) << 16);
	if (wait_int(INT_CMD_DONE, 100, &sts))
		return fail("CMD13", sts);
	hc_wr(INTSTS, INT_CMD_DONE);
	*status = hc_rd(RESP0);
	return 0;
}

/*
 * End of a data transfer: Transfer Complete, or DAT inhibit clearing in the present state.
 * The first probe saw data arrive but no Transfer Complete, so report the first fallback once.
 */
static int xfer_fallbacks;

static int wait_xfer_done(void)
{
	uint64_t until = now() + rsr("cntfrq_el0") / 2;
	for (;;) {
		uint32_t s = hc_rd(INTSTS);
		if (s & INT_ERROR)
			return fail("xfer error", s);
		if (s & INT_XFER_DONE) {
			hc_wr(INTSTS, INT_XFER_DONE);
			return 0;
		}
		uint32_t pr = hc_rd(PRESENT);
		if (!(pr & 2)) {
			if (!xfer_fallbacks++) {
				kv("no xfer-done: sts ", s, 8);
				kv("pr ", pr, 8);
				kv("blk ", hc_rd(BLKSZ_CNT), 8);
				con_newline();
			}
			return 0;
		}
		if (now() > until) {
			kv("!! xfer stuck: sts ", s, 8);
			kv("pr ", pr, 8);
			kv("blk ", hc_rd(BLKSZ_CNT), 8);
			con_newline();
			hc_wr(INTSTS, 0xffffffff);
			reset_cmd_dat();
			return -1;
		}
	}
}

/* Read count blocks from lba. count == 1 uses CMD17, more uses CMD18 with auto CMD12. */
static int read_blocks(uint32_t lba, uint32_t count, uint8_t *buf)
{
	uint32_t sts;
	if (idle_lines())
		return fail("lines busy", hc_rd(PRESENT));
	hc_wr(INTSTS, 0xffffffff);
	hc_wr(BLKSZ_CNT, (count << 16) | 0x7000 | 512);
	hc_wr(ARG, lba);
	uint32_t mode = TM_READ | TM_BLKCNT_EN | (count > 1 ? TM_MULTI | TM_AUTO_CMD12 : 0);
	hc_wr(XFER_CMD, (CMD_R1_DATA(count > 1 ? 18 : 17) << 16) | mode);
	if (wait_int(INT_CMD_DONE, 100, &sts))
		return fail("read cmd", sts);
	hc_wr(INTSTS, INT_CMD_DONE);

	for (uint32_t b = 0; b < count; b++) {
		if (wait_int(INT_READ_READY, 500, &sts))
			return fail("read data", sts);
		hc_wr(INTSTS, INT_READ_READY);
		for (int i = 0; i < 128; i++) {
			uint32_t w = hc_rd(DATA);
			if (buf) {
				uint8_t *p = buf + i * 4;
				p[0] = w;
				p[1] = w >> 8;
				p[2] = w >> 16;
				p[3] = w >> 24;
			}
		}
		buf = 0;	/* caller buffers hold one block: keep the first */
	}
	/* Diagnostic: how many more data-port reads until the buffer-read-enable flag clears. */
	uint32_t extra = 0;
	while ((hc_rd(PRESENT) & (1U << 11)) && extra < 4096) {
		(void)hc_rd(DATA);
		extra++;
	}
	if (extra) {
		puts_("pio: ");
		dec(extra);
		kv(" extra words, pr ", hc_rd(PRESENT), 8);
		kv("sts ", hc_rd(INTSTS), 8);
		con_newline();
	}
	return wait_xfer_done();
}

/* ADMA2 64-bit descriptor for an SDHCI 3.0 host: 96 bits. */
struct adma_desc {
	uint16_t attr, len;
	uint32_t addr_lo, addr_hi;
} __attribute__((packed));

#define ADMA_VALID		(1U << 0)
#define ADMA_END		(1U << 1)
#define ADMA_TRAN		(2U << 4)

static struct adma_desc desc[4] __attribute__((aligned(16)));
static uint8_t dmabuf[32768] __attribute__((aligned(4096)));

/* Read up to 64 blocks into dmabuf by ADMA2 (CMD17, or CMD18 + auto CMD12). */
static int read_dma(uint32_t lba, uint32_t count)
{
	uint32_t sts;
	if (count == 0 || count > 64)
		return -1;
	if (idle_lines())
		return fail("lines busy", hc_rd(PRESENT));
	volatile struct adma_desc *d = desc;
	d->attr = ADMA_VALID | ADMA_END | ADMA_TRAN;
	d->len = (uint16_t)(count * 512);
	d->addr_lo = (uint32_t)(uintptr_t)dmabuf;
	d->addr_hi = (uint32_t)((uint64_t)(uintptr_t)dmabuf >> 32);
	dsb();
	hc_wr(INTSTS, 0xffffffff);
	hc_wr(ADMA_ADDR_LO, (uint32_t)(uintptr_t)desc);
	hc_wr(ADMA_ADDR_HI, (uint32_t)((uint64_t)(uintptr_t)desc >> 32));
	hc_wr(BLKSZ_CNT, (count << 16) | 0x7000 | 512);
	hc_wr(ARG, lba);
	uint32_t mode = TM_DMA | TM_READ | TM_BLKCNT_EN | (count > 1 ? TM_MULTI | TM_AUTO_CMD12 : 0);
	hc_wr(XFER_CMD, (CMD_R1_DATA(count > 1 ? 18 : 17) << 16) | mode);
	if (wait_int(INT_CMD_DONE, 100, &sts))
		return fail("dma cmd", sts);
	hc_wr(INTSTS, INT_CMD_DONE);
	if (wait_int(INT_XFER_DONE, 1000, &sts)) {
		kv("adma err ", hc_rd(ADMA_ERR), 8);
		kv("pr ", hc_rd(PRESENT), 8);
		con_newline();
		return fail("dma xfer", sts);
	}
	hc_wr(INTSTS, INT_XFER_DONE);
	dsb();
	return 0;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }

/* GPT entry name is UTF-16LE; compare its ASCII subset. */
static int name_is(const uint8_t *e, const char *want)
{
	const uint8_t *n = e + 56;
	int i = 0;
	for (; want[i]; i++)
		if (n[i * 2] != (uint8_t)want[i] || n[i * 2 + 1])
			return 0;
	return n[i * 2] == 0 && n[i * 2 + 1] == 0;
}

static void put_name(const uint8_t *e)
{
	con_putc(' ');
	for (int i = 0; i < 36 && e[56 + i * 2]; i++)
		con_putc_safe((char)e[56 + i * 2]);
}

static void gpt_scan(uint64_t entries_lba, uint32_t n, uint32_t esize)
{
	uint32_t per = 512 / esize, found = 0, used = 0;
	for (uint32_t i = 0; i < n && esize && per; i++) {
		if (i % per == 0 && read_dma((uint32_t)(entries_lba + i / per), 1))
			return;
		const uint8_t *e = dmabuf + (i % per) * esize;
		uint64_t first = le64(e + 32), last = le64(e + 40);
		if (!first)
			continue;
		used++;
		if (name_is(e, "userdata") || name_is(e, "boot") || name_is(e, "system")) {
			puts_("part ");
			dec(i + 1);
			put_name(e);
			kv(" lba ", first, 8);
			kv("- ", last, 8);
			puts_("(");
			dec((int64_t)((last - first + 1) >> 11));
			puts_(" MiB)");
			con_newline();
			found++;
		}
	}
	puts_("entries used ");
	dec(used);
	con_newline();
	(void)found;
}

void emmc_probe(void)
{
	puts_("[emmc]");
	con_newline();
	uint32_t apps = rd32(GCC_SDCC1_APPS_CBCR), ahb = rd32(GCC_SDCC1_AHB_CBCR);
	kv("cbcr apps ", apps, 8);
	kv("ahb ", ahb, 8);
	con_newline();
	if ((apps & CBCR_CLK_OFF) || (ahb & CBCR_CLK_OFF)) {
		puts_("!! SDCC1 clock off, not touching the controller");
		con_newline();
		return;
	}

	kv("ver ", hc_rd(VERSION) >> 16, 4);
	kv("caps ", hc_rd(CAPS0), 8);
	kv("", hc_rd(CAPS1), 8);
	con_newline();
	kv("present ", hc_rd(PRESENT), 8);
	kv("hostctl ", hc_rd(HOSTCTL), 8);
	con_newline();
	kv("clkctl ", hc_rd(CLKCTL), 8);
	kv("hostctl2 ", hc_rd(HOSTCTL2), 8);
	con_newline();
	kv("intsts ", hc_rd(INTSTS), 8);
	kv("inten ", hc_rd(INTEN), 8);
	kv("sigen ", hc_rd(SIGEN), 8);
	con_newline();
	uint32_t cqcfg = rd32(CQ + 0x08);
	kv("cqver ", rd32(CQ + 0x00), 8);
	kv("cqcfg ", cqcfg, 8);
	con_newline();
	if (cqcfg & 1) {
		puts_("!! command queue engine enabled by ABL, stopping");
		con_newline();
		return;
	}
	if (!(hc_rd(CLKCTL) & (1U << 2))) {
		puts_("!! SD clock not running, stopping");
		con_newline();
		return;
	}

	/* Status bits only latch when enabled. Signals stay as ABL left them (IRQs are masked anyway). */
	hc_wr(INTEN, hc_rd(INTEN) | 0xf3ff0023U);

	/* Find the RCA: only the card's own address answers CMD13. */
	uint32_t st, sts = 0;
	int found = 0;
	puts_("rca scan:");
	for (uint32_t r = 1; r <= 16; r++) {
		if (!cmd13_quiet(r, &st, &sts)) {
			puts_(" ");
			dec(r);
			puts_("=ok");
			if (!found)
				rca = r;
			found = 1;
		}
	}
	if (!found) {
		kv(" none, last sts ", sts, 8);
		con_newline();
		kv("present ", hc_rd(PRESENT), 8);
		con_newline();
		return;
	}
	con_newline();
	if (cmd13(&st))
		return;
	puts_("rca ");
	dec(rca);
	kv(" cmd13 ", st, 8);
	puts_("state ");
	dec((st >> 9) & 0xf);
	puts_((((st >> 9) & 0xf) == 4) ? " (tran)" : " (NOT tran)");
	con_newline();

	/* One polled read as a diagnostic: it may not complete on this host. */
	if (read_blocks(0, 1, sector)) {
		puts_("pio read failed, using ADMA2");
		con_newline();
	}
	kv("pio lba0 sig ", sector[510] | sector[511] << 8, 4);
	con_newline();

	if (read_dma(0, 1))
		return;
	for (int i = 0; i < 512; i++)
		sector[i] = dmabuf[i];
	puts_("dma lba0 ");
	for (int i = 0; i < 8; i++)
		hex(sector[0x1be + i], 2);
	con_newline();
	kv("lba0 sig ", sector[510] | sector[511] << 8, 4);
	kv("type ", sector[450], 2);
	con_newline();

	if (read_dma(1, 1))
		return;
	for (int i = 0; i < 512; i++)
		sector[i] = dmabuf[i];
	puts_("lba1 ");
	for (int i = 0; i < 8; i++)
		con_putc_safe((char)sector[i]);
	uint64_t entries = le64(sector + 72), last = le64(sector + 48);
	uint32_t n = le32(sector + 80), esize = le32(sector + 84);
	kv(" last ", last, 8);
	con_newline();
	kv("entries at ", entries, 4);
	puts_("n ");
	dec(n);
	puts_(" size ");
	dec(esize);
	con_newline();
	if (sector[0] == 'E' && sector[1] == 'F' && sector[2] == 'I')
		gpt_scan(entries, n, esize);

	/* Throughput: 64 blocks (32 KiB) by CMD18 + auto CMD12, ADMA2. */
	uint64_t t0 = now();
	int r = read_dma(0, 64);
	uint64_t us = (now() - t0) * 1000000 / rsr("cntfrq_el0");
	if (!r) {
		puts_("cmd18 x64 ok in ");
		dec((int64_t)us);
		puts_(" us = ");
		dec(us ? (int64_t)(32768ULL * 1000000 / us / 1024) : 0);
		puts_(" KiB/s");
		con_newline();
	}
	if (!cmd13(&st)) {
		puts_("after: state ");
		dec((st >> 9) & 0xf);
		con_newline();
	}
}
