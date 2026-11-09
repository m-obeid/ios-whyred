# whyred hardware facts (recon 2026-11-09)

Source: live phone (Void Linux on Halium, kernel 4.19.325 whyred), read over SSH as root.
Raw data in this folder: live.dtb/live.dts (/sys/firmware/fdt), iomem.txt, dmesg.txt, boot.img.

## Boot chain

- Bootloader: Qualcomm ABL (partition `abl`), same family as topaz.
- Boot image: header v0, page size 4096, kernel = gzip Image with 27 DTBs appended.
- ABL picks the first DTB: model "SDM 636 PM660 + PM660L E7S", qcom,msm-id <0x159 0>, qcom,board-id <0x30008 0 0x10008 0>.
- arm64 Image header: text_offset 0x80000, flags 0xa.
- Linux kernel code loads at 0x80080000.
- topaz needs boot image v4. whyred needs v0 with an appended DTB.

## CPU

| Cores | MPIDR | MIDR | Class |
|---|---|---|---|
| cpu0-3 | 0x000-0x003 | 0x51AF8014 | Kryo 260 Silver (A53), boot CPU |
| cpu4-7 | 0x100-0x103 | 0x51AF8002 | Kryo 260 Gold (A73) |

- Silver MIDR matches topaz exactly. Gold uses the same MPIDR layout as topaz.
- Features: fp asimd evtstrm aes pmull sha1 sha2 crc32 cpuid (ARMv8.0, no LSE atomics).
- TGran16 on Gold: not yet confirmed. Linux reports a sanitized value, so the shim must read ID_AA64MMFR0 on a Gold core.
- PSCI 1.0, method smc.

## Interrupts and timer

- GICv3: GICD 0x17a00000 (0x10000), GICR 0x17b00000 (0x100000), stride 0x20000.
- topaz: GICD 0xf200000, GICR 0xf300000.
- Arch timer: 19.2 MHz (0x124f800), PPIs 1/2/3/0 (INTID 17/18/19/16).

## Memory (4 GB model)

- DT memory: 0x80000000 + 2 GiB, 0x1_00000000 + 0x7e4c0000.
- Every fixed carve-out sits below 0xa0400000 (modem, adsp, wlan_msa, smem, splash, ramoops).
- Free contiguous RAM: 0xa0400000 to 0x17e4bffff, about 3.47 GiB. The two banks are physically adjacent.
- Linux System RAM per iomem: 80000000-855fffff, 88f00000-8abfffff, 94b00000-9fffffff, a0400000-17e4bffff.

## Display

- Panel: Tianma TD4310, DSI video mode, 1080x2160, 60 Hz, 24 bpp.
- Splash carve-out (cont_splash_mem): 0x9d400000, size 0x2400000 (36 MiB).
- Video mode means the MDP scans the buffer continuously. Writes to the splash buffer appear on screen.
- Pixel format and stride under ABL: unknown. Read the MDP SSPP registers from the shim, or test candidates (RGB888 stride 3240, xRGB8888 stride 4320).
- Linux fb0 (after takeover): 32 bpp, stride 4352, virtual 1080x4320.

## Storage

- eMMC, sdhci-msm v5 at 0xc0c4000 (hc), 0xc0c5000 (cqhci), 0xc0c8000 (ICE). IRQs SPI 0x6e, 0x70. 8-bit bus, HS400.
- 64 GB: userdata is mmcblk0p64, 51 GB, currently holds the Halium/Void rootfs (20 GB used).

## Reset

- PS_HOLD: 0x10ac000. TCSR boot misc detect: 0x1fd3000.
- PSCI SYSTEM_RESET works too (topaz shim uses it).

## Shim m1 results (2026-11-09, photo)

- fastboot: unlocked, kernel:uefi (ABL), max-download-size 512 MiB.
- fastboot uploads fail on this laptop's xHCI ports until LPM is off:
  `echo "18d1:d00d:k" | sudo tee /sys/module/usbcore/parameters/quirks`, then replug.
- v0 boot image with gzip Image + whyred DTB boots. The shim runs at EL1, SCTLR 0x00c50838, CNTFRQ 0x124f800.
- Boot core: MPIDR 0x000 (Silver), MIDR 0x51af8014, mmfr0 0x1122, 16K=NO.
- Silver 0x001-0x003: same as the boot core, 16K=NO.
- Gold 0x100: MIDR 0x51af8002, mmfr0 0x101122, 16K=YES (PARange 40-bit, 16-bit ASID).
- Gold 0x101-0x103: CPU_ON returned 0 but no report in 200 ms. This happened after 0x100 ran CPU_OFF, which powered down the Gold cluster.
- Shim v2 (all cores woken back to back, parked in wfe, no CPU_OFF): all 7 report.
  Gold 0x100-0x103 all 16K=YES, mmfr0 0x101122, up after 5.5-8.9 ms. Silver 0x001-0x003 up after 1.7-4.2 ms.
