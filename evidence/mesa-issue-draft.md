# Mesa issue draft: malformed A6xx minimum-LOD descriptor on x86

Proposed title:

> [turnip][a6xx] Negative image-view MIN_LOD_CLAMP conversion breaks non-zero-base-mip reads on x86 and culls CS2 world draws

Target: `mesa/mesa` GitLab, Turnip/Freedreno.

This is a draft only. Do not publish the multi-gigabyte captures publicly
without choosing a suitable transfer location and checking them for private
data.

## Summary

`fdl6_view_init()` can pass a negative view-relative minimum LOD to the
unsigned A6xx `MIN_LOD_CLAMP` packer when an image view has a non-zero base
mip and no application-specified minimum LOD. The float-to-unsigned conversion
is out of range and therefore undefined. On the tested x86 build it encodes
`-4.0` as `0xc00` (LOD 12), while the native AArch64 build happens to encode
zero.

This makes valid `OpImageFetch` reads through a rebased image view return zero
on Adreno 690 with an x86 Turnip driver. It is independently reproduced by a
small headless Vulkan program and caused CS2's hierarchical-depth pyramid to
become zero, which in turn culled the game's world color draws.

A one-line lower clamp fixes the standalone reproducer and live CS2. Reverting
only that patch restores the reproducer failure.

The self-contained reproducer and proposed patch are in
[standalone reproducer](../reproducer/README.md).

## Affected source

In Mesa 26.2.0 commit
`9f0a761020bca92f2b07156a0621e5360cb8eca5` and Mesa 26.2.2,
`src/freedreno/fdl/fd6_view.cc` contains:

```cpp
view->descriptor[6] = A6XX_TEX_MEMOBJ_6_MIN_LOD_CLAMP(
   args->min_lod_clamp - args->base_miplevel);
```

For the default absolute minimum LOD of `0.0` and `baseMipLevel=4`, the packer
receives `-4.0`. `MIN_LOD_CLAMP` is a 12-bit unsigned fixed-point field with
eight fractional bits. The generated pack operation converts `value * 256.0`
to `uint32_t`, which is undefined for this negative value.

The tested patch is:

```cpp
view->descriptor[6] = A6XX_TEX_MEMOBJ_6_MIN_LOD_CLAMP(
   MAX2(0.0f, args->min_lod_clamp - (float)args->base_miplevel));
```

This preserves positive view-relative minimum-LOD restrictions while keeping
an absent or lower absolute clamp from becoming an invalid unsigned field.
The initial patch deliberately changes only the A6xx/A7xx path exercised by
Adreno 690; the analogous A8xx expression should be audited separately.

## Standalone reproduction

The test creates a 1024x1024 `VK_FORMAT_R32_SFLOAT` image with 11 mips. A
producer compute shader writes non-zero values to physical mip 4. A consumer
then reads the same texels through:

- a full view with `baseMipLevel=0` and shader LOD 4; and
- a rebased view with `baseMipLevel=4` and shader LOD 0.

These are valid views of the same physical subresource and must match. On the
affected x86 driver the first read succeeds and the rebased read returns zero:

```text
full(base=0,lod=4)[0,0]=0.899999976 rebased(base=4,lod=0)[0,0]=0.000000000
full(base=0,lod=4)[63,63]=0.912599981 rebased(base=4,lod=0)[63,63]=0.000000000
result=FAIL
```

Khronos validation reports no messages. As a diagnostic control, adding
`VkImageViewMinLodCreateInfoEXT{minLod=4}` to the rebased view makes the stock
x86 driver pass because the descriptor receives a view-relative clamp of
zero. Applications should not need this extension structure for the default
case.

## Controlled results

All GPU tests used the same Adreno 690.

| Driver/execution path | Result |
| --- | --- |
| Native AArch64 Turnip 26.2.2 | Reproducer passes |
| x86 Turnip 26.1.6 through FEX | Reproducer fails; rebased reads are zero |
| x86 Turnip 26.2.0 through FEX | Reproducer fails; rebased reads are zero |
| Matched x86 Turnip 26.2.2 through FEX | Reproducer fails; rebased reads are zero |
| x86 Turnip 26.2.3 through FEX | Reproducer fails; rebased reads are zero |
| Valve/SteamOS x86 candidate through FEX | Reproducer fails; rebased reads are zero |
| Stock x86 driver plus explicit absolute `minLod=4` | Reproducer passes |
| Patched matched x86 Turnip 26.2.2 | Reproducer passes |
| Reverted matched x86 Turnip 26.2.2 | Reproducer fails again |
| Patched Valve/SteamOS x86 candidate | Reproducer passes |
| Patched Valve/SteamOS x86 candidate in live CS2 | Dust II renders correctly |

