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

## Hardware buttons (2026-11-10)

- Kernel: CI run 42723857575 (patch 0964). SPMI arbiter VERSION 0x30000000 (v3). APIDs: PON (PPID 0x008) 4,
  PM660L GPIO7 (PPID 0x2c6) 167. GPIO7 subtype 0x11, mode 0 (input), EN 0x80.
- Idle: PON RT_STS 0x0810 = 0, GPIO7 RT_STS 0xc610 bit 0 = 1. Power = PON bit 0, volume down = PON bit 1,
  volume up = GPIO7 bit 0 low. All three seen pressed and released (sweetkeys=2 log), events go to SweetHID.
- Keep presses under ~4 s: PON S1+S2 timers reset the phone (power ~6.5 s, power + vol- ~5.5 s).

## Touch (2026-11-10)

- Kernel: CI run 42725187297 (patch 0965: bsd/dev/sweet_syna.c, SweetHID panel size from the boot video).
- ABL leaves the BLSP1 AHB clock on (cbcr 0x2000c220) and QUP1 I2C off (0x80000000); the driver enables
  QUP1 I2C from XO (rcg cfg 0x1, cbcr 0x1) and puts GPIO2/3 (cfg was 0) on function 3.
  GPIO66 cfg 0x3c3 io 0x3, GPIO67 cfg 0x1. QUP hw 0x20070000, io_mode 0xa5 (FIFOs 64/64).
- RMI4 at 0x20: PDT F34 (query 0x27), F01 (query 0x2f cmd 0x26 ctrl 0xd data 0x4), F12 (query 0x4c ctrl 0x13
  data 0x6). Manufacturer 1, product "E753". Comes up unconfigured (status 0x81); driver sets configured.
- F12 objects at data 0x06 (7 per read): raw coordinates are panel pixels, 0..1080 x 0..2160, origin top
  left, no flips (corners (48,0) (1055,70) (1026,2083) (80,2123)). sweetsyna=2 reports to SweetHID.
- Result: unlock by swipe and home screen interaction work. Power locks; the backlight stays on because
  whyred's backlight is PM660L WLED (DSI DCS disabled): needs an SPMI-write WLED driver.

## Speed baseline (2026-11-10)

- Linux on whyred: Gold cpufreq 1113600/1401600/1747200/1804800 kHz (schedutil), Silver max 1612800.
  Pinned to cpu4 at 1.8048 GHz: `dd if=/dev/zero bs=1M count=200 | md5sum` 0.99 s real / 0.68 s user;
  dash 100k-iteration loop 0.44 s user, 0 sys.
- iOS (kernel 42729844526, sweetfb_noconsole=1 sweetfb_hz=30, 2 min after boot): md5sum 2.37 s real /
  1.91 s user / 0.76 sys; dash loop 3.97 s real / 2.69 user / 1.26 sys.
- So: user CPU time 2.8x slower on md5 (no atomics, no syscalls) -> Gold cores probably at a low OPP
  (~650 MHz); XNU has no CPU clock driver and SDM660 uses OSM, not cpufreq-hw (topaz's shim fix does not
  apply). The extra sys time on the dash loop is the ARMv8.1 LSE/LDAPR emulation (patch 0081).
- USB unplug/replug: patch 0966 (CI run 42729844526) ends active transfers and re-arms ep0 SETUP.

## Gold CPU clock: bootloader state vs Linux (2026-11-10)

- Shim OSM probe (whyred/shim OSM=1, read-only): after ABL the Gold mux (0x17811054) is 0x401 = GPLL0
  (600 MHz), Gold PLL L 0x20 (614 MHz, unused), CPRH ctl/status 0, OSM disabled, OSM table empty.
  Speed bin fuse 3. => XNU runs Gold at 600 MHz (matches the 2.8x slowdown).
- Linux reference (whyred/tools/osmdump, a read-only kernel module built against the phone's exact
  kernel in the whyred-kernel-build Docker image; dump in recon/osmdump-linux.txt):
  Gold PLL L 0x5e (1804.8 MHz), mux cfg 0x500 (PLL early), OSM enable 1, desired index 4, cycle ctl 0x113,
  CPRH ctl (0x179c7aa0) 0x080a00c9 (bit 0 OSM interface), status 0xcc504, SAW 0x40314.
  Gold LUT rows (freq_data / volt): 0 0x0004000f/0x2bc (288 MHz, 700 mV), 1 0x0404003a/0x102bc (1113.6, 700),
  2 0x04040049/0x202f4 (1401.6, 756), 3 0x0404005b/0x3033c (1747.2, 828), 4..39 0x0404005e/0x40348 (1804.8, 840).
  CPRH_REG_CORNER(0..7): 0e00414b 0e00414b 0e1d4f59 0e3e616b 0e44616e 0e697282 0e837d87 20007676.
- Normal-world reads that abort (synchronous external abort): OSM cluster block +0x000..0xfff (TZ sequencer)
  and unimplemented gaps, and CPRH +0x3000. Read only offsets the Linux drivers define.
- Next: port the Linux init (cpr3-regulator.c/cprh-kbss-regulator.c, clk-cpu-osm-660.c, both in
  ~/Project/whyred-linux/src/kernel-lineage-21) into the shim, staged, checking against this dump.

## Gold at 1804.8 MHz under XNU (2026-11-10)

- whyred/shim/osm.c osm_bringup(row), run on Gold before XNU (GOLD_ROW, default 4): hmss_gpll0 to 300 MHz,
  OSM clock 200 MHz, CPR clock; CPRH open loop (Linux values, 0x7f8 = 0x4080, corners from the dump,
  ctl 0x080a00c9, loop enable 0x1c1f); SAW AVS 0x01010031 / 0x04580458; OSM normal-world registers and all
  40 rows; 9 SCM IO writes (SMC64 0xC2000502, x1 = 2) into the TZ sequencer (MEM-ACC, APM); enable at row 0,
  then rows 1..n.
- Stage A (read-only, ABL state): SCM available (0xC2000601 -> x1 = 1), SCM IO reads of the sequencer work,
  firmware defaults 7 0 b c 0 19 0 0 5b9; Gold PLL cfg_lo 0x200d4828; OSM rcg 0x105; hmss_gpll0 cfg 0x100.
- Result: CPRH corner/mV/SAW per row 0..4: 0/700/2bc, 1/700/2bc, 2/756/2f4, 3/828/33c, 4/840/348.
  Shim loop ratio 3.0 (1804.8/600). Under iOS: md5sum 200 MB 0.96 s real / 0.66 user (Linux 0.99 / 0.68),
  dash loop 1.01 user + 0.45 sys (was 2.69 + 1.26). SpringBoard + SSH 47 s after fastboot boot (was 68-76).
- Open loop runs each corner at its fused ceiling (840 mV at 1804.8, Linux closed loop: 788).
  No LMH/thermal limits firmware is started: nothing throttles on heat under XNU.
- Remaining speed gap: ARMv8.1 LSE/LDAPR emulation (patch 0081), visible as sys time on atomics-heavy code.

## Thermal governor, LSE stats, CPRH closed loop (2026-11-10)

- Patch 0967 bsd/dev/sweet_thermal.c, started from sweet_emmc_keys_thread after touch. Polls Gold TSENS
  sensors 3-6 (tm 0x10ae000, status 0xa0 + 4n, valid bit 21, bits 11:0 signed deci-C) every 500 ms.
  Steps OSM DESIRED (0x179c3f10) one row down at >= 90 C, to row 0 at >= 105 C (Linux step trip),
  one row up per 2 s at <= 75 C, never above the shim's boot row. sweettherm=0 off, 1 on, 2 logs.
- Patch 0968: sleh.c counts emulated LSE ops per type and the hottest user PCs (64-slot table, shared
  cache PCs unslid, others with bit 63 set). sweettherm=2 prints `WHYRED LSE: N/s: ...` and
  `WHYRED LSE top:` every 10 s. Goal: find which code traps most before choosing an optimization.
- Shim CPRH_CL=1: 0x85c = 0x1de (RO 7 target quotient, Linux dump), 0x7f8 = 0x4090 (closed loop), then
  five gold_state lines 200 ms apart. Test image whyred/out/osm-cl-probe.img (OSM=1 OSM_TARGET=4 CPRH_CL=1).
  Expected: voltage drops from 840 mV toward Linux's ~788 mV at row 4. Corner words already carry
  RO select 7 and per-corner quotient deltas.
- CI: run 42785894823 (0967 only) cancelled, run 42786485693 builds 0967 + 0968.
- Run 42786485693 passed, both patches applied. Kernel sha 9b2c2b17...d29e. Boot images (fast2 args +
  sweettherm=2): whyred/out/xnu-ios-therm.img (open loop) and xnu-ios-therm-cl.img (shim CPRH_CL=1).
- Boot of xnu-ios-therm.img (10:14 local): `tsens trdy 0x8, OSM enable 1, boot row 4`. Gold 42 C idle,
  59 C peak during boot. 4 busy loops (cpus=4, all Gold) for 3 min: 48 -> 70 C in 20 s, plateau 71.2 C,
  53 C 10 s after the load. Never reached 90 C, no steps taken.
- LSE: 0 emulated ops per second for the whole boot, SpringBoard, SSH and the load test. iOS 16's
  arm64 userland does not run LSE/LDAPR on this CPU (it checks the CPU capabilities). Test binary
  (1M LDADD, scratch lse.S via tools/mkinit.py): counter shows exactly 1000000 at 0x100000418,
  0.55 s (0.24 user + 0.30 sys), so ~0.55 us per emulated op. 1M LDAXR/STLXR loops: 0.01 s.
  The earlier claim that dash-loop sys time is LSE emulation was wrong. Its cause is still unknown
  (fork/exec or page faults are candidates).
- Patch 0969: the 10 s log line adds the CPRH corner and mV (0x179c7aa4), and boot-args
  sweettherm_hot / sweettherm_cool (deci-C) for testing steps. CI 42789978412.
- Kernel run 42789978412 (sha 02cc26a2...af92). Open loop, sweettherm_hot=650 cool=550, 4 busy loops:
  65.0 C -> row 3 (828 mV), 65.7 C -> row 2 (756 mV), plateau 61.8 C at row 2, after the load
  51.4 C -> row 3, 52.4 C -> row 4 (840 mV). Steps and recovery work.
- CPRH closed loop (shim 0x85c = 0x1de, 0x7f8 = 0x4090): 792 mV idle, 796 mV under full load at row 4
  (open loop 840, Linux 788). 140 s of 3 busy loops + repeated md5 of a 50 MB random file: 522 runs,
  0 mismatches. Peak 70.5 C (open loop 71.2). Closed loop is now the shim default, CPRH_OL=1 for open.
- Default image whyred/out/xnu-ios-fast3.img: kernel 42789978412, closed loop, row 4, sweettherm=1
  default thresholds 90 / 75 C, otherwise the fast2 boot-args.

## Backlight: PM660L WLED (2026-11-10)

- Live DT qcom,leds@d800 (pm660l-spmi-wled): USID 3, ctrl 0xd800, sink 0xd900, string-cfg 0x3.
  Linux qcom-spmi-wled.c WLED4: ctrl 0x46 bit 7 module enable; sink 0x57/0x58 + 0x10n brightness
  LSB / MSB[3:0]; sink 0x47 [3:0] sync clear then set.
- SPMI v3 writes: chnls 0x08400000 + 0x8000 * apid, CMD +0 = (0 << 27) | (addr << 4) | (n - 1),
  WDATA0 +0x10, STATUS +0x08 (bit 0 done, 3:1 errors). Reads via observer 0x09400000 (as keys).
  WLED ctrl PPID 0x3d8 = APID 52, sink 0x3d9 = APID 6, both owner 0 (apps).
- ABL state: module 0x80 (on), sinks 0x30 (strings 0+1), brightness 2047 / 4095.
- Patch 0970 bsd/dev/sweet_wled.c (allowlisted writes, sweetwled=0 off) + SweetFramebuffer hooks:
  display off/on switch the WLED module, IOMFBBrightnessLevel maps DBV 1..0x7ff to 3..4095.
  Patch 0971: WLED off ~600 ms after the off request so the fade to black shows first; start level
  = ABL level / 2 (0x3ff -> 2047). Verified: lock fades then goes fully dark, unlock restores 2047.
