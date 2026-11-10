#pragma once
#include <stdint.h>

#define SPLASH_BASE		0x9d400000UL	/* cont_splash_mem */
#define SPLASH_SIZE		0x02400000UL

#define PSCI_VERSION		0x84000000UL
#define PSCI_CPU_OFF		0x84000002UL
#define PSCI_CPU_ON		0xc4000003UL
#define PSCI_SYSTEM_RESET	0x84000009UL
#define PSCI_FEATURES		0x8400000aUL
#define PSCI_SYSTEM_RESET2	0xc4000012UL

#define rsr(reg) ({ uint64_t _v; __asm__ volatile("mrs %0, " reg : "=r"(_v)); _v; })
#define wsr(reg, v) __asm__ volatile("msr " reg ", %0" :: "r"((uint64_t)(v)) : "memory")

static inline void dsb(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void isb(void) { __asm__ volatile("isb" ::: "memory"); }
static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline uint64_t rd64(uintptr_t a) { return *(volatile uint64_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void wr8(uintptr_t a, uint8_t v) { *(volatile uint8_t *)a = v; }

/* main.c */
extern uintptr_t fb_base;
extern uint32_t fb_w, fb_h, fb_cpp, fb_stride;
uint64_t smc(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3);
uint64_t now(void);
void delay_ms(uint64_t ms);
void con_newline(void);
void con_putc(char c);
void con_putc_safe(char c);
void puts_(const char *s);
void hex(uint64_t v, int digits);
void dec(int64_t v);
void kv(const char *k, uint64_t v, int digits);
uint32_t be32(const volatile uint8_t *p);
int streq(const volatile uint8_t *a, const char *b);
const volatile uint8_t *align4(const volatile uint8_t *p);

/* osm.c */
uint64_t osm_speed_ticks(uint64_t n);
void osm_print_speed(const char *who, uint64_t ticks, uint64_t n);
void osm_probe(void);
void osm_probe2(void);
int osm_bringup(unsigned target);

/* mss.c */
void mss_probe(uintptr_t mba, uintptr_t mba_end);

/* emmc.c */
void emmc_probe(void);
int emmc_init(void);
int emmc_read(uint32_t lba, uint32_t count, uintptr_t dst);
int emmc_part(const char *name, uint32_t *first, uint32_t *blocks);

/* fat.c */
int fat_mount(const char *part);
int fat_lookup(const char *dir, const char *name, uint32_t *clus, uint32_t *size);
int fat_read(uint32_t clus, uint32_t size, uintptr_t dst);

/* xnu.c */
int payload_find(uintptr_t dtb, uintptr_t *start, uintptr_t *end);
int ramdisk_find(uintptr_t dtb, uintptr_t *start, uintptr_t *end);
void gic_prepare(void);
const char *xnu_boot(uintptr_t start, uintptr_t end);
