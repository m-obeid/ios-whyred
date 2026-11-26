# ios-whyred

**Apple's iOS 16 running on a Redmi Note 5 Pro.**

This is a port of Apple's open-source XNU kernel (xnu-8796) and the **unmodified iOS 16.7.16 (20H392) userland of the iPhone X** to the Xiaomi **Redmi Note 5 Pro** (`whyred`, Qualcomm **SDM636** / Snapdragon 636). launchd, the system daemons, backboardd, SpringBoard and the stock apps run on the phone.

The tree descends from two earlier ports:

- [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet): XNU and the iPhone X userland on the Redmi Note 10 Pro (SM7150). Patches 0001-0079.
- ios-topaz, the `topaz` branch of this repository: the same on the Redmi Note 12 4G (SM6225). Patches 0080-0152 and 0900-0950, with GPU, audio, Wi-Fi glue and sandbox work from [HackiOS](https://github.com/oosexgang/hackios-xnu-kernel) by OOSEX GANG. See [CREDITS.md](CREDITS.md).

The whyred work starts at patch 0960 and lives in `whyred/`.

> Experimental bring-up, not a usable phone OS. Many parts are missing or stubbed, and Apple's security model is mostly off. Nothing is flashed to the boot partitions: the kernel starts from RAM with `fastboot boot`, and a normal reboot returns to the Linux installed on the phone.

## What works (2026-11-26)

| Area | State |
| --- | --- |
| Boot | Stock ABL, `fastboot boot` of a v0 boot image (gzip Image + whyred DTB). The shim starts on a Silver core, wakes Gold `0x100` with PSCI `CPU_ON`, then powers the Silver core off. XNU runs only on Gold |
| CPU | The four Kryo 260 Gold cores (Cortex-A73 class) at up to 1804.8 MHz: the shim brings up OSM and the CPRH closed loop (792 mV at the top row). Kernel load governor (rows 1113.6 to 1804.8 MHz) and TSENS thermal governor |
| Memory | `0xa2000000` up, about 3.4 GiB, above every fixed carve-out |
| Storage | Own eMMC (SDHCI ADMA2) driver. The iOS root is a 16 GiB HFS+ image stored as a file inside Linux's ext4 `userdata`. XNU reads and writes it through its recorded extents, so Linux keeps working |
| Display | MDP5 scanout of iOS surfaces at the native 1080x2160 (360x720 pt). CPU rendering, no GPU yet. PM660L WLED backlight, brightness sliders in Settings and Control Center, auto-lock |
| Touch and keys | Synaptics RMI4 touch on BLSP1 QUP1, power and volume keys over SPMI |
| USB | CDC-ECM Ethernet gadget, about 11.5 MB/s. SSH into iOS (dropbear) |
| Wi-Fi | Modem booted by our kernel from the stock `modem` partition (MBA path), RPM-SMD rail votes, WCN3990 WLFW, WPA2 join, auto-join after a reboot, Safari over HTTPS |
| Battery | PM660 fuel gauge: level, voltage, current, temperature, charging state in the status bar |
| Clock | PM660 RTC plus a per-phone offset boot-arg, set before launchd starts |
| Audio | ADSP booted through TrustZone, APR/AFE/ASM/ADM stream, TI TAS2557 loudspeaker. System sounds and ringtone previews |
| Microphone | PM660L analog codec into the LPASS digital codec. Voice Memos records and plays back |
| Haptics | PM660 ERM motor: system-sound vibrations and UI haptics |
| Flashlight | PM660L flash LED from Control Center, four levels |
| Status bar | iPhone 8 style bar through `whyred_ui.dylib` (the 360x720 pt screen matches no iPhone) |
| Power | Screen off and idle: about 160 mA, about 25 h on the 4000 mAh battery. No suspend yet |

## What does not work (yet)

- **GPU**: the Adreno 509 sits idle. The topaz Adreno 610 path (Mesa freedreno behind CoreAnimation's GL ES renderer) needs an A5xx port.
- **No Bluetooth, cellular, camera, sensors (light, proximity, motion), GPS, NFC, fingerprint, SEP.** The modem runs for Wi-Fi only.
- **Earpiece and headset jack**: untested.
- **Sleep**: no panel sleep over DSI, no RPM sleep votes, no system suspend.
- **Security is off**: permissive AMFI and sandbox, stub AppleKeyStore, no data protection.
- Device identity stays iPhone X (`iPhone10,3`).

## How whyred differs from topaz

| Problem | Fix |
| --- | --- |
| ABL wants a boot image v0 with a gzip kernel and an appended DTB, not v4 | `whyred/shim/build.sh` packs the shim like the stock whyred `boot.img`. The XNU payload is the ramdisk |
| Silver cores have no 16K translation granule (same as topaz) | The shim hands off to Gold `0x100`. Never `CPU_OFF` the last running Gold core: the cluster powers down |
| ABL leaves Gold at 600 MHz (GPLL0), OSM and CPRH off | `whyred/shim/osm.c` ports the Linux OSM and CPRH init (with SCM writes into the TZ sequencer) |
| eMMC instead of UFS. PIO on this SDHCI host is broken | ADMA2-only driver (patch 0961). Card RCA is 2 |
| Modem boots through MBA (self-authenticating), not the TrustZone PAS path | Patch 0975 ports the Linux `pil-msa` sequence. Diag, memshare, recursive tftp and the cnss-daemon BDF steps keep the WLAN PD alive |
| PMIC rails belong to the RPM, direct SPMI writes fail | GLINK client over RPM message RAM (patch 0978) for LDO and clock votes |
| Backlight is a PM660L WLED, not DSI DCS | Allowlisted SPMI writes (patch 0970) |
| ADSP DMA faults the LPASS SMMU and resets the SoC | Identity context bank for SID 1 (patch 1014) |

## How it boots

```
ABL (stock, unlocked)
  -> fastboot boot xnu-ios.img            (boot image v0, RAM only)
     -> shim on Silver CPU0: wakes Gold 0x100, then CPU_OFF
        -> shim on Gold: GIC setup, OSM + CPRH to 1804.8 MHz, finds the payload
           -> XNU Mach-O + Apple Device Tree (ADT) + boot-args
              -> eMMC driver -> HFS+ root image inside userdata
                 -> dyld + shared cache -> launchd -> backboardd + SpringBoard
```

There is no serial console. The kernel log streams over USB to UDP port 5140 on the host: `python3 whyred/tools/udplog.py FILE --secs N`.

## How to do it yourself

You need a Redmi Note 5 Pro (`whyred`, 4 GB) with an **unlocked bootloader** and a Linux install on the phone with SSH as root (this one runs Void Linux on Halium). On the PC: `fastboot`, `aarch64-linux-gnu-gcc`, AOSP `mkbootimg`, clang and `ld64.lld`, `apfs-fuse`, `hfsprogs`, sudo, and a GitHub account for the kernel build. Set `PHONE=root@ADDRESS` for the phone's Linux SSH.

If fastboot uploads fail on your xHCI ports, turn off LPM for the device and replug:

```sh
echo "18d1:d00d:k" | sudo tee /sys/module/usbcore/parameters/quirks
```

### 1. Build the kernel

The kernel builds on GitHub's macOS runners with [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build). Fork the repo, then on the `whyred` branch:

```sh
gh workflow run xnu.yml -R YOU/ios-whyred --ref whyred -f runner=macos-15-intel
whyred/mkboot.sh --fetch RUN_ID          # -> whyred/out/kernel-RUN_ID/
```

The arm64 runners often fail with "not acquired by Runner", so use the Intel runner. A build takes 12 to 20 minutes.

### 2. Build the iOS root image

Download the iPhone X (iPhone10,3) iOS 16.7.16 (20H392) IPSW from Apple and put the OS, SystemOS and AppOS disk images in `whyred/ipsw/` (names in `whyred/tools/build-iosroot.sh`).

```sh
whyred/tools/build-iosroot.sh            # -> whyred/ipsw/ios16-root.img, 16 GiB sparse
whyred/tools/fetch-ssh-debs.py
whyred/tools/add-ssh.sh                  # dropbear, root login with your ~/.ssh/id_ed25519.pub
whyred/tools/add-clock.sh                # launchd job that keeps the RTC offset fresh
```

### 3. Copy the root to the phone

On the phone's Linux, reserve the file once: `fallocate -l 16G /userdata/iosroot.img`. Then from the PC:

```sh
whyred/tools/push-root.sh                # copy, record extents in whyred/out/root-extents.json, check raw sectors
```

### 4. Build the boot image and boot

```sh
B="debug=0xc serial=3 cpus=4 keepsyms=1 -noprogress rd=emmcroot0 emmcroot=userdata emmcrw=1 rootrw=1 \
unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1 \
sweetcpufreq=0 sweetbattery=2 panicreset=60 sweetusb=2 sweetfb_native=1 sweetkeys=1 sweetsyna=2 \
sweetfb_noconsole=1 sweetfb_hz=30 topaz_inject=/usr/lib/whyred_ui.dylib,*app,backboardd \
modem=1 wifi=1 wifi_smmu=2 adsp=1 whyred_rtcoff=OFFSET"
WHYRED_NATIVE=1 WHYRED_EXTENTS=whyred/out/root-extents.json CMDLINE="$B" \
    whyred/mkboot.sh whyred/out/kernel-RUN_ID/kernel.development.sweet none
whyred/tools/to-fastboot.sh 240          # Linux -> fastboot, no buttons
fastboot boot whyred/out/xnu-boot.img
```

`OFFSET` is real time minus the PM660 RTC count, in seconds (`kern.whyred_rtc` reads the RTC). The USB network comes up about 40 s after `fastboot boot`. Give the host side 192.168.7.1/24, then `ssh root@192.168.7.2`. The first boot of a fresh root spends about 17 minutes on the Apple logo while LaunchServices builds its database.

### 5. Changes on the phone after the first boot

These live on the iOS root, not in the image builder. Each one fixes a respawn loop or turns on a feature:

- `whyred/userland/build.sh`, then copy `out/whyred_ui.dylib` to `/usr/lib/whyred_ui.dylib` (status bar, keyboard size, brightness, haptics, flashlight, telephony restart loop).
- The RedwoodAudio HAL plug-in from the `userland.yml` CI run into `/System/Library/Audio/Plug-Ins/HAL`.
- `launchctl disable` and `bootout` of `user/501/com.apple.fairplayd.H2`, `user/501/com.apple.bluetoothd`, `system/com.apple.lskdd`, and vmd, wifip2pd and nanotimekitcompaniond.
- Move `AegirPoster.appex` (Metal wallpaper) and `BTAudioHALPlugin.driver` out of the system folders.
- MobileGestalt `CacheExtra` keys set to true: DeviceSupportsHaptics, camera flash, Sounds & Haptics. Details in the facts file.

### Back to Linux

A normal reboot (`ssh root@192.168.7.2 /sbin/reboot`) lands in the phone's Linux. Panics reboot after 60 s (`panicreset=60`), and a missing root reboots after 120 s. Hold power + volume down for fastboot. Keep presses on the keys under 4 s: the PMIC resets the phone on longer holds.

## Repository layout

| Path | What |
| --- | --- |
| `whyred/shim/` | Bare-metal boot shim: Gold hand-off, GIC, OSM/CPRH, payload loader, eMMC/FAT/modem probes |
| `whyred/mkboot.sh` | Kernel + ADT + boot-args + shim into a fastboot image |
| `whyred/tools/` | ADT generator, root image build and push, fastboot switch, UDP log reader, read-only Linux probe modules |
| `whyred/userland/` | `whyred_ui.dylib`, launchd clock job, test tools (vibe, pcmplay, micrec), built without an iOS SDK |
| `whyred/research/recon/whyred-facts.md` | Hardware facts and the bring-up log, with every CI run and result |
| `xnu/patches/` | XNU patch series, applied in order by CI: 0001-0079 sweet, 0080-0152 topaz, 0900-0950 HackiOS-based, 0960+ whyred |
| `.github/workflows/` | Kernel (`xnu.yml`) and user space (`userland.yml`) builds |
| `shim/`, `tools/`, `userland/` | topaz shim, shared tools (`mkpayload.py`, `mkiosroot.sh`, `sweet-root-tweaks.py`), HackiOS-based user space |
| `NOTES.md`, `RECOVERY.md` | sweet's engineering log and the topaz recovery runbook (history) |

Not in git: partition backups, live device trees and dumps, Apple firmware and root images, vendor firmware, build trees.

## whyred kernel patches

| # | What |
| --- | --- |
| 0960-0963 | Early console on the splash buffer, eMMC root, MDP5 display, USB constants and cache ops |
| 0964-0966 | Keys over SPMI v3, Synaptics touch, USB reset recovery |
| 0967-0969, 0999 | TSENS thermal governor, LSE counters, CPU load governor |
| 0970-0971, 0979 | PM660L WLED backlight |
| 0972 | Batched USB rings (12.8x faster) |
| 0973-0974, 0983 | PM660 fuel gauge and charger |
| 0975-0978 | Modem MBA boot, WLAN SMMU, RPM-SMD client, WCN3990 rails |
| 0980-0995 | Debounce, touch injection, Apple80211 answers, sandbox fixes, Wi-Fi join fixes (channels, HT/WMM, regdomain, sequence numbers, RSSI bounds) |
| 0996-0998 | PM660 RTC and clock set at boot |
| 1000 | Vibration motor |
| 1001-1017 | ADSP, AFE, TAS2557 speaker, LPASS SMMU, continuous stream |
| 1018-1019 | Haptics device, stream resume fix |
| 1020 | Flashlight |
| 1021-1025 | Microphone |
| 1026-1027 | Power measurement switches (battery-only with the cable in, scanout off) |

## Key hardware facts (whyred)

- SoC SDM636: 4x Kryo 260 Silver (MPIDR `0x0`-`0x3`, no 16K granule) + 4x Kryo 260 Gold (`0x100`-`0x103`, 16K granule). ARMv8.0, no LSE.
- GICv3 at `0x17a00000`, redistributors `0x17b00000`, stride `0x20000`. Timer 19.2 MHz.
- DDR `0x80000000` + 2 GiB and `0x1_00000000` + `0x7e4c0000`. Fixed carve-outs end below `0xa0400000`.
- Panel: Tianma TD4310, 1080x2160, DSI video mode, splash at `0x9d400000` (xRGB8888, stride 4320).
- PMICs PM660 (SID 0/1) and PM660L (SID 2/3) on SPMI v3.
- More in [whyred/research/recon/whyred-facts.md](whyred/research/recon/whyred-facts.md).

## Credits

Based on [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet) and the topaz port, with GPU, audio, Wi-Fi glue and sandbox work from [HackiOS](https://github.com/oosexgang/hackios-xnu-kernel) (details and licences in [CREDITS.md](CREDITS.md)). Register sequences come from the LineageOS whyred Linux kernel sources. The status bar fix follows an idea from danstronics/ios-s9plus-exynos. Built on Apple's open-source releases (xnu, IOHIDFamily, libpthread, hfs), [BearSSL](https://bearssl.org), [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build) and [ipsw](https://github.com/blacktop/ipsw). iOS itself is Apple's and is not distributed here.
