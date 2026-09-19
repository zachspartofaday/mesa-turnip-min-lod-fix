# CS2 ARM64 rendering investigation — 18 September 2026

**CS2 now renders correctly through native ARM Steam plus FEX with a focused
x86 Turnip patch.** The original black/white world was caused by an undefined
float-to-unsigned conversion while A6xx image-view descriptors packed a
negative view-relative `MIN_LOD_CLAMP`. On the tested x86 build that value
became LOD 12; the native AArch64 build happened to produce zero. Clamping the
relative minimum LOD to zero fixes a standalone Vulkan reproducer and live
Dust II. Reverting only that patch restores the reproducer failure.

This is a diagnostic receipt, not a release qualification. It records the
RenderDoc failure chain, standalone reproduction, matched-version GPU A/B/A,
live-game confirmation, and the experimental driver currently staged on the
test system. It separately confirms why FEX Vulkan thunks cannot currently be
used through Steam's pressure-vessel runtime.

## Tested system

| Component | Observed value |
| --- | --- |
| Kernel | `7.3.0-rc2-10-volterra`, AArch64 |
| GPU | Turnip Adreno 690, Vulkan 1.3.354 |
| Host ARM Mesa packages | `mesa-volterra-canary 1:26.2.2-2`, `vulkan-freedreno-volterra-canary 1:26.2.2-2` |
| Effective x86 CS2 Vulkan driver | `Mesa 26.2.0 (git-9f0a761020)` |
| Working experimental driver | Valve/SteamOS x86 Turnip candidate with the one-line minimum-LOD clamp; SHA-256 `a9325e549221bad662895d97c60e9dffa39ec1a84ee09bd1882f0a7a17d79cce` |
| FEX | `fex-emu-volterra 2609.20260908.395b132f-2`; base commit `395b132f346b1a45def246d10c52245edba1ef02` |
| CS2 | native Linux build ID `25218825` |
| Steam runtime | Steam Linux Runtime 3.0 (`sniper`) through the Volterra FEX compatibility tool |

The same gameplay corruption occurred with Valve's FEX 2607 build and the
Volterra FEX 2609 build. Direct x86 `vkcube` and `vulkaninfo` under FEX render
and enumerate Turnip correctly.

The host package version is not the CS2 driver version. With Vulkan thunks
disabled, pressure-vessel imports the x86 graphics provider from the FEX
rootfs. Its pacman database still records Mesa `1:26.1.6-1`, but the actual
`usr/lib/libvulkan_freedreno.so` is the official Mesa 26.2.0 release build at
commit `9f0a761020bca92f2b07156a0621e5360cb8eca5` (build ID
`7f2f116cb999b174c30baae9e8a8e07354ada2da`). Process maps confirmed this was
the library used by the baseline game. Updating only the host AArch64 Mesa
package therefore does not update CS2's no-thunk driver.

## Confirmed root cause and patch

The relevant A6xx/A7xx path in `src/freedreno/fdl/fd6_view.cc` originally
packed:

```cpp
A6XX_TEX_MEMOBJ_6_MIN_LOD_CLAMP(
   args->min_lod_clamp - args->base_miplevel)
```

For CS2's rebased view, the default absolute minimum LOD is `0.0` and
`baseMipLevel` is 4, so the generated unsigned fixed-point packer receives
`-4.0`. Converting the resulting `-1024.0` to `uint32_t` is out of range and
undefined. A CPU-only reduction produced field `0x000` (LOD 0) on native
AArch64 and `0xc00` (LOD 12) in the x86 code path. FEX is not needed to cause
that x86 result.

The tested correction is:

```cpp
A6XX_TEX_MEMOBJ_6_MIN_LOD_CLAMP(
   MAX2(0.0f, args->min_lod_clamp - (float)args->base_miplevel))
```

A standalone headless Vulkan test writes known non-zero values to physical
mip 4 and reads the same texels through both a full image view at shader LOD 4
and a rebased image view at shader LOD 0. Stock x86 Turnip returns zero only
for the rebased view. Native AArch64 passes. Explicitly supplying absolute
`minLod=4` also makes the stock x86 driver pass, independently confirming the
descriptor field. A matched Mesa 26.2.2 x86 build produced the decisive
sequence: unpatched fail, patched pass, reverted fail.

