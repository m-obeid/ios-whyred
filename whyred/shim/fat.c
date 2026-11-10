/*
 * Read-only FAT12/16/32 for the MSS probe: files in the stock "modem" partition (NON-HLOS,
 * vfat, files under image/) are read straight into their destination by ADMA2 (emmc.c).
 * 8.3 names only (long-name entries are skipped); every name we need fits 8.3.
 */
#include "shim.h"

static uint32_t part_lba, spc, fat_lba, root_lba, root_sectors, data_lba, root_clus, fat_type;
static uint32_t bounce[128];                    /* one sector of FAT, or a directory sector */
static uintptr_t cluster_buf_pa = 0xa1100000;    /* free RAM below the XNU base: partial clusters */

static uint32_t u16le(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t u32le(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int rd_sector(uint32_t lba)
{
	return emmc_read(lba, 1, (uintptr_t)bounce);
}

int fat_mount(const char *part)
{
	uint32_t blocks;
	if (emmc_init() || emmc_part(part, &part_lba, &blocks)) {
		puts_("!! fat: partition not found");
		con_newline();
		return -1;
	}
	if (rd_sector(part_lba))
		return -1;
	const uint8_t *b = (const uint8_t *)bounce;
	if (u16le(b + 11) != 512 || b[510] != 0x55 || b[511] != 0xaa) {
		puts_("!! fat: not a 512-byte FAT volume");
		con_newline();
		return -1;
	}
	spc = b[13];
	uint32_t reserved = u16le(b + 14), nfats = b[16], root_ents = u16le(b + 17);
	uint32_t tot = u16le(b + 19) ? u16le(b + 19) : u32le(b + 32);
	uint32_t fatsz = u16le(b + 22) ? u16le(b + 22) : u32le(b + 36);
	fat_lba = part_lba + reserved;
	root_lba = fat_lba + nfats * fatsz;
	root_sectors = (root_ents * 32 + 511) / 512;
	data_lba = root_lba + root_sectors;
	uint32_t clusters = (tot - (data_lba - part_lba)) / spc;
	fat_type = clusters < 4085 ? 12 : clusters < 65525 ? 16 : 32;
	root_clus = fat_type == 32 ? u32le(b + 44) : 0;
	puts_("fat");
	dec(fat_type);
	kv(" lba ", part_lba, 8);
	kv("spc ", spc, 2);
	con_newline();
	return 0;
}

static uint32_t fat_next(uint32_t c)
{
	if (fat_type == 16) {
		if (rd_sector(fat_lba + c / 256))
			return 0x0fffffff;
		uint32_t v = ((const uint16_t *)bounce)[c % 256];
		return v >= 0xfff8 ? 0x0fffffff : v;
	}
	if (fat_type == 32) {
		if (rd_sector(fat_lba + c / 128))
			return 0x0fffffff;
		uint32_t v = bounce[c % 128] & 0x0fffffff;
		return v >= 0x0ffffff8 ? 0x0fffffff : v;
	}
	uint32_t off = c + c / 2;                       /* FAT12: entries straddle sectors */
	uint8_t lo, hi;
	if (rd_sector(fat_lba + off / 512))
		return 0x0fffffff;
	lo = ((const uint8_t *)bounce)[off % 512];
	if ((off + 1) % 512 == 0) {
		if (rd_sector(fat_lba + off / 512 + 1))
			return 0x0fffffff;
		hi = ((const uint8_t *)bounce)[0];
	} else {
		hi = ((const uint8_t *)bounce)[(off + 1) % 512];
	}
	uint32_t v = (c & 1) ? (uint32_t)(lo >> 4 | hi << 4) : (uint32_t)(lo | (hi & 0xf) << 8);
	return v >= 0xff8 ? 0x0fffffff : v;
}

static uint32_t clus_lba(uint32_t c) { return data_lba + (c - 2) * spc; }

/* "modem.b00" -> "MODEM   B00" */
static void to83(const char *n, char out[11])
{
	int i = 0, o = 0;
	for (; o < 11; o++)
		out[o] = ' ';
	for (o = 0; n[i] && n[i] != '.' && o < 8; i++, o++)
		out[o] = (char)(n[i] >= 'a' && n[i] <= 'z' ? n[i] - 32 : n[i]);
	while (n[i] && n[i] != '.')
		i++;
	if (n[i] == '.')
		i++;
	for (o = 8; n[i] && o < 11; i++, o++)
		out[o] = (char)(n[i] >= 'a' && n[i] <= 'z' ? n[i] - 32 : n[i]);
}

/* Search one directory (cluster chain, or the FAT12/16 root when dir_clus is 0). */
static int dir_find(uint32_t dir_clus, const char *name, uint32_t *clus, uint32_t *size, int *is_dir)
{
	char want[11];
	to83(name, want);
	uint32_t c = dir_clus, sec = 0, nsec = dir_clus ? spc : root_sectors;
	for (;;) {
		for (sec = 0; sec < nsec; sec++) {
			uint32_t lba = dir_clus ? clus_lba(c) + sec : root_lba + sec;
			if (rd_sector(lba))
				return -1;
			const uint8_t *e = (const uint8_t *)bounce;
			for (int k = 0; k < 16; k++, e += 32) {
				if (e[0] == 0)
					return -1;              /* end of directory */
				if (e[0] == 0xe5 || e[11] == 0x0f || (e[11] & 0x08))
					continue;               /* deleted, long name, volume label */
				int same = 1;
				for (int j = 0; j < 11; j++)
					if ((char)e[j] != want[j])
						same = 0;
				if (same) {
					*clus = u16le(e + 26) | (fat_type == 32 ? u16le(e + 20) << 16 : 0);
					*size = u32le(e + 28);
					*is_dir = (e[11] & 0x10) != 0;
					return 0;
				}
			}
		}
		if (!dir_clus)
			return -1;
		c = fat_next(c);
		if (c >= 0x0ffffff8 || c < 2)
			return -1;
	}
}

/* Find "dir/name" (one level of directory) and return its first cluster and size. */
int fat_lookup(const char *dir, const char *name, uint32_t *clus, uint32_t *size)
{
	uint32_t dclus, dsize;
	int is_dir;
	if (dir_find(root_clus, dir, &dclus, &dsize, &is_dir) || !is_dir)
		return -1;
	return dir_find(dclus, name, clus, size, &is_dir);
}

/* Read a whole file to dst (exactly size bytes; the tail of the last cluster goes via a bounce). */
int fat_read(uint32_t clus, uint32_t size, uintptr_t dst)
{
	uint32_t csize = spc * 512, done = 0;
	while (done < size) {
		if (clus < 2 || clus >= 0x0ffffff8)
			return -1;
		uint32_t left = size - done;
		if (left >= csize) {
			/* merge a run of consecutive clusters into one read */
			uint32_t run = 1, c = clus;
			while ((run + 1) * csize <= left && run < 64) {
				uint32_t n = fat_next(c);
				if (n != c + 1)
					break;
				c = n;
				run++;
			}
			if (emmc_read(clus_lba(clus), run * spc, dst + done))
				return -1;
			done += run * csize;
			clus = fat_next(c);
		} else {
			if (emmc_read(clus_lba(clus), spc, cluster_buf_pa))
				return -1;
			for (uint32_t i = 0; i < left; i++)
				*(volatile uint8_t *)(dst + done + i) = *(volatile uint8_t *)(cluster_buf_pa + i);
			done = size;
		}
	}
	return 0;
}