- Do not CPU_OFF the last running Gold core.
- PSCI SYSTEM_RESET with IMEM restart reason 0x77665500 boots Linux, not fastboot. Linux uses a PMIC warm reset
  (DT qcom,force-warm-reboot), so the IMEM flag needs the PON PS_HOLD reset type set to warm over SPMI first.
  Workaround: hold vol- during the reset.
- PSCI version 0x00010000. FEATURES: CPU_ON 0, SYSTEM_RESET 0, SYSTEM_RESET2 -1 (not supported).
- MDP: MDSS hw 0x30020000. Live pipe vig0: addr 0x9d400000, size 1080x2160, stride 4320,
  format 0x000237ff (4 bytes per pixel), unpack 0x03020001.
- Pixel bytes in memory: B, G, R, x. That is xRGB8888 as a little-endian word, the same as topaz.
- ABL moves the DTB (seen at 0x83200000, 385696 bytes) and keeps both memory banks.

## First XNU boot (2026-11-09)

- Kernel: CI run 42669291638 (macos-15-intel, Xcode 16), topaz patches + 0960 (early console at 0x9d400000).
  No LSE/ldapr in the kernel. arm64 runners were out of capacity, Intel built in 20 min.
- Image: whyred/mkboot.sh = mkadt_whyred.py ADT (RAM 0xa2000000 + 0xdc000000, no disp0/sweet-ufs)
  + tools/sweet-init RAM disk (mockfs) + shim m2. 17 MB, fastboot boot works.
- Boot-args: debug=0xc serial=3 cpus=1 keepsyms=1 -noprogress -enable_kprintf_spam rd=md0 sweetcpufreq=0 sweetbattery=0
- Result: shim hands off Silver -> Gold 0x100, XNU early console + kernel log on screen,
  init runs as PID 1 and prints "SWEET init: still alive" every 5 s.
- cpus=4 (Gold 0x100-0x103) also reaches PID 1 with the heartbeat. The m2 shim's GIC setup for
  0x101-0x103 is enough. Image: whyred/out/xnu-boot-cpus4.img.

## eMMC from bare metal (shim probe, 2026-11-09)

- GCC SDCC1 clocks on after ABL: APPS CBCR (0x116004) 0x00004221, AHB CBCR (0x116008) 0x20008001.
- SDHCI at 0xc0c4000: host version 0x6b02 (spec 3.00), caps 0x742dc8b2 / 0x0000808f (ADMA2, 64-bit).
  ABL leaves: host ctl 0x0b38 (ADMA2 64-bit, 8-bit bus, 1.8 V power on), clock ctl 0x000f0007
  (SD clock on), host ctl2 0x0003 (HS200), int enable 0x07ff003f, signal enable 0, CQE off (cqcfg 0, cqver 0x510).
- Card RCA is 2 (Qualcomm LK convention), not 1. CMD13 -> 0x00000900, state 4 (tran).
- PIO is broken on this host: after a block the buffer-read-enable flag never clears (4096 extra
  data-port reads), and Transfer Complete never comes. Use ADMA2 only.
- ADMA2 (64-bit, 96-bit descriptors, desc address at 0x58/0x5c, transfer mode bit 0) works:
  CMD17 and CMD18 + auto CMD12. 64 blocks in 1.09 ms (~29 MB/s with overhead).
- GPT: last LBA 0x747bfde, 64 entries x 128 bytes at LBA 2.
  boot = part 58, LBA 0x160000, 64 MiB. system = part 61, LBA 0x220000.
  userdata = part 64, LBA 0xdc0000 to 0x747bfde, 52599 MiB.

## XNU root on the eMMC (2026-11-09)

- Kernel: CI run 42679394416 (macos-15-intel, 12.5 min with ccache), patch 0961 (bsd/dev/sweet_emmc.c).
- Test root: whyred/tools/mktestroot.sh (32 MiB HFSX, /sbin/launchd = sweet-init), flashed with
  `fastboot flash cache`. The cache backup is whyred/backup/cache-mmcblk0p60.img.gz (sha256 177a543e...).
  Linux boots fine with it: Halium's f2fs/ext4 mounts of mmcblk0p60 fail and /cache stays unmounted.
- Boot-args: debug=0xc serial=3 cpus=1 keepsyms=1 -noprogress -enable_kprintf_spam rd=emmcroot0
  emmcroot=cache panicreset=60 sweetcpufreq=0 sweetbattery=0
- Result: `SWEET eMMC: "cache": RCA 2, first sector 0x1a0000, 256 MiB, read-only, DMA at 0xa5fd4000`,
  `BSD root: emmcroot0`, `hfs: mounted WhyredTest on device b(1, 0)`, root volume unsealed (error 25,
  expected), init runs as PID 1 from the eMMC.

## Not yet known

- Whether ABL accepts an uncompressed Image in a v0 boot image or needs gzip.