The same patch was then built into the Valve/SteamOS x86 Turnip candidate and
installed as the FEX-rootfs graphics provider. The standalone test passed, and
a live Dust II practice match rendered the world, weapon, materials, lighting,
and HUD correctly. The running CS2 process mapped that exact patched library.
The retained screenshot is
`/home/deck/volterra-experiments/cs2-hzb-repro/cs2-min-lod-patched-ingame.png`,
SHA-256
`366254023fcff421779545a98fab6261d068b6b3e6d0dd04362858ce23875fa6`.

The reproducer, shaders, instructions, and patch are retained in
[standalone reproducer](../reproducer/README.md).

## Rendering evidence

The retained RenderDoc 1.45 capture is frame 344 of a local Dust II match:

- capture: `/home/deck/.local/share/fex-emu/renderdoc-captures/cs2_frame344.rdc`
  on the test system, 3,214,624,177 bytes;
- thumbnail: `cs2_frame344-thumb.png`, 3,056,532 bytes, SHA-256
  `8afa9add4184d0cbe14d838d0e51266d124ead9f641e4402daa2e38e8fc87b38`;
- exported Chrome trace: `cs2_frame344.chrome.json`, 7,076,850 bytes;
- structured XML: `cs2_frame344.zip.xml`, 93,053,120 bytes, with its retained
  2.13 GB companion archive.
- paired `TU_DEBUG=sysmem` capture:
  `cs2_sysmem_frame697.rdc`, 2,846,425,889 bytes, SHA-256
  `1483885af314c4bdb5a30517a1217c3cf81bb5d5b85d451d1c31acc210983f1f`;
  its 79,845,438-byte XML export and 1,927,298,503-byte companion are retained
  beside it;
- comparison screenshots retained beside the capture:
  `cs2-wayland-baseline.png` (SHA-256
  `276494c9e8a6081e73543ac4173f592fd8d381ec45c26e456c95c9966df752c4`),
  `cs2-tu-debug-sysmem.png` (SHA-256
  `1d482c7f2a3c14a07b32039b55968342607635ca68f96548d2ff65a5dfd0e818`),
  and `cs2-tu-debug-flushall.png` (SHA-256
  `032731c71a2fe7c6b6f2a6c7a3c694854b0a15b2bafe6c50e9991e292c1242ee`);
- version comparison screenshots in the same directory:
  `cs2-mesa-26.2.3.png` (SHA-256
  `4d33729923a3d67169a2f55d98e7ae6f4507a59a3fdc7ff5d56220d2d3df415b`)
  and `cs2-mesa-26.1.6.png` (SHA-256
  `8399fa940a5774a066b2e2d769fd7888c0ec3a372c7aee625e7e0cbd516ea0ce`).

The capture contains approximately 6.9 GB of raw resource data, 15,228
resources checked, 4,713 serialized resources, and 18,836 referenced
resources. Its thumbnail shows the black/white world and correctly rendered
HUD before any VNC encoding. Do not add the multi-gigabyte artifacts to Git;
retain or transfer them separately for an upstream report.

### First bad resource and missing-draw chain

Headless RenderDoc replay on the A690 traced the presented corruption backward
through the actual resources and pixel histories:

1. The final swapchain image (`ResourceId 38938`) is assembled at event 6126
   from the full-resolution HDR scene (`ResourceId 39073`). The HUD is added
   later at event 6178.
2. `39073` is already corrupt before tone mapping. Its source scene image
   (`ResourceId 39061`) is already corrupt before bloom and final compositing.
3. At a representative black building pixel `(500, 300)`, visible fragment
   shaders produce plausible non-black colors, but their fragments fail the
   `LESS_OR_EQUAL` depth test. The depth target (`ResourceId 38974`, D24S8)
   contains `0.9417928`, written by the depth-only draw at event 2203.
