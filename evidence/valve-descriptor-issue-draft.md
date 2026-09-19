# Valve issue draft: CS2 exceeds `maxBoundDescriptorSets`

Proposed title:

> [CS2][Linux][Vulkan] Pipeline layouts use 5 sets when maxBoundDescriptorSets is 4

Target: `ValveSoftware/csgo-osx-linux` GitHub issue tracker.

This is a draft only. It deliberately separates the Vulkan validity error from
the Turnip rendering defect; removing the error did not repair the corrupt
frame.

## System information

- CS2: native Linux build, build ID `25218825`
- Graphics API: Vulkan
- GPU: Qualcomm Adreno 690
- Driver: Turnip, effective x86 driver
  `Mesa 26.2.0 (git-9f0a761020)`
- Reported `VkPhysicalDeviceLimits::maxBoundDescriptorSets`: 4
- OS/kernel: custom Arch Linux ARM-based VolterraOS,
  `7.3.0-rc2-10-volterra` AArch64
- Execution: native Linux x86_64 game under FEX, Steam Linux Runtime 3.0
  (`sniper`)

## Description

Khronos validation reports that CS2 passes five descriptor-set layouts to
`vkCreatePipelineLayout` on a device advertising a limit of four:

```text
Validation Error: [ VUID-VkPipelineLayoutCreateInfo-setLayoutCount-00286 ]
vkCreatePipelineLayout(): pCreateInfo->setLayoutCount (5) exceeds the
maxBoundDescriptorSets limit (4).
```

The Vulkan valid-usage rule requires `setLayoutCount` to be less than or equal
to `maxBoundDescriptorSets`:
<https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineLayoutCreateInfo.html>.

A RenderDoc capture contains 83 `vkCreatePipelineLayout` calls:

- 38 with `setLayoutCount=5`;
- 43 with `setLayoutCount=3`;
- 2 with `setLayoutCount=4`.

Validation emitted this one unique VUID ten times in the baseline run. All 451
recorded `vkCmdBindDescriptorSets` calls in the capture have
`dynamicOffsetCount=0`; only two dynamic descriptor declarations appear in the
XML.

## Reproduction

1. Run the native Linux CS2 build with the Khronos validation layer enabled on
   a Vulkan device where `maxBoundDescriptorSets` is 4.
2. Launch a local map with `-condebug -insecure +map de_dust2`.
3. Inspect validation output during pipeline creation.

The VUID is reproducible in the baseline configuration.

## Control experiment

Turnip normally reserves one of the A6xx hardware's five sets for dynamic
offset handling and advertises four. For diagnosis only, I applied the
CS2-scoped drirc option:

```xml
<option name="tu_dont_reserve_descriptor_set" value="true" />
```

The live CS2 process had the drirc environment, loaded the x86 Khronos
validation layer and x86 Turnip provider, and no new descriptor-limit VUID was
emitted. CS2 still rendered the world incorrectly. The same remained true when
combined with `TU_DEBUG=sysmem`.

This establishes that the five-set call is invalid on the default device but
does not establish it as the cause of the separate rendering corruption.

## Expected result

CS2 should query and obey `VkPhysicalDeviceLimits::maxBoundDescriptorSets`, and
should not create a pipeline layout with more sets than the device advertises.

## Actual result

CS2 creates five-set pipeline layouts when the advertised limit is four,
triggering `VUID-VkPipelineLayoutCreateInfo-setLayoutCount-00286`.

## Attachments available

- Khronos validation log with all ten messages
- RenderDoc 1.45 capture and structured XML
- `vulkaninfo --summary`
- process environment/maps proving which x86 validation and Turnip libraries
  the game loaded
- separate Mesa/Turnip rendering report, to be cross-linked after filing

The full RenderDoc capture is approximately 3.2 GB and can be transferred by
arrangement.
