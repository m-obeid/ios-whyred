# GPU stage 1: Adreno 509 up to one finished job (plan, 2026-11-26)

Goal: the GPU powered, its firmware running, and one command buffer executed with the result checked in memory.
Nothing on the screen changes. Everything is checkable from the kernel log.

## Sources

- Linux KGSL on this phone: `~/Projects/whyred-linux/src/kernel-lineage-21/drivers/gpu/msm/` (adreno.c, adreno_a5xx.c,
  kgsl_iommu.c, adreno_ringbuffer.c), clocks in `drivers/clk/qcom/gpucc-sdm660.c`, GDSC in `gdsc-regulator.c`.
- Live DT `recon/live.dts`: kgsl-3d0 6964-7110, GDSCs 6844-6880, gfx_corner 6529-6580, kgsl-iommu 7469 and 7883,
  zap 6883.
- topaz driver (a6xx): patches 0901-0947, `TopazAdreno.cpp` (hardware) and `RedwoodGPU.cpp` (msm DRM emulation for
  Mesa, chip independent).
- N1709/ios-whyred `drivers/gpu/whyred_gpu.c` and `hw.md`: a never-booted whyred ladder for steps 1-4 below,
  register cross-check only.
- HackiOS `drivers/gpu/hakios.adreno.c`: a6xx with GMU, the staged `adreno=N` method.

## Hardware facts

| Item | Value |
| --- | --- |
| GPU | Adreno 509, chip ID 0x05000900, registers 0x5000000 (256 KiB), IRQ SPI 300 |
| GMEM | 256 KiB + 16 KiB, base 1 MiB |
| Clocks | gpucc 0x5065000: gfx3d RCG 0x1070, gfx3d branch 0x1098, rbbmtimer 0x10d0, rbcpr 0x1054, GPU PLL0/1 (l 0x29). GCC: gpu_cfg_ahb, gpu_bimc_gfx, bimc_gfx |
| GDSC | CX 0x5066004 (hardware control 0x5066008). GX 0x5066094, clamp/GMEM reset 0x5065130, block reset 0x5066090 |
| Rail | gfx_corner: CPR4 LDO controller 0x5061000, LDO 0x506e000, mem-acc 0x1fcf004. Closed loop and LDO mode off for every corner. Each corner also votes RPM `rwcx` 0 (0x40, 0x40, 0x80, 0xc0, 0x100, 0x140, 0x180) |
| Corners | 160 MHz 1, 266 2, 370 3, 465 4, 588 5, 647 6, 700-750 7. KGSL starts at 266 MHz |
| Speed bin | QFPROM 0x780000 + 0x41a0, bits 28:21 |
| SMMU | 0x5040000 (64 KiB), qcom,skip-init. Context banks: user SID 0, secure SID 2. Linux always translates (per-process tables, no bypass) |
| Zap | a512_zap.mdt/.b00-.b02 (vendor /firmware), TrustZone PAS id 13. A509 has CPZ retention: no resume call |
| Microcode | a530_pfp.fw, a530_pm4.fw (vendor /firmware). Copied minus the first 4 bytes into GPU memory, then CP_PFP_INSTR_BASE 0x835/6 and CP_PM4_INSTR_BASE 0x838/9 |
| No GPMU | The A509 has no GPMU, LM or SPTP collapse (simpler than topaz's and HackiOS's chips) |

## Decisions

- **Firmware from the iOS root**, not in git: `/usr/share/firmware/whyred/` on the root image, read after the root
  mounts (topaz's `topaz_fw_read`). Vendor files stay out of the repository.
- **SMMU identity context bank** for SID 0 (CBAR S1 bypass, SCTLR M=0), as for WLAN (0976) and LPASS (1014), so GPU
  address = physical address below 4 GiB. Linux has no bypass mode, so this is new on this GPU; topaz ran its a6xx
  this way.
- **Rail left as the bootloader set it** first, GPU at a low clock (19.2 MHz XO, then 160/266 MHz), with an RPM `rwcx`
  vote at the 266 MHz level (0x80). Higher clocks only after the job test passes, matching the corner map.
- **Polling, no interrupts** for completion (topaz did the same): CP_EVENT_WRITE CACHE_FLUSH_TS into a fence page.
- New file `bsd/dev/whyred_gpu.c` (hardware) for the steps. RedwoodGPU.cpp comes in stage 2.

## Steps (boot-arg whyredgpu=N runs 1..N, default 0 = nothing)

Each step logs what it touches before touching it and what it found after. Reads come before writes. A failed check
ends the run with one log line.

1. Read only: GCC GPU branches, gpucc (PLLs, RCG, branches), both GDSCs, speed bin, CPR4/LDO/mem-acc state.
2. RPM `rwcx` vote (level 0x80), GCC branch votes (gpu_cfg_ahb, gpu_bimc_gfx, bimc_gfx). Check halt bits.
3. CX GDSC on, gpucc AHB/CXO branches.
4. GX GDSC on (block reset pulse, GMEM reset pulse, unclamp, poll PWR_ON), gfx3d RCG at XO 19.2 MHz, gfx3d and
   rbbmtimer branches. Then the first reads inside the GPU block (RBBM status, CP registers).
5. SMMU: ID registers and SMR/S2CR as found (read only), then the identity bank for SID 0.
6. a5xx_start register setup: VBIF, UCHE ranges, GMEM range, A509 CP queue thresholds, PC_DBG_ECO, bank bits,
   CP_PROTECT, hardware clock gating (a512 list), 64-bit mode.
7. Microcode: read the two files, copy into contiguous memory, program the instruction bases. Ring (32 KiB) and
   RPTR shadow.
8. Zap shader: SCM PAS 13 init image / mem setup / auth and reset, as the ADSP boot (1001) does.
9. CP start (CP_ME_CNTL 0), CP_ME_INIT (8 dwords), CP_SET_SECURE_MODE 0 through the ring, wait for idle.
10. Job test: an indirect buffer with CP_MEM_WRITE of a known word, then CACHE_FLUSH_TS into the fence page. Pass =
    both values seen in memory.
11. Clock up: 266 MHz from the GPU PLL, repeat step 10 1000 times, log time per job.

## Risks

- A wrong access inside the GPU or SMMU block resets the SoC silently: steps stay gated and log first.
- The zap shader might need a memory region TrustZone accepts (Linux loaded it at 0xfc100000). The ADSP path found
  any contiguous region works for its PAS.
- SMMU identity on this GPU is unproven. Fallback: real stage 1 page tables (topaz's original 0901 code), one
  context, flat map of the pool.
- GX rail voltage at higher clocks: CPR4 corners are not programmed by us. Stay at or below 266 MHz until measured.

## Stage 2 preview

RedwoodGPU service with a5xx GET_PARAM values (GPU_ID 509, CHIP_ID 0x05000900, GMEM 0x44000), Mesa freedreno a5xx
(check that Mesa's device table has the 509), then CoreAnimation as on topaz.