4. Event 2203 draws 4,092 indices for the building into the depth prepass. The
   only later base-color draw with the same geometry buffer and instance slot
   is event 3551, but its generated `VkDrawIndexedIndirectCommand` has
   `indexCount=0`; it therefore writes no color and leaves the valid depth
   behind.
5. The indirect buffer (`ResourceId 87510`) is cleared and then generated by
   compute dispatch 2976. That shader uses the hierarchical-depth texture
   (`ResourceId 8975`, 1024x1024 R32_FLOAT, 11 mips) for occlusion culling.
6. Mips 0-4 contain plausible depth values (`0.8983` through `1.0`). Mips 5-10
   are entirely zero. Large screen-space objects select those higher mips and
   are incorrectly reduced to zero-index indirect draws.
7. Dispatch 2945 is specifically responsible for downsampling mip 4 into mips
   5-8. It binds a sampled view of mip 4 and storage views of mips 5-8. The
   mip-4 image barrier transitions that subresource from `GENERAL` to
   `SHADER_READ_ONLY_OPTIMAL` with compute-shader write visibility followed by
   shader-sampled-read access.
8. RenderDoc's compute-thread debugger for dispatch 2945 shows all four
   `ImageFetch` operations at thread `(0,0,0)` returning `0.0`, even though a
   direct min/max query of the bound mip-4 subresource reports
   `0.8999014-1.0`. The shader consequently writes zero to mip 5; dispatch 2950
   propagates zero through mips 9-10.

This is substantially narrower than a generic lighting or shader failure. The
immediate bad operation is a sampled read through an R32F image view whose base
mip is non-zero. The underlying mip contains valid data, but the malformed
minimum-LOD descriptor makes the compute shader observe zero. The resulting
invalid depth pyramid drives GPU culling to omit large world draws. The
standalone Vulkan reproduction and patch A/B/A below independently confirm
that descriptor cause.

Khronos validation emitted one unique VUID ten times:

```text
VUID-VkPipelineLayoutCreateInfo-setLayoutCount-00286
setLayoutCount (5) exceeds maxBoundDescriptorSets (4)
```

The structured capture contains 83 `vkCreatePipelineLayout` calls: 38 use
five set layouts, 43 use three, and two use four. All 451 recorded
`vkCmdBindDescriptorSets` calls have `dynamicOffsetCount=0`; only two dynamic
descriptor declarations appear in the XML. Turnip normally reserves one of
the A6xx hardware's five descriptor sets for dynamic offsets and therefore
advertises four to applications. Setting
`tu_dont_reserve_descriptor_set=true` changed the advertised limit from four
to five, but did not change the corrupted frame. The limit violation is real
and reportable, but that override result means it is not established as the
sole rendering cause.

## Controlled A/B results