- Brightness slider: dead. sweetbl=1 (AppleARMBacklight published, backboardd matches it) made the
  UI much slower and the slider still sat at 50 %; CoreBrightness never called setProperties on
  either service. Needs CoreBrightness RE. Default stays sweetbl=0.
- Default image whyred/out/xnu-ios-fast4.img: kernel run 42800881885 (sha 37938d05...3116),
  fast3 boot-args, shim closed loop row 4.

## Status bar: why it is the pink "rdar" placeholder (2026-11-10, parked)

- UIKit +[_UIStatusBarVisualProvider_Phone visualProviderSubclassForScreen:...] (0x189ab546c):
  UIScreen._exclusionArea nil -> LegacyPhone (iPhone 8 bar); rect y > 0 -> DynamicSplit; else Split.
  Split/DynamicSplit then pick a size class (Split1080, Split1125, ...); no match -> Fallback (pink).
- _exclusionArea comes from SpringBoard: +[UISDisplayContext(SpringBoard) sb_embeddedDisplayDefaultContext]
  -> +[UISDisplaySingleRectShape(SpringBoard) sb_thisDeviceDisplayShape] -> SBHScreenTypeForCurrentDevice
  (SpringBoardHome) = _SBHScreenTypeLookup(reference bounds in points, scale, SBFEffectiveArtworkSubtype,
  SBFEffectiveDeviceClass, SBFEffectiveHomeButtonType). Internal builds only: SBHomeScreenDefaults
  overriddenScreenType (os_variant_has_internal_content).
- Native canvas 1080x2160 @3 = 360x720 pt matches no iPhone, so the bar falls back. MG cache
  ArtworkDeviceSubType 570 changed nothing (tested, reverted; original in whyred/backup/).
- Options: (A) present 1080x2340 (iPhone 12 mini, 360x780 pt) and drop 180 rows when copying to the
  panel; (B) internal override; (C) home-button screen type (may lose gestures).
- MG key hash: base64(md5("MGCopyAnswer" + name)) without padding. Tools: ipsw 3.1.743 in the
  scratchpad, cache copied from whyred/ipsw/ios16-root.img (loop mount, sudo).

## USB Ethernet speed (2026-11-10)

- Before: one TRB per direction, a StartTransfer per frame, events polled every 1 ms:
  ~1 frame/ms. 115 MB download over SSH took 118 s (0.9 MB/s).
- Patch 0972 (bsd/dev/sweet_usb.c): RX = 16 TRBs x 2 KiB in one transfer (CSP + ISP_IMI + IOC per
  TRB, XferInProgress enabled on ep 0x01, re-armed after XferComplete); TX = up to 16 queued frames
  per transfer, IOC + LST on the last; TX queue 64; poll 250 us while busy, 1 ms idle. CPU never
  writes TRBs the controller owns (non-coherent DMA, shared cache lines).
- Kernel run 42832495994 (sha 63f14b72...e115). Same 115 MB file: 9.6 s = 11.5 MB/s (12.8x),
  sha256 matches. Upload 50 MB random: 8.3 MB/s, sha256 matches. ~11 frames per TX batch under load.
- Default image whyred/out/xnu-ios-fast5.img = fast4 + 0972.

## Battery: PM660 fuel gauge (2026-11-10)

- Linux reference (sysfs, charging): battery capacity 97, voltage_now 4161854, current_now -381835;
  bms current_now +381835 (FG raw positive = charging), temp 340 (34.0 C), charge_full_design 4000000.
  Linux picks profile ascent_3450mah (batt_id 60 kOhm) but "using OTP"; the pack is 4000 mAh.
- Patch 0973: bsd/dev/whyred_spmi.c (read-only SPMI v3 observer reads, APID cache) and SweetBattery
  sweetbattery=2: FG 0x4009/0x400a MSOC (Linux fg_get_msoc rounding), VBATT 0x41a0 (+CP 0x41a6)
  x 122.07 uV, IBATT 0x41a2 (+CP 0x41a8) signed x 488.28 uA (positive = DISCHARGING, measured
  unplugged: +280 mA; the Linux sysfs signs misled me), BATT_TEMP 0x4150
  (quarter K), SMB2 0x1006[2:0] charger state, 0x1310 bit 4 USB plugin. Design 4000 mAh. 10 s poll.
- APIDs: ppid 0x40 -> 30, 0x41 -> 31, 0x10 -> 7, 0x13 -> 10.
- First CI failed: missing kern/clock.h for delay(). Kernel run 42848264830 (sha 2d797f67...d5c7).
  First read under iOS: 100% (raw 255) 4294 mV +42 mA 36.0 C, charger state 3 (full-on), USB 1.
- Image whyred/out/xnu-ios-batt.img = fast5 + 0973 with sweetbattery=2.
- Unplug test: usb 0 -> +280 mA raw (discharging). Plugged into the laptop port the pack still
  drained 98 -> 95 % at +166..174 mA raw, charger state 3 (full-on), 38-40 C: the port loses to the
  load at 1804.8 MHz. Fix: sign back to raw positive = discharging; IsCharging needs current in.

## Status bar fixed: whyred_ui.dylib (2026-11-10)

- Idea from danstronics/ios-s9plus-exynos core/userland/src/display_native.c (hooks the provider choice
  and returns _UIStatusBarVisualProvider_LegacyPhone).
- whyred/userland/whyred_ui.c, built by build.sh without an SDK (clang + ld64.lld, text stubs for
  libobjc/libSystem, ad-hoc signed). Installed at /usr/lib/whyred_ui.dylib on the iOS root.
  Loaded by kern.exec_inject: boot-arg topaz_inject=/usr/lib/whyred_ui.dylib,SpringBoard,Preferences,MobileSafari.
- Measured: SpringBoard calls the selector on _iOS, _Phone and _Split; _Split returns nil for 360x720 pt
  (not Fallback) and UIKit draws the rdar placeholder. The dylib wraps +visualProviderSubclassForScreen:
  visualProviderInfo: on _iOS/_Phone/_Split/_DynamicSplit and turns nil (Split/DynamicSplit) or Fallback
  into LegacyPhone. Result: iPhone 8 bar with carrier "iPhone", 92 % and the charging bolt (webcam).
- Battery fix confirmed in the same boot (kernel run 42854797661): 91 % 4234 mV +114 mA charging=1.
- Default image whyred/out/xnu-ios-fast6.img = kernel 42854797661 + sweetbattery=2 + topaz_inject.

## Wi-Fi recon (2026-11-10)