The matched 26.2.2 test changed only the proposed line and used the same x86
compiler and build options for the fail/pass/fail sequence.

A CPU-only reduction of the pack operation produced:

```text
native AArch64: original field=0x000, decoded LOD=0
x86 through FEX: original field=0xc00, decoded LOD=12
```

The clamped expression produced `0x000` on both. This is evidence of the
architecture-dependent consequence, not a reliance on any particular result
from undefined behavior.

## System

- Device: Microsoft Windows Dev Kit 2023, Qualcomm SC8280XP
- GPU: Adreno 690
- Kernel: `7.3.0-rc2-10-volterra`, AArch64
- Game: native Linux x86_64 CS2, build ID `25218825`
- Runtime: Steam Linux Runtime 3.0 (`sniper`)
- CPU translation: FEX
- Baseline effective x86 driver:
  `Mesa 26.2.0 (git-9f0a761020)`, build ID
  `7f2f116cb999b174c30baae9e8a8e07354ada2da`
- Vulkan device: `Turnip Adreno (TM) 690`, Vulkan 1.3.354
- RenderDoc: 1.45, commit `2fc0bc04...`

FEX is not required to explain the failure: the malformed field is produced by
x86 Mesa code, the same x86 CPU probe produces `0xc00` natively, and the patch
fixes both the standalone GPU test and the application. FEX Vulkan thunking is
disabled; Steam imports the x86 Turnip provider from the FEX rootfs.

## CS2 consequence

RenderDoc traced a black building to this chain:

1. Depth-prepass event 2203 draws 4,092 indices and writes valid depth.
2. Dispatch 2945 downsamples CS2's R32F hierarchical-depth image. Physical
   mip 4 contains `0.8999014 .. 1.0`, but four `OpImageFetch` operations
   through a view with `baseMipLevel=4` return zero.
3. Mips 5-10 consequently become zero.
4. GPU-culling dispatch 2976 consumes those mips and generates a zero-index
   indirect command for the matching color draw.
5. Color event 3551 therefore does not draw the building, although its depth
   remains and occludes later fragments.

With the patched x86 driver, a live Dust II practice match renders the world,
weapon, materials, lighting, and HUD correctly. The CS2 process mapping was
checked and points to the patched `libvulkan_freedreno.so`. Clean native
1920x1080 screenshots taken after spawn protection expired are retained as
`evidence/images/before-stock.png` and `evidence/images/after-patched.png`,
with SHA-256 values
`3fd7b0dcefe7eed8e1066a152704b0bebbadaaf3b7ca16b00ce3747f8f358c57`
and
`643f76bc356d1dbab627993fee735d0645fd588963624a1dd5686693828d5d0e`.
They use the same settings and workload but different viewpoints, so their
FPS overlays are not a performance comparison.

The earlier observation that `TU_DEBUG=sysmem` changed the corruption remains
useful historical context, but it is no longer the primary root-cause
hypothesis.

## Separate Vulkan-validity finding

CS2 also creates pipeline layouts with five descriptor-set layouts while the
default driver advertises four, triggering
`VUID-VkPipelineLayoutCreateInfo-setLayoutCount-00286`. Making Turnip advertise
five removes that VUID but does not fix the frame. This application issue is
being tracked separately and is not needed to reproduce the minimum-LOD bug.

## Attachments

- Standalone source, shaders, instructions, results, and patch:
  [standalone reproducer](../reproducer/README.md)
- Baseline capture: `cs2_frame344.rdc`, 3,214,624,177 bytes
- Baseline thumbnail SHA-256:
  `8afa9add4184d0cbe14d838d0e51266d124ead9f641e4402daa2e38e8fc87b38`
- `TU_DEBUG=sysmem` capture: `cs2_sysmem_frame697.rdc`, 2,846,425,889 bytes
- `sysmem` capture SHA-256:
  `1483885af314c4bdb5a30517a1217c3cf81bb5d5b85d451d1c31acc210983f1f`

The captures and their XML exports are retained and can be provided privately.

## Request

Please review whether clamping the view-relative A6xx/A7xx minimum LOD to zero
is the correct fix, and whether the analogous A8xx path should receive the
same correction. I can test revisions on the A690, provide the complete
fail/pass/fail build details, and share the RenderDoc capture privately.