| Test | Result |
| --- | --- |
| Baseline native Linux CS2 Vulkan | Menu and HUD render; gameplay world corrupt |
| Standalone repro, native AArch64 Turnip 26.2.2 | Pass |
| Standalone repro, matched x86 Turnip 26.2.2 | Fail: rebased reads are zero |
| Stock x86 driver plus explicit absolute `minLod=4` | Pass |
| Matched x86 26.2.2 with minimum-LOD clamp | Pass |
| Same matched build after reverting only the clamp | Fail again |
| Patched Valve/SteamOS x86 candidate, standalone repro | Pass |
| Patched Valve/SteamOS x86 candidate, live CS2 | Dust II world and weapon render correctly; observed average roughly 36 FPS |
| FEX 2607 versus Volterra FEX 2609 | Same gameplay corruption |
| FSR disabled and shaders rebuilt | No change |
| `-vulkan` | No change; the native Linux build already uses Vulkan |
| `TU_DEBUG=noubwc` | No change |
| `TU_DEBUG=nolrz` | No change |
| `TU_DEBUG=sysmem` | Materially different corruption: weapon and nearby lit floor render, most of the map remains black; still unusable, about 38 FPS |
| `TU_DEBUG=sysmem` plus `tu_dont_reserve_descriptor_set=true` | Five-set violation disappears with validation active, but the same black world remains; validation reduces this run to about 12 FPS |
| `TU_DEBUG=flushall` | No material change; world remains corrupt, about 36 FPS |
| `TU_DEBUG=nobin` | No material correction; large black world areas remain, about 37 FPS in the retained team-selection frame |
| `TU_DEBUG=3d_load` | No material correction; large black world areas remain, about 36 FPS in the retained team-selection frame |
| `TU_DEBUG=unaligned_store` | No material correction; large black world areas remain, about 37 FPS in the retained team-selection frame |
| `IR3_SHADER_DEBUG=nofp16` | No change |
| `TU_DEBUG=sysmem,nolrz` | Startup stalled; not a usable workaround |
| `tu_dont_reserve_descriptor_set=true` | Limit becomes five; corruption remains |
| `SDL_VIDEO_DRIVER=wayland` with thunks disabled | Dust II still corrupt, about 31 FPS |
| Signed Arch x86 Turnip 26.2.3 (`4d3f224e...`) | Exact private driver mapping confirmed; Dust II live world remains corrupt, about 37 FPS in the retained frame |
| Signed Arch x86 Turnip 26.1.6 (`56b1865c...`) | Exact private driver mapping confirmed; same corruption, about 46 FPS in the retained warm frame |
| Direct x86 `vkcube` under FEX | Renders correctly |
| RenderDoc final backbuffer | Contains the corruption; excludes VNC/PipeWire as its source |
| `+r_csgo_gpu_culling 0` | Does not fully correct the live frame; black/white world corruption remains |

The native-Wayland test is significant because a separate Valve report says
it can fix black/flickering CS2 content on some Linux configurations. It did
not help this system, so XCB/XWayland is not the remaining cause here.
The standalone `sysmem` result was the only Mesa debug switch that materially
changed the bad frame, but the later reproducer and patch A/B/A identify the
minimum-LOD descriptor as the actual cause of the missing world draws. The
earlier GMEM/load-store interpretation is therefore historical context, not
the current root-cause conclusion.

The combined five-set run verified the settings on the live CS2 process. It
mapped the x86 Khronos validation layer and the pressure-vessel-provided x86
`libvulkan_freedreno.so`; no new descriptor-limit VUID appeared in that
launch's FEX log. The same drirc setting reports five sets in a direct x86
probe. Correcting the advertised limit therefore closes the obvious
interaction experiment, but not the rendering defect.

The baseline captured 38,421 Vulkan chunks and 61 dynamic-rendering scopes;
the later `sysmem` capture contains 39,171 chunks and 120 scopes. They are
different game frames with different command topology, so resource IDs and
event numbers cannot be compared one-for-one. The event and resource IDs in
the failure chain above apply to the baseline capture.

The retained screenshots for the new bounded tests are:

- `cs2-sysmem-five-ingame.png`, SHA-256
  `9b8d55ba1bc4fe841218cd1f94f54f8976371df9ee485938a37eba65c1b88cb7`;
- `cs2-tu-debug-nobin.png`, SHA-256
  `8ccc596b03d1be45d96c8a10d712058428158695a7eecc1a76ccab79a8402c41`;
- `cs2-tu-debug-3d-load.png`, SHA-256
  `40a64780fb9da9685e9f964efdab97eb44f6aa22f6592550e03b975ce86a9b1e`;
- `cs2-tu-debug-unaligned-store.png`, SHA-256
  `0520afad861055d62744ca1266d49922b0b4ffdbd8a9bba920771ec902674de0`.