- Linux: wlan0 up (WCN3990 via icnss + the modem's WLAN PD). Modem: pil-q6v5-mss with
  "No pas_id found" -> self-authenticating MBA boot (mba.mbn then modem.mdt/bNN into
  0x8ac00000..0x92200000), NOT the TrustZone PAS path topaz uses. Linux sources:
  drivers/soc/qcom/pil-msa.c, pil-q6v5.c, pil-q6v5-mss.c (lineage-21 tree).
- DT: qcom,mss@4080000 (qdsp6v62-1-5; reg qdsp6_base 0x4080000, halt_q6 0x1f63000, halt_modem
  0x1f65000, halt_nc 0x1f64000, rmb_base 0x4180000, restart_reg 0x179000, cxip_lm_vote_clear
  0x1fe5048), mem-protect-id 0xf, modem_fw_region 0x8ac00000 (0x7e00000), pil_mba_region
  0x94800000 (0x200000). icnss@18800000 (membase 0x18800000 size 0x800000, 12 IRQs 0x19d..0x1a9,
  SMMU anoc2 0x16c0000 SIDs 0x1a00/0x1a01, wlan-msa 0x85700000 1 MB). SMEM 0x86000000 (2 MB, hwlock 3).
  GLINK over SMEM to "mpss" (remote pid 1, IPCRTR channel, IRQ 0x1c4, APCS mailbox 0x17911000 bit 15).
- Firmware: modem partition mmcblk0p56 (FAT, mounted /vendor/firmware_mnt): image/mba.mbn,
  modem.mdt + b00..b28, wlanmdsp.mbn, bdwlan.bin / .1xx / .bxx. bt_firmware mmcblk0p38, persist p53.
- topaz reference (subagent survey of patches 0113-0130, 0900): one file iokit/Kernel/arm/TopazModem.cpp,
  polled thread. Generic and reusable: SMEM parser, SMP2P, GLINK, QRTR, QMI, pd-mapper/tftp/rmtfs,
  WLFW, CE/HTC/WMI-TLV/HTT (already WCN3990 ath10k snoc tables), WPA2, all of 0900 (en0, Apple80211
  ioctls). topaz README: joins WPA2, Safari works. SoC-specific: all addresses, PAS boot, SMMU SID,
  SMP2P bits, hardcoded firmware offset tables (FAT16 modem_a), UFS-only partition reads.
- Plan: (1) MBA modem boot, prototyped in the shim (fast local loop, screen output) until the
  modem reports SMP2P ready; (2) port into TopazModem.cpp with whyred addresses and eMMC FAT reads;
  (3) host services (rmtfs on modemst1/2/fsg, tftp, pd-mapper); (4) WLFW + CE with icnss addresses
  and SMMU; (5) enable 0900 for en0 / Settings.

## Modem MBA boot, step 1a works (2026-11-10)

- Shim MSS=1 (whyred/shim/mss.c), ramdisk = mba.mbn (0x3a2d0 bytes, ELF). Sequence: GCC clocks
  (cfg_ahb 0x18a000, bimc_mss_q6 0x14401c, boot_rom vote 0x152004 bit 10, gpll0_out_msscc 0x15200c
  bit 2, snoc 0x18a03c, mnoc 0x18a004: all on); MBA copied to 0x94800000; hyp_assign (SCM 0x42000c16)
  1 MB HLOS -> VMID 15 returned 0; restart pulse 0x179000; RMB MBA_IMAGE = 0x94800000; q6v55
  v62-1-5 power-up (XO CBCR 1, BHS status 1, 30 MEM_PWR banks, unclamp, reset release, CLK_ENA).
  Result: RMB PBL status 1 (success), MBA status 2 (XPU unlocked, scribbled). No RPM rail votes needed.
- Pitfalls: MSS registers (RMB 0x4180000) abort until gcc_mss_cfg_ahb runs (ESR 0x96000010).
  Q6SS 0x4080000 reads abort once the core runs (FAR 0x4080014, ESR 0x96000210): poll RMB only.
- Before power-up: Q6 RESET 7, PWR_CTL 0x00780000, MEM_PWR 0, GFMUX 0x100, BHS 0; RMB all 0.
- Shim: exceptions now reboot after 60 s (lands in Linux, then tools/to-fastboot.sh); SCALE=3
  makes the console readable through the webcam.
- Steps 1b + 1c work (same evening): shim reads mba.mbn, modem.mdt, modem.bNN from the stock "modem"
  partition (GPT name "modem", FAT16, image/, new whyred/shim/fat.c + emmc_read/emmc_part API) by
  ADMA2 straight into place. modem.mdt (0x20dc = ELF headers 0x3f4 + hash 0x1ce8) copied to
  0x94900000, hyp_assign to VMID 15 -> RMB META_DATA, CMD 1 -> status 3 (headers OK). Region
  0x8ac00000 + 0x7600000 hyp_assign to {15, HLOS} -> 28 PT_LOAD segments (0x366c202 bytes) loaded
  in order with verify_blob (CODE_START, CMD 2, CODE_LENGTH += memsz) -> status 4: AUTH COMPLETE.
  The modem runs. Image whyred/out/mss-1c.img (SCALE=3 MSS=1 ./build.sh, no ramdisk needed).
- A 57 MB ramdisk (tools/pack-modem.py) is accepted by fastboot but ABL never starts it and
  fastboot then rejects commands until `fastboot reboot bootloader`. Keep boot images small.
- Next (stage 2): move the MBA boot into the kernel (TopazModem.cpp structure) and talk to the
  running modem: SMEM 0x86000000, SMP2P, GLINK "mpss" IPCRTR, QRTR, then rmtfs/tftp/pd-mapper
  and WLFW.

## Modem boots from the iOS kernel (patch 0975, 2026-11-10)

- Kernel run 42888154568, boot-arg modem=1: "modem: FAT16, 12283 clusters of 16384 bytes, 309 files",
  SMEM version 12, MBA runs (PBL 1, MBA 2), metadata status 3, region assign 0, 28 segments, status 4.
  SMP2P item 435 up at 45 ms, slave-kernel 0x6 READY HANDOVER at 99 ms.
- GLINK: descriptor 478 already made by the modem, our TX FIFO 479 (16 KB) allocated, VERSION sent
  (APCS bit 15). The modem's FIFO 480 never appears in the apps-modem partition: rx 0, chan down.
  The modem goes FATAL at 34.8 s (crash reason empty). Next: dump items 478-480 in both partitions.
- Fix 1: topaz smem_get searched only the uncached item list; the SDM636 modem allocates FIFO 480 in
  the cached list (from the partition end). With the cached search (kernel run 42891561610): GLINK
  VERSION/ACK, modem opens glink_ssr + IPCRTR, QRTR HELLO from node 0, services servreg-notif 0x42
  and ssctl 0x2b; pd-mapper asked "tms/pddump_disabled"; rmtfs opened modemst1/2, fsg, fsc (oem_1/2
  missing), ALLOC -> FATAL 15 ms after (reads 1). topaz assigns rmtfs 3 MB to HLOS+MSS+NAV; whyred
  Linux (msm_sharedmem, no qcom,vm-nav-path, qcom,guard-memory): 2 MB to HLOS+MSS_MSA, 4 KB guards.

- Fix 2 works (kernel run 42894230510, modem=1, no wifi=1): rmtfs buffer a0404000+200000 to HLOS+MSS_MSA
  ret 0; ~42 QRTR services (nas, wds, uim, dms, ipa ...); pd-mapper tms/pdr_enabled, avs/audio,
  kernel/elf_loader; tftp served image/wlanmdsp.mbn (3717208 B) from the FAT index; at 722 ms
  *** WLFW service 0x45 up (node 0 port 0x53) ***: ind_register ok, host_cap rejected (err 1, not fatal),
  msa_info ok (2 MSA regions), msa_ready ok, cap ok: chip_id 140 family 4002 (WCN3990), board_id ff,
  fw 101c821a 2019-07-25, WLAN.HL.1.0.1.c2-00538-QCAHLSWMTPLZ-1.214870.1. Then "waiting MSA_READY ind",
  FATAL at 13.6 s (crash reason item 421 empty).
- Suspects for the 13.6 s FATAL: the host side of WLFW stops without wifi=1 (no BDF, wlan_cfg, mode);
  WCN3990 rails (icnss vdd-cx-mx, vdd-1.8-xo, vdd-1.3-rfa, vdd-3.3-ch0: RPM-SMD LDOs) never voted on.
- Next for wifi=1: SMMU anoc2 0x16c0000 SIDs 0x1a00/0x1a01 (topaz: apps SMMU 0x0C600000 SID 0x1A0),
  CE base 0x18800000 + 0x240000, shadow 0x18800000 + 0x32000, board file bdwlan.bin (board_id ff).
- Patch 0976 first run (kernel 42896938632, modem=1 wifi=1): anoc2 SMMU reads work: sCR0 0x00200626
  (USFCFG=1: unmatched streams fault), ID0 0x48017a06, ID1 0x40008015: 6 SMRs, 21 CBs, 4 KB pages x32.
  Then the modem thread stalled inside the topaz identity-bank writes (iOS kept running). The SMMU
  is qcom,skip-init: next run lists SMR/S2CR read-only (writes only with wifi_smmu=1).
- Read-only run (kernel 42898741854): all 6 SMRs invalid (0), every S2CR 0x000200ff (type 2 fault,
  cb 63): nothing routes SID 0x1a00. Linux qcom,iommu-dma = "bypass" -> SMR + S2CR type 1 (bypass).
- Crash reason now readable: "dog_hb.c:266:Task starvation: diag, ping: 4" at 13.7 s, after tftp
  "image/modem_pr/mcfg/configs/mcfg_hw/mbn_hw.dig" and mcfg_sw "not found": the FAT index only went one
  directory deep. Fixed: recursive (8 levels), 96-byte paths, 4096 entries.

## WCN3990 rails (2026-11-10)

- Read-only dump (kernel 42904705967), PM660 SID 1 LDOs before the modem boots: l6 (vdd-1.3-rfa) 0x4500
  status 94 vset 1304 mV en_ctl 80; l9 (vdd-1.8-xo) 0x4800 status 94 1800 mV en_ctl 80; l19 (vdd-3.3-ch0)
  0x5200 status 94 3300 mV en_ctl 80; l5 (vdd-cx-mx) 0x4400 status 00 vset 848 mV en_ctl 00: OFF.
  APIDs: 0x144 -> 98, 0x145 -> 118, 0x148 -> 132, 0x152 -> 155.
- Patch 0977: whyred_spmi_write (only SID 1 0x4446, apps-owned channel) and, with wifi=1, l5 on before
  the modem boots.
- Result (kernel 42914059374): the l5 EN_CTL write returned -1, rails unchanged, modem FATAL at 13.6 s
  (diag starvation) as before. The PMIC arbiter refuses direct writes to this LDO (RPM-owned): it
  needs an RPM-SMD request (glink-rpm over RPM message RAM, "ldoa" 5, swen/uv). Next build logs the
  exact arbiter status, and adds sysctl kern.whyred_backlight (WLED off/on for a dark test loop).

## RPM-SMD client, WCN3990 power (2026-11-10/11)

- Patch 0978: GLINK over RPM msg RAM 0x778000 (0x7000). TOC "grt0" at the end: {magic, count,
  {id, offset, size}}; count 10, ap2r at 0x200 (0x6f8 bytes), r2ap at 0x900 (0x6f8). VERSION 1 ->
  RPM sends VERSION + VERSION_ACK, opens two channels (rcid 2 and 3, 13- and 10-byte names); our
  "rpm_requests" lcid 1 OPEN_ACK ok, remote rpm_requests rcid 3. Requests acknowledged with
  "req" header + "msg#" (0x2367736d) answers. After "ldoa" 5 uv 848000 swen 1: pm660 l5 status 94,
  en_ctl 80: on. Kernel run 42919412920.
- Modem still FATAL at 13.7 s (diag starvation). Linux icnss (not ath10k) never waits for
  MSA_READY_IND and sends no board data; after cap: icnss_hw_power_on (rails + clock
  cxo_ref_clk_pin = RPM_SMD_RF_CLK1_PIN: "clka" id 4, key "pccb" 1), then waits FW_READY_IND.
  topaz's w_start_bdf waited for MSA_READY_IND, so the host stalled after cap.
- Next build: rf_clk1 pin-ctrl vote, after cap wait for FW_READY directly, no BDF.
- Kernel 42921074876: rpm ldoa 5 and clka 4 both acked (active + sleep), WLFW goes straight to
  "waiting FW_READY" after cap; no FW_READY, modem FATAL at 13.6 s with diag starvation again (also
  without wifi=1 earlier): not WLAN-specific. Next: MSS proxy votes as pil_mss_make_proxy_votes
  ("rwcx" 0 / "rwmx" 0 vlvl 384, qdss "clk0" 1 STAT 1); boot-arg modem_proxy=0 skips them.
- Kernel 42922409124: proxy votes cx/mx/qdss acked; FATAL moved from 13.6 s to 17.2 s, still diag
  starvation. topaz's QRTR ignored NEW_LOOKUP (type 10) and data for unserved ports silently.
  Next build answers lookups as net/qrtr/ns.c (matching NEW_SERVER + all-zero terminator) and logs
  unknown control types and unclaimed data.

- Kernel 42925705109: lookups answered, still FATAL 13.6 s, only DEL_CLIENT control traffic.
- Diag starvation cause: the apps side must serve diag (Linux drivers/char/diag/diagfwd_socket.c:
  service 0x1001, MODEM_INST_BASE 0, CNTL 0 / DATA 2 / DCI 4 are apps servers, instance raw in
  NEW_SERVER). Kernel 42927375140 announces them on ports 0x4005-0x4007 and drops the data: the
  modem connects at once (cntl from port 0x1b: 15, 9, 2208, 16 ... bytes), and stays up (QRTR
  traffic at modem t=74 s, no FATAL). Fixed.
- Still no FW_READY_IND. Linux gets it 2 s after the QMI connect (dmesg 26.58 s -> 28.57 s).
  MSA assign matches Linux icnss (MSS_MSA + WLAN [+ WLAN_CE]). Suspects: anoc2 SMMU faults every
  stream (WLAN SIDs 0x1a00/0x1a01, icnss qcom,iommu-dma = "bypass"), and its clock
  smmu_aggr2_noc_clk = RPM "aggr" id 2 (key "KHz") was never voted.
- Kernel 42929010942: votes aggr 2 at 19.2 MHz; boot-arg wifi_smmu=2 writes one SMR (sid 0x1a00
  mask 1) with S2CR type 1 (bypass), logging before each write.

- Kernel 42929010942 wifi_smmu=2: SMR/S2CR writes land, S2CR bypass (type 1) reads back as fault
  (type 2), the sTLBGSYNC write stalls the thread. Kernel 42932945989: S1-bypass context bank CB4
  (CBAR 0x1f000, SCTLR 0xe0), S2CR translate to CB4, SMR0 0x80011a00, no global sync: reads back OK.
- Kernel 42935086878: l5 + rf_clk1 voted after cap (as icnss_hw_power_on). The modem then sets
  l6/l9/l19 to mode 05 hw_en 20 and l19 to 3200 mV itself. Still no FW_READY.
- Kernel 42937371808: QMI sequence now as Linux icnss (ind_register with fw_ready, msa_ready,
  pin_connect, client_id; no host_cap; dynamic_feature_mask 1 after power on, acked). This
  firmware rejects rejuvenate_enable (0x19) with err 1. Still no indication of any kind and no
  tftp request for bdwlan: the WLAN PD stalls before it talks to the chip.

- memshare (QMI 0x34 v1 inst 1, Linux msm_memshare): the modem asks client 1 for 15 MB right
  after boot; served from 0x96000000 (free RAM below the splash, outside XNU), assigned to MSS_MSA.
- Linux apps-side QRTR services (qrtr-lookup on node 1): tftp 0x1000 (versions 1..12), rmtfs 14,
  servreg-loc 64, memshare 52, IPA ctl 49, diag 0x1001 for MODEM/LPASS/WCNSS/SENSORS/CDSP/WDSP,
  plus userspace 53/16, 55/1, 271, 280, 770, 1053. WCNSS diag (128+) is never used by the modem.
- Linux bt_power (bt_wcn3990) votes l9 1.8 V, l6 1.304 V, l19 3.312 V, BOB pin1 (pcv1 3.6 V,
  bobm 2) before the modem boots; icnss uses the l6/l9/l19 *_pin_ctrl regulators ("pcen" 2).
  With both, PMIC hw_en reads 0x22 on l6/l9/l19.
- FW_READY cause (Linux logcat): the userspace cnss-daemon is a second WLFW client. After cap it
  sends BDF downloads (regdb.bin missing, bdwlan.bin from firmware_mnt/image) and cal_report.
  The firmware answers the last BDF segment with err 1 on Linux too. Kernel 42947778970 does
  bdwlan.bin (4 segments, last err 1) + cal_report: PIN_CONNECT_RESULT ind (0x2c), FW_READY at
  2.4 s, wlan_cfg ok, wlan_mode mission ok, WMI SERVICE_READY, scan sees real APs (ch 6 -67 dBm,
  ch 40 -83 dBm). en0 attaches, wifid queries Apple80211 (patch 0900); no scan request seen yet.
- Wi-Fi test image: whyred/out/xnu-ios-wifi-cal.img (boot-args + modem=1 wifi=1 wifi_smmu=2).
- WLED: one boot had the init read fail (status 0x9), which disables backlight control for the
  boot (kern.whyred_backlight EIO). Needs a retry.

- First real join (user, Settings): WPA2 to the home AP, DHCP, Safari loads apple.com. Status bar
  Wi-Fi icon grey: wifid asks RSSI (16) rarely; kernel 0981 now posts APPLE80211_M_LINK_QUALITY (17).
- Link lost after ~6.6 min: deauth reason 16 (group key handshake timeout); topaz never answered
  EAPOL group message 1/2. Then the WLAN PD restarted (WLFW on a new port, MSA re-assign ret -2)
  and our CE/HTC/WMI kept the old state: every scan timed out (errno 60), empty list.
- Fixes (0978): group key handshake answered (verified: "group key renewed (idx 2)" 3 min after the
  join, link kept); wlan_host_reset() on WLFW restart, CE/WMI/HTT memory reused; boot-arg
  wlan_pdr_test=N restarts the PD via servreg-notif RESTART_PD (0x24) to test it.
- After a reboot wifid does not auto-join the saved network (scans only, no ASSOCIATE). Joining by
  tapping it in Settings works.
- 0980: kern.whyred_touch (quad) injects a one-finger tap/swipe: x1 | y1<<12 | x2<<24 | y2<<36 |
  ms<<48. Unlock after boot: 540,2158 -> 540,700 in 250 ms. scratchpad tap.sh wraps it + webcam.
- Ghost volume / screenshot presses: garbage SPMI reads (GPIO7 RT 0x10/0x88/0xfc, PON 0x75) with
  no error bit while the modem runs. 0980 drops implausible values and debounces 3 samples.
- Keyboard 123/abc dead: UIKit size providers answer 320 pt (iPhone SE) on the 360-pt screen; each
  keyplane change set another frame size -> updateLayout -> reload -> initial keyplane. whyred_ui
  wraps the providers (UIKeyboard/UIKeyboardImpl +defaultSize..., -defaultSizeForInterfaceOrientation:)
  to answer 360 (as danstronics keyboard_native.inc). Lookup with class_copyMethodList:
  class_getClassMethod in the constructor ran +initialize too early and recursed (stack overflow).

- Night 2026-11-11 (unattended, touch injection + webcam):
  - Keyboard fix verified: 123 switches to numbers-and-punctuation and back; installed for all apps.
  - Group key fix verified twice (renewals at +3 min, +11 min). Then at +16.5 min the AP ended the
    link with reason 7: wifid scanned every ~7 s while joined, each scan ~7 s off-channel. Kernel
    42965871166: while joined, scans are answered from the cache, a real one at most every 60 s
    (verified: scans 20-23 ~71 s apart while joined).
  - PD restart recovery verified with wlan_pdr_test (kernel 42965871166): host reset -> FW_READY ->
    CE reprogrammed on the same memory -> HTC READY -> WMI READY -> HTT -> scans work again (~2 s).
  - wifid never auto-joins (after boot, after a link loss, after a PD restart): it scans, reads the
    results, but sends ASSOCIATE (20) only on a tap in Settings. Requests still refused: 383 (set,
    ~1 per 10 s), 115, 96, 97, 106 RSSI_BOUNDS, 387, 346, 422, 51 COUNTRY_CODE. Next lead for
    auto-join. DEAUTH (29) is answered now (kernel 42968887721).
  - Lock screen ignores the unlock swipe while our WLED is off: light it (kern.whyred_backlight=1),
    swipe 540,2158 -> 540,700 / 250 ms, dark again. Settings dock icon 945,2040, Wi-Fi row 400,960,
    first known network row 300,787 (after the list loads, ~2 scans).

## App crashes and hangs (2026-11-11)

Crash logs: /var/mobile/Library/Logs/CrashReporter (bug_type 309 crash, 509 hang/watchdog
stackshot; symbolicate 509 frames as 0x180000000 + offset with `ipsw dyld a2s`). Daemon stacks:
`/usr/sbin/spindump PID 3 -file /tmp/x.txt -onlyTarget` (slide = spindump libdispatch base -
0x18db34000).

- Photos black screen: main thread in +[PLImageFormat _syncFormats] -> MCCopyCheckpointValue ->
  checkpointd, which waits forever on com.apple.fairplayd.versioned. fairplayd.H2 exits at once
  (needs FairPlayIOKit, not in our kernel), launchd respawned it 1636 times, the queued message
  is never read. Same for sharingd: its main() waits on bluetoothd (also respawn-looping, no BT),
  so Safari's Share button hung in -[SFShareSheetSlotManager ensureConnectionEstablished].
  Fix (on the phone, persistent): `launchctl bootout` + `launchctl disable` of
  user/501/com.apple.fairplayd.H2 and user/501/com.apple.bluetoothd (these LaunchDaemons sit in
  the user/foreground domain). Clients now fail fast; sharingd reaches its run loop.
- Settings > Safari crashed: SafariCore asks sandbox_container_path_for_pid (Sandbox op 4);
  patch 0936 returned 0 without writing a path when CFFIXED_USER_HOME is absent (Preferences has
  only HOME), so NSString got stack garbage -> nil -> fileURLWithPath: threw. 0982 returns ENOENT
  (also on the early exits), as for an unsandboxed process.
- Safari long press on an image: QR detector -> +[CIContext contextWithOptions:] -> GL ES context
  -> NULL function. The "software_renderer" option is ignored on iOS (only sets priority_request_low).
  whyred_ui makes -[CIContext initWithOptions:] answer nil, as CI does when no renderer works
  (verified: long press works, 2026-11-11).
- Wi-Fi join failed after a reboot: no auth reply on either band (fixed MAC 02:54:4F:50:41:5A, the AP
  still held the old association). 0978 now sends a deauth (reason 3) before the peer and auth, and
  retries auth 8 times. Verified: auth reply at +36 ms, joined.
- Battery 12% is real (3613 mV; on the laptop port the phone still loses ~340 mA). The 100% flashes
  came from SweetBattery's fallback on a failed SPMI read; 0983 keeps the last reading.
- Photos still crashes: no Photos.sqlite. At boot assetsd threw "assetsd requires TCCPhotosService
  access" (-[PLPrivacy photosAccessAllowedWithScope:] returns q, nonzero = throw). After a kickstart
  it stays up but creates no library; Photos asserts numberOfSections == 1. assetsd logs only to
  os_log (no `log` tool; applog's OS_ACTIVITY_DT_MODE showed nothing; launchd ignores edited plists).
  Next: parse /var/db/diagnostics on the laptop.
- whyred_ui now loads everywhere: `kern.exec_inject="/usr/lib/whyred_ui.dylib *app"` (0947: every
  program with ".app/" in its path; boot-arg topaz_inject=/usr/lib/whyred_ui.dylib,*app). Spotlight
  is its own process (/Applications/Spotlight.app), which is why 123 failed there.
- AegirPoster (Astronomy wallpaper) crash-loops in NUNIAegirRenderer (Metal): no GPU. Unrelated.
- Wi-Fi icon: SBWiFiManager (bars from wifid SCALED_RSSI, dBm from RSSI.RSSI_CTL_AGR) reports
  bars 3, rssi -66, associated, primary interface (whyred_ui probe in SpringBoard, 2026-11-11).

## Wi-Fi away from home (2026-11-23, hotel, Aruba APs)

- 0984 (sandbox extensions) verified: Photos opens, library created.
- Joins failed at the hotel. Findings, in order:
  - 0985: the channel list marked every channel passive (bit 7); the firmware does not transmit on a
    passive channel until it hears a beacon. Now passive only on radar channels 52-144 (+DFS bit 10).
  - 0986: assoc request now carries HT capabilities and WMM when the AP has them; joins log frames
    from the AP. 0987: regdomain ETSI1_WORLD (0x37, CTL 0x30) and TX power limit 20 dBm after WMI
    ready, as ath10k does.
  - Linux on the phone joined Hotel_Galeria at once, but was steered from the 2.4 GHz BSS to the same
    AP's 5 GHz BSS (Aruba band steering). 0988 prefers 5 GHz (+15 dB when >= -80 dBm).
  - 0989 logs WMI MGMT_TX_COMPLETION (event 0x7006: 0 acked, 1 discarded, 3 no ack): every auth and
    assoc request is acked by the AP, inline or by DMA. The AP answers auth, never assoc.
  - 0990: random locally administered address each boot (blacklist theory): no change, assoc still
    unanswered on 5280 MHz.
  - After a failed join the next join transmits nothing (no completions) and scans stop. 0991 deletes
    and recreates vdev 0 after a failed join and logs WMI credits/queue at each join.
- Unattended UI here: webcam sees the phone (shot.sh crop 620:320:570:530, transpose=1); tap mapping
  phone_x = (cx-30)*2, phone_y = (cy-100)*2.15+45 on the 1200-px-tall crop. Keyboard join key
  880,1958; backspace 950,1791. whyred_ui in Preferences types /var/mobile/whyred_type into the
  first responder (owned by mobile).
- Linux's assoc request to the same AP (wpa_supplicant "req_ies", 2.4 GHz): SSID, rates
  82 84 0b 16 0c 12 18 24, HT cap 2d 01 13 ff.., RSN caps 08 00, ext rates 30 48 60 6c, op classes
  51 53 54 73-78 7c-80 00 82 80, ext caps 00 00 08, WMM, Qualcomm vendor IE. AP answered in 4 ms.
- 0992 sends the same layout: still acked and never answered (2026-11-23, kernel 45093259324).
- 0991 verified: after a failed join the next one transmits again (completions logged).
- Join chose 2472 MHz again on that boot (the 5 GHz BSS of Hotel_Galeria was not in the scan table).
- Still open: the AP acks our assoc request and never answers; Linux on the same phone and AP is
  answered. Not content (0992), not the address (0990), not band (5280 MHz also silent), not a scan
  during the join.
- SOLVED by 0993 (kernel 45142594955, image whyred/out/xnu-ios-0993.img): w_mgmt_hdr left the
  sequence control at 0 in every frame and the firmware sends it as is. The Aruba AP took the assoc
  request (seq 0) for a repeat of the auth (seq 0), acked it and dropped it. Home APs did not care.
  Each mgmt frame now carries its own sequence number. Joined Hotel_Galeria at once: auth reply 4 ms,
  assoc reply (status 0, aid 1) 5 ms after the request, 4-way handshake done 9 ms later (2472 MHz,
  -49 dBm beat the 5 GHz BSS at -66 +15). 0993 also sets listen interval 1 and logs join candidates.

## Wi-Fi auto-join after a reboot (2026-11-23)

- wifid scans and finds the saved network, then logs "Join candidate ... is not allowed (error=(1 'Below
  minimum RSSI threshold'))" for every BSS (-64 dBm included). Read with the unified log parser.
- wifid (/usr/sbin/wifid, 0x100128a20): reject when scanResult.RSSI < threshold; the threshold
  (0x10010b0a0) defaults to device[+0x80], set from driver[+0x100] = RSSI_THRESHOLD of
  Apple80211CopyValue(106 RSSI_BOUNDS). Our kernel refused 106, so the threshold was 0 dBm.
- IO80211 decodes 106 as 16 bytes: uint32 version, int32 RSSI_MIN @4, RSSI_MAX @8, RSSI_THRESHOLD @12.
  0995 answers 1, -95, -30, -80. 0994 (noise -95, SNR in scan results, NOISE 17) did not change it.
- 0995 verified (CI 45155342324, image whyred/out/xnu-ios-0995.img): after a reboot, with no taps, wifid
  joined the saved Hotel_Galeria 49 s after boot (auth, assoc status 0, 4-way handshake done).
- Unified logs: the phone clock is 1969, so ls -t on Persist puts new tracev3 files last; take the
  highest numbers. logd does not flush on /sbin/reboot.
- Tools: ipsw (blacktop) extracts IO80211 from the IPSW's shared cache; 7z reads the local HFS+
  snapshot whyred/ipsw/iosroot-snapshot-20261109.img (wifid at iOSRoot/usr/sbin/wifid).


## Idle power and the 1970 clock (2026-11-23)

- Before: about 410 CPU-s per 120 s idle, battery -670 mA on USB. After the fixes below: about 25 CPU-s
  per 120 s, about -60 mA. Measure with `ps -ax -o time,command | sort -r | head` twice, 60 s apart.
- Main cost: coreduetd at a full core. spindump: its knowledgestorage.execution queue answers
  _DKPredictionQuery from dasd (about 65 ms each, thousands). dasd log (category budgetModulation):
  "Last modulated budgets at Wed Dec 31 16:01:06 1969, modulating for 1994946 slots", then one query
  per 2 hours of 15 min slots back toward 1970.
- Cause: every boot starts at 1970 until Wi-Fi time sync. dasd writes lastModulation =
  now into /var/mobile/Library/Preferences/com.apple.dasd.widgetRefreshBudgets.plist about 67 s after
  boot. The next modulation after the real time arrives walks 56 years.
- Fix by hand: write the plist with lastModulation = now, and kill cfprefsd, dasd and coreduetd in the
  same step. cfprefsd caches the plist. An edit without the cfprefsd restart had no effect. killall did
  not kill dasd. kill -9 by pid works, and launchd restarts them on demand.
- Also done: Photos "For You" widget removed from IconState.plist (backup /var/root/IconState.plist.orig).
  Its prediction was NaN and dasd recomputed it in a loop. vmd, wifip2pd and nanotimekitcompaniond
  disabled with launchctl disable. BTAudioHALPlugin.driver moved to /var/root/disabled-plugins/.
- The PM660 RTC (SID 0, 0x6048..0x604b) counts seconds and keeps them across reboots. TZ refuses writes
  (hwclock -w: EPERM). Offset to real time 1767200547 (rtc 28251469 at 2026-11-23 16:40:16 UTC).
- 0996: sysctl kern.whyred_rtc read -1. All 8 PPID slots of whyred_spmi.c were taken before the RTC.
  0997: 16 slots, kern.whyred_rtc works (CI 45215490554, macos-15-intel).
- Launchd job com.whyred.clock (whyred/tools/add-clock.sh, whyred/userland/whyred-clock.sh): sets the
  clock from the RTC plus /var/root/whyred_rtc_offset and keeps the offset fresh. It works, but its
  first command ran after dasd had saved a 1970 date. Too late.
- 0998: the kernel sets the clock from SweetBattery's 1 s timer while it reads before 2023, using
  boot-arg whyred_rtcoff=1767200547 (add it to the image's boot-args). CI 45226449453, image
  whyred/out/xnu-ios-0998.img. Verified: "WHYRED RTC: clock set" twice before exec launchd (bsd_init
  resets the calendar once, the next tick sets it again), phone matches the laptop to the second, dasd
  saved lastModulation 2026-11-23 18:32, coreduetd 0.00 CPU-s per minute.
- If the RTC loses power, the offset changes: take the new value from /var/root/whyred_rtc_offset
  (the launchd job keeps it fresh) and rebuild the image with it.
- Slow artifact downloads at the hotel (about 20 KB/s per connection to the Azure blob store, while the
  line gives about 1 MB/s): fetch the zip with aria2c -x 8 -s 8 from the artifacts/ID/zip redirect.
- Laptop note: VMware vmnet8 used 192.168.2.0/24, the hotel LAN, and owned the gateway address
  192.168.2.1. Moved to 192.168.150.0/24 (backups *.bak in /etc/vmware).
- CI: macOS arm64 runners often fail with "not acquired by Runner" (capacity). Use
  `-f runner=macos-15-intel`.


## Auto-lock: the lock screen never went dark by itself (2026-11-23)

- The lock button worked (0970/0971), but an untouched lock screen stayed lit for minutes.
- backboardd AttentionAwareness log: "reset attention lost timeout for SpringBoard" about once a second
  (its own clock, 322, 323, ... s). The unified log parser shows these 19 ms apart: it reads 19.2 MHz
  ticks as ns (factor 52), so use the AttentionAwareness clock for spacing.
- Backtraces from a whyred_ui hook on -[SBIdleTimerGlobalCoordinator resetIdleTimer], symbolicated with
  ipsw dyld a2s (SpringBoard.framework __TEXT 0x1c57ca000): NSFireDelayedPerform ->
  -[SBTelephonyManager _handleTelephonyDaemonRestart] -> handleCallControlFailure: -> updateCalls ->
  updateSpringBoard -> resetIdleTimer. CommCenter is not in launchd.plist (image v13), the
  CTServerConnection fails with (2, 5), and the restart handler reschedules itself every second.
- Fix in whyred_ui.c (SpringBoard): the first _handleTelephonyDaemonRestart runs, later ones do nothing.
  Verified: display off and WLED off about 20 s after a SpringBoard restart, and it stays off.
- ReportCrash then stood out (4 CPU-min): AegirPoster (Astronomy lock-screen wallpaper extension) crashed
  96 times in one boot and PosterBoard relaunched it. Moved
  /System/Library/CoreServices/AegirProxyApp.app/PlugIns/AegirPoster.appex to /var/root/disabled-plugins/.
- Idle with both fixes and the screen off by auto-lock: about 6 CPU-s per 120 s, all processes summed
  (ps time, whole seconds). Was about 25 earlier today, about 410 before the power work.
- Install: whyred/userland/build.sh, then copy out/whyred_ui.dylib to /usr/lib/whyred_ui.dylib on the
  phone (previous copy /usr/lib/whyred_ui.dylib.bak) and restart SpringBoard.

## Brightness slider (2026-11-23)

- Both sliders call BackBoardServices, not CoreBrightness's client: Settings
  -[DBSBrightnessManager setBacklightValue:specifier:] -> BKSDisplayBrightnessTransactionCreate +
  BKSDisplayBrightnessSet -> backboardd -[BKDisplayBrightnessController setBrightnessLevel:reason:options:]
  (v36@0:8f16@20Q28, level 0..1). backboardd's own classes are in /usr/libexec/backboardd, not the cache.
- whyred_ui.c in backboardd: after the original, the level goes to IOMFBBrightnessLevel on AppleCLCD
  (nits 16.16 = 500 * level^2, at least 2; SweetFramebuffer.cpp -> PM660L WLED). brightnessLevel answers
  from that property; isBrightnessLevelControlAvailable answers YES (Control Center's slider did not move).
  Hooks on BrightnessSystemClient in SpringBoard/Preferences stay for logging; DisplayBrightness never
  went through them.
- backboardd is not in an .app, so topaz_inject names it: /usr/lib/whyred_ui.dylib,*app,backboardd.
  At runtime: sysctl kern.exec_inject="/usr/lib/whyred_ui.dylib *app backboardd", then restart backboardd.
- Verified: both sliders change the brightness (user). At boot backboardd restores the saved level
  ("synchronizeALSPreferencesAndDisplayBrightness", 0.337 -> panel level 0xe9).
- Boot-args of whyred/out/xnu-ios-0998.img (kernel CI 45226449453):
  debug=0xc serial=3 cpus=4 keepsyms=1 -noprogress rd=emmcroot0 emmcroot=userdata emmcrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1 sweetcpufreq=0 sweetbattery=2 panicreset=60 sweetusb=2 sweetfb_native=1 sweetkeys=1 sweetsyna=2 sweetfb_noconsole=1 sweetfb_hz=30 topaz_inject=/usr/lib/whyred_ui.dylib,*app,backboardd modem=1 wifi=1 wifi_smmu=2 whyred_rtcoff=1767200547
- Webcam shots (shot.sh) were black at the hotel: no view of the phone, the user tests by hand.
- Auto-brightness needs the ambient light sensor (behind the sensor DSP); not done.

## CPU load governor (patch 0999, 2026-11-23)

- sweet_thermal.c now also picks the Gold OSM row by load every 50 ms: the busiest CPU's busy share
  (whyred_cpu_idle_mach, osfmk/kern/recount.c, from recount_processor_usage) times the current speed
  with 25 % headroom; to the top row from 90 % busy; down only after 4 looks. Floor row 1 (1113.6 MHz,
  same voltage as row 0 at 288 MHz). Thermal steps set the ceiling. sweetgov=0 keeps the ceiling row.
  Once a minute: "WHYRED GOV: last minute % at rows 0..4 ...". CI 45250324901, image xnu-ios-0999.img.
- A/B, same kernel, screen off, 6 min settle then 4 min of sweettherm=2 readings, on USB power:
  sweetgov=0: row 4 all the time, CPRH 792 mV, Gold 44.0 C. Governor: row 1 88-99 % of most minutes
  (one minute 34 % at row 4 during background work), CPRH 684 mV average (664-668 at row 1), 43.2 C.
- CPRH closed loop trims below the table: 792 mV at row 4 (table 840), about 666 at row 1 (table 700).
- Battery current cannot show the gain while plugged in: near full the charger feeds the phone and
  InstantAmperage reads about 0 (ioreg prints negative values as unsigned 64-bit).
- Boot with the screen on: first minute 81 % at row 4 (boot work), then mostly row 1 when idle.

## Vibration motor (patch 1000, 2026-11-23)

- Live DT qcom,haptic@c000 on PM660 (SID 1): ERM, play-mode direct, vmax 3000 mV, ilim 800 mA.
  Register layout from the phone's Linux qpnp-haptic.c (lineage-21): STATUS 0x0a, EN_CTL 0x46 bit 7,
  CFG1 0x4c bit 0 (1 = ERM), CFG2 0x4d, SEL 0x4e [5:4] (0 = direct), VMAX 0x51 [5:1] = mV/116, overdrive
  bit 6, ILIM 0x52 bit 0 (1 = 800 mA), PLAY 0x70 bit 7. SEC_ACCESS 0xd0 (0xa5) only for TEST2 0xe3.
- PPID 0x1c0 = APID 63, apps-owned: writes work. As found after Linux -> fastboot: status 0, en 0,
  cfg1 0 (LRA), cfg2 0x1, sel 0, vmax 0x22, ilim 0, play 0.
- 1000: whyred_spmi.c sets ERM/direct/3000 mV/800 mA on first use; sysctl kern.whyred_vibrate=MS (any
  user, at most 2000 ms, 0 stops) runs EN_CTL+PLAY and a thread call stops it. Verified by the user:
  three 700 ms buzzes. Also: SPMI write channels are now mapped once per slot (each write used to map).
- CI 45260351732, image whyred/out/xnu-ios-1000.img (same boot-args as 0999).
- iOS side: mediaserverd hosts AVHapticServer (AudioToolbox), but it never starts here: "ServerManager:
  Failed to allocate HapticSynth (exception)", "-[AVHapticServer init]: ... server bailing out" (no
  haptic audio output). So -[AVHapticServer playVibePattern:...] is never called (a hook there stayed silent).
- whyred_ui.c interposes (dyld __interpose) AudioServicesPlaySystemSound, ...WithCompletion,
  ...WithOptions, ...WithVibration and AudioServicesStopSystemSound in every injected process: a VibePattern
  array (pairs on, ms) plays on the motor; system sound IDs that vibrate on an iPhone use a table (4095
  vibrate 400 ms, 1011 400, 1106 power connected 150, 1350/1351 400, 1519/1520 80, 1521 three 80 ms).
  Verified: the vibe test tool (whyred/userland/tools/vibe.c, needs "vibe" in kern.exec_inject) and
  plugging in USB (SpringBoard plays 1106 with options nil) buzz the motor (user).
- Not covered yet: CoreHaptics and UIKit feedback (UIImpactFeedbackGenerator), which go to AVHapticServer
  directly. ToneLibrary alerts (ringtones, notifications) are expected to pass VibePattern options; not yet
  seen on the phone (no notifications and no Clock app on this root).

## ADSP boots under XNU (patch 1001, 2026-11-24)

- Hardware (live DT, Linux dmesg): loudspeaker = TI TAS2557 at 0x4c on I2C 0xc1b6000 (i2c6, shared with
  NFC; reset GPIO 77, irq GPIO 73); earpiece/headset/mics = PM660L analog codec (anlg-cdc@f000) + LPASS
  digital codec msm-sdw-codec@152c1000. Both codecs wait for the ADSP in Linux ("Adsp is not loaded yet").
  tasha/tavil (WCD933x/934x) nodes exist but do not probe.
- ADSP PIL (qcom,lpass@15700000): pil-tz-generic, PAS id 1, firmware "adsp" (image/adsp.* on the modem
  FAT), carveout adsp_fw 0x92a00000+0x1e00000, SMP2P items 443 in / 429 out (APCS bit 10), GLINK edge
  "lpass" (SMEM items 478/479/480, APCS bit 9). Channels the ADSP opens: glink_ssr, IPCRTR, apr_audio_svc,
  apr_apps2, fastrpcglink-apps-dsp, LOOPBACK_CTL_LPASS.
- 1001 adapts the inherited TopazAudio.cpp (topaz SM6225 ADSP + AudioReach): carveout, GLINK bit, and APR
  on apr_audio_svc instead of GPR. APR header 0x250 (seq cmd, 5 words), core svc 3, apps 5 -> adsp 4;
  AVCS_CMD_ADSP_EVENT_GET_STATE 0x1290c, reply 0x1290d.
- Verified (boot-arg adsp=1, CI 45290295636): PAS init_image/mem_setup/auth_and_reset ret 0, slave-kernel
  READY at t=20 ms, GLINK and QRTR up (node 5, 20 services), pd-mapper answers "avs/audio". First APR
  ask (t=32 ms) got APR_BASIC_RSP_RESULT status 0x16 (not ready yet); one second later state 1:
  "ADSP APR READY". Still up after 60 s. No crypto bus or extra RPM votes were needed.
- The iOS side exists in the repo: userland/audio/audio_plugin.c (HackiOS RedwoodAudio HAL plug-in:
  Speaker, Hawking, Actuator = Taptic output, Codec mic) writing to /dev/redwood-audio (and
  /dev/redwood-haptic); TopazAudio provides /dev/topaz-audio + the redwood alias (0935).
- Speaker path (Linux sdm660-internal.c, DT): PRI_MI2S_RX (AFE port 0x1000, msm-dai-q6-mi2s.0, rx-lines
  1 = SD0, tx-lines 2 = SD1 for the amp's feedback) -> "tas2557 ASI1" (ti,i2s-bits 16). Pins GPIO 12 SCK
  (pri_mi2s, func 2), 13 WS (pri_mi2s_ws, func 3), 14 SD0, 15 SD1 (func 3), 8 mA, no pull (NORTH tile).
- 1002 (verified, CI 45295258705/45295642148): APR to AFE (svc 4), v1 parameter packets:
  AFE_SVC_CMD_SET_PARAM 0x100f3 + AFE_MODULE_CLOCK_SET/AFE_PARAM_ID_CLOCK_SET (PRI_MI2S_IBIT 0x100,
  1.536 MHz, attr COUPLE_NO) status 0; AFE_PORT_CMD_SET_PARAM_V2 0x100ef + I2S_CONFIG (16 bit, SD0,
  stereo, WS internal, 48 kHz) status 0; AFE_PORT_CMD_DEVICE_START 0x100e5 status 0.
- TAS2557 on BLSP2 QUP2 (0xc1b6000; GPIO 22/23 func 3, CENTER tile 0x03500000; GCC BLSP2 AHB vote 0x52004
  bit 15, CBCR 0x25004; QUP2 I2C RCG 0x28020/0x28024, CBCR 0x28008). QUP hw 0x20070000, FIFO 64/64.
  Reset GPIO 77 low 5 ms then high. 1003 (verified): book 0 / page 0, register 3 = 0xa0 (PG2.1): the
  firmware is tas2557_uCDSP.bin (24 KB, in whyred-linux build/firmware; vendor file, not in git).
  pm660l_l6 (its supply) was already on.
- TAS2557 configuration (1004, 1006): our parser of tas2557_uCDSP.bin (TI PPC format, driver version
  0x400: 4 PLLs, 4 programs, 7 configurations; program 0 "Tuning Mode", configuration 0 48 kHz / BCLK
  1.536 MHz). Sequence as tas2557_set_program + enable: SW reset, defaults (SAR ADC2, clock error
  controls, safe guard 0x5a, ASI1 16 bit + DOUT tri-state), program blocks (type 1, program checksum
  verified via CRC regs 0/0/33 + 0/0/32), PLL, configuration pre (4) and coefficients (3), startup,
  unmute (soft mute is book 100). Result: power 0xf8/0xa3, speaker control 0x7c. The QUP staging buffer
  must hold the 48-byte bursts (1006: 40 refused them).
- Stream (1005-1015): APR ASM (svc 7, port session << 8 | 1) and ADM (svc 8). ASM_CMD_SHARED_MEM_MAP_REGIONS
  (pool 3 = 4K: whole pages only, else EBADPARAM) -> ASM_CMDRSP handle; OPEN_WRITE_V3 (PCM v2, no
  post-proc 0x10C68); MEDIA_FMT_UPDATE_V2 (24 bytes); ADM_CMD_DEVICE_OPEN_V5 (28 bytes exactly, 32 gave
  EBADPARAM; RX COPP, port 0x1000, NULL_COPP_TOPOLOGY) -> COPP 0; MATRIX_MAP_ROUTINGS_V5; RUN_V2;
  DATA_CMD_WRITE_V2. WRITE_DONE payload is {addr lsw, addr msw, handle, status}.
- LPASS Q6 SMMU (0x5100000, 13 SMRs, 14 CBs, skip-init): nothing matches SID 1 after boot and unmatched
  streams fault. An S2CR bypass reads back as type 2 (fault): the ADSP's first buffer read then reset the
  SoC silently (1008-1013; RUN without data was fine for 4.8 s, the amplifier off changed nothing,
  hyp_assign to LPASS VMID 6 returned -2 and is not what Linux does). Fix (1014): identity context bank
  CB4 (CBAR 0x1f000, SCTLR 0xe0 M=0), SMR0 SID 1 -> S2CR translate CB4, and buffer addresses carry
  SID 1 in the high word (boot-arg adsp_sid=1, as Linux msm_audio_ion). CB FSR shows no fault.
- FIRST SOUND (2026-11-24, image adsp=1 adsp_amp=1 adsp_sid=1, CI 45380606248): 34 writes of 100 ms of a
  1 kHz sine (peak 2000, about -24 dBFS) played; the laptop microphone recorded a pure 1 kHz tone (+80 dB
  over the neighbouring bins, clipping the mic) for the 3.5 s the buffers last. The TAS2557 gain is
  probably high: lower it before louder tests.
- Continuous stream (1016, 1017; image whyred/out/xnu-ios-1017.img + adsp=1, CI 45386865744):
  /dev/topaz-audio and /dev/redwood-audio (the HAL plug-in's) feed a 1 s ring mapped once; 20 ms chunks,
  4 in flight, the next on each write-done; RUN on frames, PAUSE + TAS2557 shutdown 0.5 s after the last;
  volume = sample scaling from the plug-in's ioctl; SID bits on by default; idle audio thread waits 20 ms
  (was 2 ms polling). 1017 zeroes each chunk the ADSP is done with: without it the ADSP was handed the
  ring's content of a second ago whenever the writer fell behind (tones ran long, gaps vanished).
- Test: whyred/userland/tools/pcmplay.c (raw 48 kHz 16-bit stereo, paced on the clock) with a file of
  500 / 1000 / 2000 Hz, 0.6 s each + 0.3 s silence. Laptop mic: the three tones in order, total length
  as the file, nothing replayed after the end; each run 129600 frames in, 0 dropped, 1 underrun (the start).
  Gaps not confirmed (a later recording was drowned in room noise).
- iOS audio (2026-11-24): HackiOS's HAL plug-in userland/audio/audio_plugin.c built by CI
  (.github/workflows/userland.yml, iOS SDK on macos-15-intel, arm64, ad-hoc signed) as RedwoodAudio.driver
  in /System/Library/Audio/Plug-Ins/HAL; VirtualAudio.plugin moved back from HAL.disabled to
  /Library/Audio/Plug-Ins/HAL. mediaserverd activates "Speaker" and routes [Speaker~Speaker~]; no crash.
  Its say() now also goes to os_log (stderr is lost, /dev/console sandboxed); to read it, the plug-in's
  /var/db/uuidtext entry must be in the local logarchive.
- Verified: SpringBoard plays system sound 1106 -> plug-in "playing ... stopped after 3.09 s (0 frames not
  taken)" -> kernel stream 148480 frames (= 3.09 s) in 3.15 s wall: 48 kHz in real time. A command-line
  AudioServicesPlaySystemSound never reaches mediaserverd (no audio session); whyred_ui plays an id
  written to /var/mobile/whyred_sound from inside SpringBoard for tests.
- adsp=1 is in the default boot-args now (whyred/out/xnu-ios-1017.img).
- Not yet heard by a person: a listening check with real iOS sounds (keyboard clicks, Safari video).

## Screen-off power (patches 1026, 1027, shim Silver off, 2026-11-26)

- Measure: kern.whyred_usbin_suspend=N (1026) runs the phone on its battery for N s with the cable in
  (SMB2 USBIN_CMD_IL 0x1340 bit 0, allowlisted; clears itself and at boot). /var/root/ibatt.sh N samples
  SweetBattery's InstantAmperage every 10 s (ioreg; + = discharging here). Byte reads of IBATT 0x41a2/a3
  through kern.whyred_codec op 7 tear (low and high byte from different samples): use the ioreg value.
  Readings while charging (the old "-60 mA") are the charger's net, not the phone.
- Screen off, idle, Wi-Fi unassociated (scans 2 x 5 s every 65 s): 160 mA median, steady (1026 and 1027,
  300 s each). About 25 h on 4000 mAh. iostat: CPUs 97 % idle.
- lskdd respawned about once a second (runs 9866 in 3 h, exit 92, ThrottleInterval 0): launchctl disable +
  bootout system/com.apple.lskdd (on the phone). Live-process CPU 5.3 -> 2.9 CPU-s per minute, launchd
  1.06 -> 0.10. No other job above 8 runs. bluetoothd was already disabled (2026-11-11).
- 1026: USB poll 10 ms after 2 s without events (was 1 ms), woken by output; audio idle poll 100 ms after
  setup (was 20 ms). SSH and the log stream work. No change visible in the 160 mA.
- 1027: kern.whyred_scanout stops INTF_1's timing engine. With the WLED off: 205 mA with scanout off vs
  160 on. The panel without a video stream (still in normal mode, not slept) draws more. Restart works
  (frames resume). Boot-arg sweetfb_teoff=1 does this automatically: leave it off. Next for the display:
  DCS 0x28 + 0x10 (sleep in) over DSI before stopping scanout, then DSI and MDP clocks off. The DSI
  command path is untested on whyred (sweetdsi=1): needs a person to watch the panel.
- Linux on this phone, awake (no suspend), screen blanked, cores at 1.6/1.8 GHz: 229 mA. Linux offlines
  CPU0 on this TZ: "psci: CPU0 killed", and back online.
- Shim: Silver 0 (the only Silver core on) now calls CPU_OFF 1 s after Gold 0x100 starts, when
  MIGRATE_INFO_TYPE is 2; parks in wfi if it returns. Image whyred/out/xnu-ios-1027s.img boots normally.
  Draw unchanged (160 mA median); whether CPU_OFF ran is not visible from XNU.
- The 160 mA floor is not the CPUs or the polling: the panel and MDP, bus and DDR clocks without sleep
  votes, the radios. Next steps: panel sleep over DSI, RPM sleep-set votes, system suspend.

## Panel sleep over DSI (patch 1028, 2026-11-26)

- 1028 kern.whyred_dcs (root): one DSI packet, v = data0 | data1 << 8 | type << 16 | HS << 24 | read << 25. DSI0
  (0xc994000) is mapped for commands on whyred without sweetdsi (backlight stays WLED). CI 45881392701, image
  whyred/out/xnu-ios-1028.img. The DSI command DMA path works on the first try (it had never run on whyred).
- DCS read 0x0A (get power mode, LP, max return size 1 first): the reply lands in read-back register 0x06c as
  0x21XX0032 (type 0x21, one byte XX), older replies shift to 0x070/0x074. Values: 0x1c awake (sleep out, normal,
  display on), 0x18 after 0x28, 0x08 after 0x28 + 0x10. 0x1c matches the DT ESD status value.
- Seen by the user: 0x28 blacks the picture with the backlight on, 0x29 restores it; 0x28 + 0x10 (HS, as the DT
  off-command) then 0x11, 120 ms, 0x29 (LP) brings the picture back clean.
- Draw, screen off by lock, WLED off, 10 s samples on battery (ibatt.sh): panel asleep + scanout on 119 mA avg
  (two levels, about 100-106 and 158-179), panel asleep + scanout off 133 avg (about 87-89 and 128-206), panel
  awake + scanout on 153 avg (147-165, steady). Panel sleep saves about 35 mA on average. Scanout off on a slept
  panel: inconclusive, needs longer runs. The two levels are probably Wi-Fi scans (guess).
- 1029 does it on every lock: after "WLED off after fade" the panel gets 0x28 + 0x10 (HS), before "WLED on" it gets
  0x11 + 0x29 (LP), each with a power mode read in the log ("panel sleep in: done, power mode 0x8", "sleep out ...
  0x1c"). A lock that ends while the wake waits is serialised by fPanelLock. Boot-arg sweetfb_psleep=0 turns it off.
  CI 45892798613, image whyred/out/xnu-ios-1029.img (default boot-args), now the default image. Verified: 19 locks,
  18 unlocks in one hour, slow and fast, all reads 0x08 / 0x1c, no failed command, the user saw a clean picture
  on every unlock.
- Freeze seen the same session (before the DCS tests): iOS turned the display off ("WLED off after fade: done")
  in Voice Memos, the last frame stayed lit, touch and power reached SweetHID but the display never came back on.
  kill -9 backboardd recovered. First SPMI reads of the WLED ctrl (0xd846) returned 0, then 0x80: reads can be
  stale. Cause unknown.
- Photos Hidden / Recently Deleted: coreauthd "ACMContextCreate failed: -5" (no SEP / AppleCredentialManager), so
  no passcode sheet appears. Workaround on the phone: ContentPrivacyEnabled = false in
  com.apple.mobileslideshow.plist (original /var/root/mobileslideshow.plist.orig). Write the plist and restart
  cfprefsd in one step; never SIGSTOP cfprefsd (every preference read then hangs, ssh included).

## GPU stage 1, steps 1-4 (patch 1030, 2026-11-27)

- bsd/dev/whyred_gpu.c, boot-arg whyredgpu=N (steps 1..N, started from sweet_thermal_init, 20 s delay). Plan:
  whyred/research/gpu-stage1.md. CI 45910244543, image whyred/out/xnu-ios-1030g4.img (default args + whyredgpu=4).
- As ABL leaves it: gcc gpu_cfg_ahb 0x20008001 (on), gpu_bimc_gfx 0x1, bimc_gfx 0x80000000 (off), gpu_gpll0 0x4.
  gpucc: PLL0 off (l 0x3d), PLL1 off (l 0x21), gfx3d RCG off (cbcr 0x80008620), rbbmtimer off, cxo on, rbcpr off,
  CX and GX GDSC collapsed (0x222001), GX domain clamp 0x1. Speed bin 0x5a (qfprom 0x7841a0 = 0xb580100): DT caps
  this bin at 430 MHz.
- Steps 2-4 all passed on the first boot: RPM rwcx/rwmx 384 acked, bimc_gfx on, CX GDSC 0xa0222000 (hwctrl
  0x80001fe0), gfx3d/rbbmtimer RCG at XO, GX GDSC 0xa0222000 (domain 0x3c0000), gfx3d cbcr 0x20008621.
- First GPU reads: VBIF version 0x20110000, RBBM status 0x1, status3 0, int0 0, AHB error 0, CP_HW_FAULT 0,
  SECVID_TRUST_CNTL 0x1 (secure until the zap shader), always-on counter running (211718 ticks in a 10 ms sleep).
  iOS boots normally with it.

## GPU stage 1 complete: first job on the Adreno 509 (patch 1031, 2026-11-27)

- 1031 adds steps 5-11 to whyred_gpu.c. CI 45916516909, image whyred/out/xnu-ios-1031g11.img (default args +
  whyredgpu=11). Firmware on the iOS root in /usr/share/firmware/whyred/ (a512_zap.mdt/.b00-.b02, a530_pfp.fw,
  a530_pm4.fw from the vendor image /firmware; not in git). GPU memory: 0xa0800000 + 4 MiB below the XNU base,
  mapped uncached.
- SMMU 0x5040000: sCR0 0x200626 (unmatched streams fault), ID0 0x48016603, ID1 0x20008005: 3 SMRs, 5 CBs, 4 KiB
  pages x8 (banks at +0x8000). All SMRs invalid, S2CR 0x200ff (fault), CB0/CB1 empty after boot.
  A read of CB2 stalled the bus and left a CPU core stuck (iOS kept running, but the display never woke after the
  next lock): banks 2 and up belong to TrustZone. Read only CB0-1.
- Identity bank: CB0 CBA2R 1, CBAR 0x1f000, SCTLR 0xe0 (M = 0), S2CR0 0 (translate to CB0), SMR0 0x80000000
  (stream 0). The first try stalled somewhere in these writes; with a log line and 150 ms before each write all
  twelve landed and read back. Cause of the first stall unknown.
- Zap shader: the split .mdt keeps the hash segment right behind the headers (offset 0x94), not at its p_offset
  0x1000. One LOAD segment, paddr 0x5000, 0x1334 bytes, relocatable. PAS 13 init_image / mem_setup 0xa0800000 /
  auth_and_reset all return 0.
- Microcode: a530_pfp.fw 17108 bytes (version word 0x5ff11a), a530_pm4.fw 19652 bytes (0x5ff066), loaded minus
  the first word; CP_RB_CNTL 0x20c, ring 32 KiB.
- RBBM_STATUS reads 0x1 until the RBBM/UCHE writes of a5xx_start, then 0xc0000101 / 0x800001c1 and never goes
  idle by the Linux test, though the CP consumes every packet. Probably the perf counter or hang detect setup,
  not real work (guess). The driver accepts a consumed ring.
- CP_ME_INIT and CP_SET_SECURE_MODE 0 consumed; SECVID_TRUST_CNTL 0x1 -> 0 (out of secure mode). Job test: an IB
  with CP_MEM_WRITE wrote 0xc0ffee09, CACHE_FLUSH_TS fence 1. 100 more fences in 1980 us (about 20 us each, GPU
  at 19.2 MHz XO).
- 0xa0800000 (the first GPU memory) lay inside TopazModem's pool 0xa0400000 + 16 MiB (rmtfs, CE rings, WMI,
  ADSP buffers). 1032 moves GPU memory to 0xa1400000 (12 MiB free up to the XNU base 0xa2000000).
- 1032 step 12: gcc_gpu_gpll0 (0x15200c bit 4) on, gfx3d RCG CFG 0x503 (GPLL0 / 2 = 300 MHz), update ok. Fence
  round trip 21 us at XO -> 10 us at 300 MHz (100 in 1026 us, 1000 in 10987 us, all in order). CI 45920839113,
  image whyred/out/xnu-ios-1032g12.img (default args + whyredgpu=12).
- Stage 2 needs a GPU pool far larger than 12 MiB (topaz: 1 GiB): RAM has to come from XNU (raise
  HIGH_PHYS_BASE in whyred/tools/mkadt_whyred.py). The RedwoodGPU service needs no a6xx changes; a new a5xx
  backend replaces TopazAdreno.cpp's exports (redwood_gpu_*, topaz_gpu_pool_*, topaz_adreno_init).
- Next: GPU clock from the PLL (266 MHz, corner within the CX 384 vote), then stage 2 (RedwoodGPU service for
  Mesa, a5xx GET_PARAM values, freedreno a5xx).

## GPU stage 2a: RedwoodGPU service on the A509 (patch 1033, 2026-11-27)

- iokit/Kernel/arm/WhyredAdreno.cpp replaces TopazAdreno.cpp/TopazAdrenoFw.cpp in iokit/conf/files.arm64 and gives
  RedwoodGPU.cpp (unchanged) the same exports: 256 MiB pool at 0xa2000000 (64 MiB chunks mapped write-combined on
  first use, peak use logged), identity addresses, A509 GET_PARAM values (GPU_ID 509, CHIP_ID 0x05000900, GMEM
  0x44000 at 0x100000, MAX_FREQ 300 MHz, TIMESTAMP = RBBM always-on counter), submit/fence through whyred_gpu.c.
- whyred/tools/mkadt_whyred.py: XNU RAM now 0xb2000000 + 0xcc000000 (was 0xa2000000 + 0xdc000000) for the pool.
  WHYRED_GPU_POOL=0 builds the old layout. iOS boots normally with it.
- Boot-arg whyredgpusvc=1 publishes RedwoodGPU after step 12 without topaz's gpu=1 display changes (gpu=1 also
  publishes it). Either one makes whyredgpu default to 12. CI 45927260587, image whyred/out/xnu-ios-1033svc.img.
- whyred/userland/tools/gputest.c (built by whyred/userland/build.sh, on the phone as /var/root/gputest): the msm
  calls without Mesa. Result: parameters as above, GEM_NEW 64 KiB at 0xa2000000, mapped in the process, a
  CP_MEM_WRITE command buffer through SUBMIT + WAIT_FENCE wrote 0x5a5a1234; 200 more submissions, 0 bad, about
  58 us per submit + wait round trip.
- Next (stage 2b): Mesa freedreno a5xx for iOS (userland/mesa/mk-mesa-mac.sh, needs a macOS CI job) and the
  eagl layer (userland/gpu/eagl.c needs HackiOS mkeagl.py + eagl_slots.txt), then CoreAnimation with gpu=1.

## GPU stage 2b: Mesa draws on the A509 (2026-11-27)

- .github/workflows/userland.yml job gpu (macos-15-intel, first try): Mesa 25.0.7 (userland/mesa/mk-mesa-mac.sh,
  freedreno + softpipe, freedreno-darwin and osmesa-es-profile patches, redwood-drm), libtopaz-gl.dylib (eagl.c +
  gl_osmesa.c, table from HackiOS mkeagl.py + eagl_slots.txt), libtopaz-inject.dylib, gltest. Run 45928949907,
  artifact topaz-gl (whyred/out/topaz-gl-45928949907). On the phone: /topaz/lib/{libOSMesa.8,libtopaz-gl,
  libtopaz-inject}.dylib, /var/root/gltest. Mesa's libraries are not in git.
- gltest on xnu-ios-1033svc (whyredgpusvc=1): GL_RENDERER FD509, GL_VERSION OpenGL ES 3.1 Mesa 25.0.7, one
  triangle at 256x256: centre ff0080ff (orange), corner ffff0000 (blue), no GL error; 100 frames of clear +
  triangle + glFinish in 200669 us (about 2 ms each). Mesa prints a harmless os_same_file_description warning.
- Next: CoreAnimation on the GPU (gpu=1, libtopaz-inject.dylib in backboardd, as topaz). Needs a person to look
  at the screen.

## GPU stage 2c: CoreAnimation on the A509 (patches 1034-1038, 2026-11-27)

- 1034 (CI 45929575858): gpu=1 starts the GPU at once. Its step 2 RPM vote failed: the modem driver had not
  opened the RPM channel yet. 1035 (CI 45969705419) waits for it (whyred_rpm_ready, 250 ms in practice).
- whyred_ui.dylib (on the phone) loads /topaz/lib/libtopaz-inject.dylib in backboardd when boot-args hold
  gpu=1. CoreAnimation gets an OpenGL ES 3.1 context on FD509. First boot (1035): the UI showed, the user saw
  it slower than the CPU and flickering.
- Cause: CoreAnimation draws into two screen surfaces in turn. The default one (kernel surface 1, the panel
  memory 0x9d400000) has 4320-byte rows, no a5xx render pitch (64-byte units), so eagl drew elsewhere and read
  each frame back (128 ms). And present skipped surface 1 as already in place while the MDP scanned the other
  one: every second frame stale (flicker).
- 1036 (CI 45975327903): with gpu=1 GetLayerDefaultSurface hands out a GPU pool surface (4352-byte rows, 64
  guard rows). Both surfaces are drawn in place and scanned directly. Frames 1-4 ms of GPU work. The user:
  "waay better".
- 1037 (CI 45976951241): kern.whyred_key = key | ms << 8 (0 power, 1 vol up, 2 vol down, 3 home). Verified:
  an injected power press turned the display off.
- 1037 reset the phone after some minutes (boots Linux). Last log line: RedwoodGPU shared surface 196 at
  0x171714000. The GPU ran in 32-bit address mode (no ADDR_MODE_CNTL writes), the write went to 0x71714000.
- 1038 (CI 45980191062, whyred/out/xnu-ios-1038gpu.img = default args + gpu=1): Linux a5xx_enable_64bit writes
  (12 registers), then a GPU write test into a page above 4 GiB. RedwoodGPU shares surfaces only up to
  whyred_gpu_reach(): 4 GiB, 64 GiB once the test passed (boot-arg whyredgpu64=0 skips the mode). First boot:
  "64-bit addressing on", but 32 one-page allocations found no page above 4 GiB, so the GPU stays below 4 GiB.
  Surfaces above it fall back to the CPU copy (slow frames, no crash). The test needs a better page source.
- 1039 (CI 45989179045, xnu-ios-1039gpu60.img = gpu=1 sweetfb_hz=60): high test page from a 16 MiB buffer.
  Still "no page above 4 GiB" (the allocator hands out low pages first): GPU stays below 4 GiB. Profile
  before: 7.9 ms a frame of GPU work on average, 3623 surface textures copied by the CPU against 77 shared.
- 256 MiB pool filled within minutes (Mail, folders): "pool full", then Mesa crashed backboardd (signal 11 in
  libOSMesa) and the UI restarted. Folders drawn meanwhile had no backdrop at all.
- 1040 (CI 45991487122, xnu-ios-1040gpu60.img): pool from 0xa2000000 to the XNU base (boot_args physBase),
  mkadt_whyred.py WHYRED_GPU_POOL=MiB, default 1024 (XNU base 0xe2000000). Use logged every 30 s. Peak 236 MiB,
  back to 9-19 MiB after a backboardd restart: no leak seen.
- Green/yellow folder icons (also App Library folders, wiggle-mode minus buttons): the second Utilities folder
  icon on home page 2 flips between a right grey gradient and solid green or yellow from frame to frame. The
  colours match nearby icons (Messages green, Tips yellow): likely a texture that is sampled but not drawn into,
  showing old pool memory. Not the cause (tested with /var/root/glargs and a backboardd restart): glgroup=0,
  glplain=0, glfetch=0 (closing platter looks more natural), FD_MESA_DEBUG=sysmem. eagl now hands luminance
  textures to Mesa as RGBA (glla=0 turns it off): did not cure it. A gltrace attempt traced nothing; at that
  point the wallpaper had gone black (maybe dark mode at 4 AM phone time) and the icon stayed grey.
- eagl reads /var/root/glargs before the boot-args (first match wins). Test script: scratchpad foldertest.sh.
- Remote use: whyred/tools/remote/remote.py --bind <tailscale IP> serves port 8765: webcam stream (laptop
  /dev/video0, phone propped in front, landscape, top to the left), taps and swipes (kern.whyred_touch) on a
  calibrated camera view or a touch pad, buttons (kern.whyred_key), kernel log tail.

## GPU stage 2d: GPU page tables (patch 1041, 2026-11-27)

- 1041 (CI 46000196958, commit 856c709, boot-arg whyredgpuvm=1): SMMU context bank 0 stage 1 translation.
  Table from the pool: 0-4 GiB maps 1:1 in 1 GiB blocks, 4-8 GiB is a window of 4 KiB pages with a guard page
  per buffer. A self-test maps a kernel page and has the GPU write to it, else back to 1:1.
- xnu-ios-1041vm.img (gpu=1 whyredgpuvm=1, 60 Hz): self-test passed ("translation works"), UI ran. Slower than
  1040: RedwoodGPU buffers are cached, each submission cleans about 7 MB out of the cache (about 3 ms).
  glFlush 12 ms a frame, frames 17.8 ms on average. Surfaces: 31 shared against 346 copied, every copy for
  "not BGRA" (A008 text masks, LA08 96x96, RGhA 256x1 half float, a 600x600 RGBA surface in client memory).
- xnu-ios-1041wc.img (same + gpu_cached=0, write-combined RedwoodGPU buffers): hung during boot about the
  time backboardd started (last line "MiniIONetworkStack: opened by configd"), white console text on screen,
  gone from USB. A long power press brought back Linux. Topaz notes WC IOBMD allocations are contiguous and
  come from memory the kernel cannot map back. Do not use gpu_cached=0.
- Phone back on xnu-ios-1040gpu60.img afterwards (first fastboot boot after the hang reset into Linux within
  minutes with no log, the second one ran).
- Next: GEM buffers from the write-combined pool, mapped through the page table (no cache cleaning). Needs
  RedwoodGPU release, reap, dropKept and keep to free pool memory. Then share A8, LA and RGBA surfaces too.

## Not yet known

- Whether ABL accepts an uncompressed Image in a v0 boot image or needs gzip.

## Haptics and repeated sounds (patches 1018, 1019, 2026-11-24)

- With the plug-in's Actuator device present, mediaserverd's haptics server starts ("Initializing Haptic 2.0
  Engine", HapticSynth 2-chan 48 kHz, SynthOutput IOFrameSize 512). Before audio it bailed out.
- MobileGestalt DeviceSupportsHaptics (obfuscated cBy4BcYs5YWtFHbBpt4C6A) was 0. It is read by CoreHaptics,
  UIKitCore, ToneLibrary, libAudioToolboxUtility. Set to true in CacheExtra of
  /private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist
  (owner mobile:nobody; original kept on the phone as /var/root/MobileGestalt.plist.pre-haptic). Takes effect
  after a reboot. Phone-side change only: a fresh root needs it again.
- System-sound vibes (SSServerImp) still fail: "Could not start vibe ... error -16490, vibrator is not
  initialized". They use libVibeSynthEngine's Legacy Vibe Engine, which wants per-product calibration
  (kVirtualAudioPortPropertyCalibrationData, vibeengineconfig). Not on this root. whyred_ui's system-sound
  interposer still covers them.
- 1018: /dev/redwood-haptic (bsd/dev/whyred_spmi.c, created by TopazAudio's thread at APR ready). The plug-in's
  Actuator writes (mono 16-bit) whose peak reaches 1000/32768 run the ERM motor. 1018 used 70 ms at 3000 mV per
  tap: the user said too intense for icon long-presses.
- 1019: tap = write length (min 20 ms) + 15 ms, VMAX 1200-3000 mV by the peak. User: "much better".
- 1019 also fixes silence after the first sound: PAUSE leaves up to 4 buffers with the ADSP and they return first
  after RUN. 1016/1017 reset the write-done count at each start, so it blanked each new chunk before it played
  (the kernel got every frame, the speaker played zeros). Now counters run across pauses and each write-done
  blanks the chunk at its own address (payload word 0).
- Verified by the user on whyred/out/xnu-ios-1019.img (CI 45462569242, default boot-args): first iOS sound
  heard (power connected, "clean and normal"), ringtone previews and repeated charger sounds play, long-press
  haptics felt.
- Settings shows "Sounds & Haptics" (PreferencesUI SOUNDS_AND_HAPTICS) behind MobileGestalt key
  gqDnklGQnpv5ilgh5uHckw (plain name unknown), 0 here. Set to true in the same CacheExtra (2026-11-24).
- Volume buttons with nothing playing change the ringer volume (SpringBoard "activeVolumeCategoryName:
  Ringtone", Ringer HUD with a bell); the log shows setRingerSilent: false every time. Before audio worked
  there were no categories and only the plain volume bar appeared.

## Flashlight (patch 1020, whyred_ui, 2026-11-25)

- PM660L flash LED: USID 3, base 0xd300 (live DT qcom,leds@d300), PPID 0x3d3 = APID 49, apps-owned.
  Android's "flashlight" node lights torch_0 + torch_1 (switch_0, mask 0x3) at 100 mA each.
- Found after ABL: safety tmr 0x93 0x93, current 0, mod 0, ires 0, strobe 0x1 0x1, en 0, hdrm 0x84 0x84.
- Patch 1020 kern.whyred_torch 0..100 (25..150 mA per LED, 12.5 mA steps): leds-qpnp-flash-v2 switch_set
  order (TGR_CURRENT 0x43+id, SAFETY_TMR 0x13 = torch, MOD_CTRL 0x46 bit 7, EN_LED_CTRL 0x4c bits 0-1).
  Verified lit, status 0 (one read of 0x1 at a level change).
- The LED keeps the current it had at switch-on: changing TGR_CURRENT while lit does nothing visible.
  A new level goes off then on (whyred_ui does it; no visible flicker).
- iOS side (SpringBoard, whyred_ui.dylib): Control Center's module needs MobileGestalt camera-flash
  (key jdLgKT+0BZmGrzd9J0cuPA set true in CacheExtra; backup /var/root/MobileGestalt.plist.pre-torch).
  AVFlashlight is replaced: init = NSObject init (the real one returns nil with no FigFlashlight),
  hasFlashlight/isAvailable YES, setFlashlightLevel:withError: -> sysctl with KVO "flashlightLevel".
  AVFCapture is dlopened from a thread at start; hooking from a dyld add-image callback made
  +[SpringBoardUI load] abort ("This process should not be linking or loading SpringBoardUI").
- Control Center levels 0.25/0.5/0.85/1 map to 25/50/112/150 mA per LED (level squared).

## Microphone (patches 1021-1023, 2026-11-25)

- Path: PM660L analog codec (SPMI SID 3, 0xf000-0xf1ff) ADC1 -> PDM over LPI TLMM GPIO 18-21, 23, 25 (func1, 8 mA,
  output-high except GPIO 20) -> LPASS digital codec (0x152c0000) DEC1 -> INT3 MI2S TX (AFE port 0x1035, SD0 mono,
  16 bit, 48 kHz) -> ADM TX COPP (mode 2, matrix 1) -> ASM read session (OPEN_READ_V3).
- DT int-mi2s3 tx-lines = 3 (QUAD01). Linux picks SD0 for 1-2 channels (vi_feed_mono defaults to SPKR_1).
- Clocks through AFE CLOCK_SET: INT3 IBIT 0x112 at 1.536 MHz, INT_MCLK_0 0x305 at 9.6 MHz (codec core).
- Register sequences come from Linux tracefs (events regmap/regmap_reg_write and spmi/spmi_write_begin, Linux has
  no debugfs). Linux arecord records real signal (peak 354 in a quiet room).
- pm660l_l7 (RPM ldob 7, 3.088 V) is the mic bias supply and also vdda33 of both QUSB2 PHYs. Voting it off drops
  USB for the rest of the boot (1021, 1022). 1023 leaves it on.
- 1022 result: every ADSP command returns 0 and data flows in real time (first data after 380 ms). Digital and
  analog register readback matches the Linux trace. The samples are all zero.
- 1024: buffers filled with 0x5a5a before each READ come back overwritten with zeros (the ADSP writes, no SMMU
  fault). With the DEC1 HPF bypassed (0x28c = 0x08) the output is a flat 32678 that no pad, mux or PDM change
  moves: the analog codec sent nothing.
- Cause: PMIC_ANALOG_MASTER_BIAS_CTL (0xf146) read 0 on iOS and 0x30 on Linux. Linux writes it at MBHC init,
  before any trace. Setting it live gave noise (rms 4.5) and a laptop 1 kHz tone at amplitude 109. 1025 writes it
  at mic start. Verified on 1025 (CI 45733899688, whyred/out/xnu-ios-1025.img): tone at 108, USB stays up.
- Voice Memos on 1025 (2026-11-26): records and plays back, the user hears it loud enough. The m4a (AAC 48 kHz
  mono) has peaks near 31000 and rms 5000-8000 for speech, so no extra gain is needed. One first try, after
  7 h of uptime, played silence and froze the app until a reboot; not seen again on a fresh boot. Cause unknown.
- whyred/tools/cdcdump: read-only Linux module (build against src/kernel-lineage-21 with build/kernel-out in the
  whyred-kernel-build image, cp -a the out dir) that dumps the digital codec, LPI pads and analog codec (regmap
  of the anlg-cdc platform device) while arecord runs. Other analog differences left: f147, f150-f159 (MBHC),
  f058 RX_CTL1.
- ADSP COPP ids come back as 0xb08f0009: mask with 0xffff.
- 1023 root watchdog: no rootvnode 120 s after boot -> PSCI reset (boots Linux).
