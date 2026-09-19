# Turnip non-zero-base-mip reproducer

This standalone Vulkan compute test reproduces the descriptor bug that made
CS2's hierarchical depth-pyramid reads return zero on Adreno 690 when using
an x86 Turnip driver through FEX. It does not require CS2, Steam, a window
system, or RenderDoc.

The test creates one 1024x1024 `VK_FORMAT_R32_SFLOAT` image with 11 mip
levels. A producer shader writes a deterministic non-zero pattern to physical
mip 4. A consumer shader reads the same physical texels through:

- a full image view (`baseMipLevel=0`) with shader LOD 4; and
- a rebased image view (`baseMipLevel=4`) with shader LOD 0.

The values must match. `--explicit-min-lod` adds
`VkImageViewMinLodCreateInfoEXT{minLod=4}` to the rebased view. That option is
a diagnostic control: it queries and enables the extension's required
`minLod` device feature, rejects devices that do not support it, and avoids the
faulty negative view-relative minimum-LOD packing in the affected Turnip
build. Applications should not need it.

## Build

```sh
glslangValidator -V producer.comp -o producer.spv
glslangValidator -V fetch.comp -o fetch.spv
cc -O2 -Wall -Wextra repro.c -o repro -lvulkan -lm
```

The optional CPU-only packer probe can be built and run independently:

```sh
cc -O2 -Wall -Wextra lod_pack_probe.c -o lod-pack-probe
./lod-pack-probe -4
```

For the x86/FEX comparison, compile `repro.c` with the x86 compiler in the FEX
rootfs and execute the resulting x86 binary through FEX. SPIR-V is portable
between both runs.

## Run

```sh
./repro producer.spv fetch.spv
./repro producer.spv fetch.spv --explicit-min-lod
./repro producer.spv fetch.spv --device-index 0
```

Without `--device-index`, the reproducer selects the sole enumerated device
whose Vulkan device name contains `Turnip`. It rejects a run with no Turnip
device or multiple Turnip devices instead of silently testing an unrelated
hardware or software implementation. Use `--device-index` to disambiguate or
to make a deliberate cross-driver comparison; the selected index and device
name are printed before the result.

The producer's `r32f` storage image requires
`shaderStorageImageExtendedFormats`. The reproducer queries and enables that
feature for every run and rejects devices that do not support it.

## Results on the Windows Dev Kit 2023

All tests used Adreno 690. Khronos validation reported no messages for the
basic reproducer.

| Driver/execution path | Result |
| --- | --- |
| Native ARM64 Turnip 26.2.2 | Pass |
| x86 Turnip 26.1.6 through FEX | Fail: rebased reads are `0.0` |
| x86 Turnip 26.2.0 through FEX | Fail: rebased reads are `0.0` |
| x86 Turnip 26.2.2 through FEX | Fail: rebased reads are `0.0` |
| x86 Turnip 26.2.3 through FEX | Fail: rebased reads are `0.0` |
| Valve/SteamOS x86 candidate through FEX | Fail: rebased reads are `0.0` |
| Stock x86 driver plus `--explicit-min-lod` | Pass |
| Patched x86 Turnip 26.2.2 | Pass |
| Reverted x86 Turnip 26.2.2 | Fail again |
| Patched Valve/SteamOS x86 candidate | Pass |

Representative failing output:

```text
full(base=0,lod=4)[0,0]=0.899999976 rebased(base=4,lod=0)[0,0]=0.000000000
full(base=0,lod=4)[63,63]=0.912599981 rebased(base=4,lod=0)[63,63]=0.000000000
result=FAIL
```

With the patch, both rebased values match the full-view values and the test
prints `result=PASS`. Reverting only the patch restores the failure.

The candidate patch clamps the A6xx/A7xx view-relative minimum LOD before it
is packed into an unsigned 12-bit fixed-point descriptor field. With the
default absolute minimum LOD of zero and `baseMipLevel=4`, the old expression
passes `-4.0` to a floating-to-unsigned conversion. On the tested x86 build it
encodes as `0xc00` (LOD 12); on the native ARM64 build it happens to encode as
zero. The conversion is out of range and therefore undefined in C++.

The A8xx equivalent should be audited separately. The patch here deliberately
changes only the path exercised on A690.