Mesa 26.2.3's release-noted CS2 fix does not generalize to this device. The
fix is commit `88283cb8ce58` (`anv: fix cmd_buffer_set_indirect_stride on ARL`),
which changes an Intel ANV Arrow Lake `STATE_BYTE_STRIDE` decision from
`aligned` to `!aligned` in `src/intel/vulkan/genX_cmd_draw.c`. There is no
corresponding Turnip code path to patch. The complete 26.2.3 Turnip driver,
including its shared SPIR-V and Turnip fixes, failed the controlled test; the
26.1.6 rollback failed too. This excludes the Intel report's 26.2 regression
window as the cause of the Adreno symptom. See Mesa's
[26.2.3 release notes](https://docs.mesa3d.org/relnotes/26.2.3.html) and
[work item 16140](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/16140).

## FEX thunk / pressure-vessel result

The Vulkan thunk is healthy outside Steam:

- direct FEX `/usr/bin/true` with `ThunksDB.Vulkan=1`: exit 0;
- direct x86 `vulkaninfo --summary` with the same config: exit 0 and Turnip
  enumerated.

Inside Steam Linux Runtime `sniper`, the same Vulkan-thunk config makes four
FEX helper processes terminate with SIGSEGV during container setup. The
runtime reports:

```text
Requested thunking via guest library
"/usr/share/fex-emu/GuestThunks/libvulkan-guest.so" that does not exist
```

The otherwise identical no-thunk runtime command exits 0, creates no
coredumps, and enumerates Turnip. An actual CS2 thunk launch likewise crashes
before game startup. This matches the container-boundary failure documented
in upstream [FEX issue #5787](https://github.com/FEX-Emu/FEX/issues/5787),
with earlier related behavior in [FEX issue #4639](https://github.com/FEX-Emu/FEX/issues/4639).
Do not enable Vulkan, DRM, or Wayland FEX thunks by default in the Steam bridge
until the pressure-vessel integration problem is fixed and requalified.

## External corroboration and next action

An Ubuntu ARM64 Steam tester on Snapdragon X Elite reported CS2 running at
reasonable frame rate while the map became black boxes from an apparent
lighting/shader failure in Canonical's
[ARM64 Steam testing thread](https://discourse.ubuntu.com/t/call-for-testing-steam-snap-for-arm64/74719?page=3).
That is the same symptom class on closely related Adreno/Turnip hardware;
NVIDIA ARM success does not exercise this driver.

The standalone Vulkan reproduction is complete. The next useful step is an
upstream Mesa/Turnip report containing:

1. the RenderDoc thumbnail and, by arrangement, the full capture;
2. the exact Mesa commit/driver string and Adreno 690 identity;
3. the descriptor-set validation VUID and capture counts;
4. the architecture comparison, explicit-minimum-LOD control, and matched
   x86 26.2.2 fail/pass/fail patch sequence;
5. the exact hierarchical-depth failure chain above, including the valid
   mip-4 range, zero mip-5 read/write, indirect `indexCount=0`, and the
   relevant barriers;
6. the successful live CS2 run with the patched driver and its mapped-library
   evidence;
7. a note that a similar Snapdragon X Elite result is public in Canonical's
   ARM64 Steam testing thread.

The evidence is also suitable for Valve's Linux CS2 tracker because the game
creates pipeline layouts beyond the device's advertised limit. Mesa and Valve
reports should cross-link rather than presuming which side owns the final fix.

## Current test-system state

CS2 was no longer running at the final state check, but the successfully
tested patched driver remains staged for another launch. The live
compatibility wrapper remains the clean pre-thunk version, SHA-256
`89d09357204c58557365f674961b021125e4f00c6a3489beb065267a241f0f2d`;
it contains no RenderDoc, drirc, forced Wayland, forced-map, or extra-thunk
diagnostic hooks.

The FEX rootfs currently contains the experimental patched Valve/SteamOS x86
Turnip library at
`/home/deck/.local/share/fex-emu/RootFS/ArchLinux/usr/lib/libvulkan_freedreno.so`,
SHA-256
`a9325e549221bad662895d97c60e9dffa39ec1a84ee09bd1882f0a7a17d79cce`.
The original stock Mesa 26.2.0 library is backed up at
`/home/deck/volterra-experiments/cs2-hzb-repro/rootfs-stock-26.2.0-libvulkan_freedreno.so`,
SHA-256
`a0b6f67e7f7fb64f5418cca3a59c73f564ca6e0c24d175f7f346e3645f5959a0`.
The temporary build-time `libdisplay-info` addition was removed from the
rootfs; the patched driver does not depend on it. FSR remains disabled. The
private validation layer, RenderDoc binaries, captures, replay analysis logs,
and private Mesa driver trees remain for follow-up.
