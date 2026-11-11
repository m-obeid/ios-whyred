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
  -> NULL function. whyred_ui forces kCIContextUseSoftwareRenderer in -[CIContext initWithOptions:].
- whyred_ui now loads everywhere: `kern.exec_inject="/usr/lib/whyred_ui.dylib *app"` (0947: every
  program with ".app/" in its path; boot-arg topaz_inject=/usr/lib/whyred_ui.dylib,*app). Spotlight
  is its own process (/Applications/Spotlight.app), which is why 123 failed there.
- AegirPoster (Astronomy wallpaper) crash-loops in NUNIAegirRenderer (Metal): no GPU. Unrelated.
- Wi-Fi icon: SBWiFiManager (bars from wifid SCALED_RSSI, dBm from RSSI.RSSI_CTL_AGR) reports
  bars 3, rssi -66, associated, primary interface (whyred_ui probe in SpringBoard, 2026-11-11).

## Not yet known

- Whether ABL accepts an uncompressed Image in a v0 boot image or needs gzip.
