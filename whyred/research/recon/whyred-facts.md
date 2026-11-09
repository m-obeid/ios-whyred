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

## iOS 16 userland from the eMMC (2026-11-09)

- Root: whyred/tools/build-iosroot.sh (iPhone X 16.7.16 20H392, 16 GiB HFSX, sha256 639fc7ab...),
  copied by whyred/tools/push-root.sh into /userdata/iosroot.img (fallocate, 11 extents, first sector
  0x2aa8000 inside userdata). Raw eMMC sectors verified against the image.
  userdata is ext4 data=journal: check raw sectors after copying, not the copy's exit status.
- Kernel: CI run 42684503166 (0961 with root-extents and writes). ADT: WHYRED_EXTENTS=whyred/out/root-extents.json.
- Boot-args: debug=0xc serial=3 cpus=4 keepsyms=1 -noprogress -enable_kprintf_spam rd=emmcroot0
  emmcroot=userdata emmcrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1
  wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1 sweetcpufreq=0 sweetbattery=0 panicreset=60
- Result: launchd and the system daemons run (securityd, cryptexd, symptomsd, dataaccessd, biomed,
  backupd, webbookmarksd, ...). SpringBoard starts and crashes with SIGTRAP:
  "failed to find the main CADisplay". Expected: disp0 is not in the ADT, so AppleCLCD /
  IOMobileFramebuffer never start (they write topaz MDP/DSI registers at 0x5e0xxxx).
- Next: port SweetFramebuffer/AppleCLCD to the whyred MDP (VIG0 0xc905000, CTL0 0xc902000) or
  to a CPU copy into the splash buffer at 0x9d400000, then give the ADT a disp0 again.

## iOS lock screen on whyred (2026-11-09)

- Kernel: CI run 42692786657 (patch 0962: AppleCLCD/SweetFramebuffer on the MDP5, VIG0 0xc905000,
  CTL0 0xc902000, INTF 0xc96b000, DSI only with sweetdsi=1). ADT has disp0 again (1125x2436 canvas).
- Same boot-args as the userland boot. SpringBoard shows the iOS 16 lock screen 60-150 s after boot:
  date "Wednesday, December 31" (no RTC), live clock, camera shortcut. Clock drawn red over a black
  wallpaper. CPU rendering (no GPU), console text still on.

## USB networking from XNU (2026-11-09)

- Kernel: CI run 42699162243 (patch 0963: whyred USB constants, real dcache ops for eMMC and USB DMA).
- sweetusb=1 report (cache test root, no disp0): GDSC usb30_prim 0xa0222000 on, all 7 clocks on
  (master 0x2000c221 ...), DWC3 GSNPSID 0x5533300a (3.00a), GCTL 0x..2000 device mode, DALEPENA 0xf,
  DCTL run/stop clear, ABL event buffer at 0x9ad74000. QUSB2 PLL locked (0x38 bit 5), TUNE1 0xf8,
  TUNE2 0xb3, POWERDOWN 0x22, TEST1 0x04 (8-bit registers read byte-replicated).
- sweetusb=2: the laptop enumerates 1209:0001 (CDC-ECM) 38 s after fastboot boot, MAC
  02:00:5e:77:ee:01. NetworkManager profile "whyred-usb" (static 192.168.7.1/24, matched by MAC)
  configures it without sudo. ping 192.168.7.2: 5/5, 1-2 ms.
- Live kernel log: udp/5140 on 192.168.7.1, read with whyred/tools/udplog.py (no netcat installed).
- whyred/tools/whyred-init.S (cache test root) reboots after 60 s; Linux is back ~100 s after boot.
- Display boots: the console moves into AppleCLCD's double buffer, so lines after that only show on
  frame flips. CTL0 layer0 0x1000005 top 0x20: ABL scans VIG0 out through CTL0 to INTF1.

## SSH into iOS (2026-11-10)

- Root image: ios16-root.img + whyred/tools/add-ssh.sh (palera1n rootful Procursus bootstrap 1800, dropbear
  2020.81 + libtomcrypt1/libtommath1, root key = laptop id_ed25519.pub), pushed (sha256 a3538b0b...).
  Snapshot of the previous phone copy: whyred/ipsw/iosroot-snapshot-20261109.img (sha256 85c55e19...).
- launchd only starts compiled-list jobs keyed under /System/Library: the dropbear job is
  /System/Library/LaunchDaemons/com.whyred.dropbear.plist (first try under /Library/LaunchDaemons never ran).
- Boot image: whyred/out/xnu-ios-ssh.img = kernel 42699162243 + root-extents + disp0 + boot-args of the
  userland boot + sweetusb=2.
- Result: USB net up 39 s after fastboot boot, `ssh root@192.168.7.2` works (try 10, ~70 s):
  Darwin 22.6.0 xnu-8796.141.3 iPhone10,3, iOS 16.7.16 (20H392), uid 0. 174 processes incl. SpringBoard,
  backboardd; / is /dev/emmcroot0 (16 GiB, 12 GiB used). First boot of a fresh root sits on the Apple
  logo for a long time (first-boot migration, load average ~60).

- First boot of a fresh root: lock screen after ~17-18 min (uptime). ~15 min of it is lsd building the
  LaunchServices database while SpringBoard waits; then load average spikes to ~180. Wallpaper (blue
  gradient) renders, clock drawn red: red/blue channel order still to verify.
- Speed: a 200k-iteration dash loop takes 8.1 s on iOS (5.5 user, 2.5 sys) vs 0.58 s on the laptop.
  Suspects: Gold cores at a low clock (no cpufreq under XNU) and kernel emulation of ARMv8.1 LSE/LDAPR
  used by the iPhone X userland (patch 0081). Compare the same loop under Linux on whyred.
- iOS has no scp/sftp-server: read files with `ssh ... cat`.

## Not yet known

- Whether ABL accepts an uncompressed Image in a v0 boot image or needs gzip.
