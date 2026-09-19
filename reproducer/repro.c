/* Minimal Vulkan reproducer for Turnip's A6xx non-zero-base-mip read bug. */
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>

#define VK_CHECK(call)                                                         \
    do {                                                                       \
        VkResult result_ = (call);                                             \
        if (result_ != VK_SUCCESS) {                                           \
            fprintf(stderr, "%s failed: %d\n", #call, result_);              \
            exit(2);                                                           \
        }                                                                      \
    } while (0)

static uint32_t find_memory_type(VkPhysicalDevice physical_device,
                                 uint32_t type_bits,
                                 VkMemoryPropertyFlags required)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & required) == required)
            return i;
    }
    fprintf(stderr, "No compatible Vulkan memory type\n");
    exit(2);
}

static uint32_t *read_spirv(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        perror(path);
        exit(2);
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        perror("fseek");
        exit(2);
    }
    long length = ftell(file);
    if (length <= 0 || (length % 4) != 0) {
        fprintf(stderr, "Invalid SPIR-V length for %s\n", path);
        exit(2);
    }
    rewind(file);
    uint32_t *data = malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "Unable to read %s\n", path);
        exit(2);
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static VkShaderModule create_shader(VkDevice device, const char *path)
{
    size_t size = 0;
    uint32_t *code = read_spirv(path, &size);
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = code,
    };
    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(device, &info, NULL, &module));
    free(code);
    return module;
}

static VkImageView create_view(VkDevice device, VkImage image,
                               uint32_t base_mip, uint32_t level_count,
                               const VkImageViewMinLodCreateInfoEXT *min_lod)
{
    VkImageViewCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = min_lod,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R32_SFLOAT,
        .components = {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = base_mip,
            .levelCount = level_count,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    VkImageView view;
    VK_CHECK(vkCreateImageView(device, &info, NULL, &view));
    return view;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s producer.spv fetch.spv [--explicit-min-lod] "
                "[--device-index INDEX]\n",
                argv[0]);
        return 2;
    }
    int explicit_min_lod = 0;
    uint32_t requested_device = UINT32_MAX;
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--explicit-min-lod") == 0) {
            explicit_min_lod = 1;
        } else if (strcmp(argv[i], "--device-index") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            char *end = NULL;
            errno = 0;
            uintmax_t parsed = strtoumax(value, &end, 10);
            if (!value[0] || strspn(value, "0123456789") != strlen(value) ||
                errno == ERANGE || end == value || *end != '\0' ||
                parsed >= UINT32_MAX) {
                fprintf(stderr, "invalid device index: %s\n", value);
                return 2;
            }
            requested_device = (uint32_t)parsed;
        } else {
            fprintf(stderr, "unknown or incomplete option: %s\n", argv[i]);
            return 2;
        }
    }

    VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "cs2-hzb-view-repro",
        .applicationVersion = 1,
        .pEngineName = "none",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
    };
    VkInstance instance;
    VK_CHECK(vkCreateInstance(&instance_info, NULL, &instance));

    uint32_t physical_count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, NULL));
    if (physical_count == 0) {
        fprintf(stderr, "No Vulkan physical devices\n");
        return 2;
    }
    VkPhysicalDevice *physical_devices =
        calloc(physical_count, sizeof(*physical_devices));
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_count,
                                        physical_devices));
    uint32_t selected_device = requested_device;
    if (requested_device >= physical_count) {
        if (requested_device != UINT32_MAX) {
            fprintf(stderr, "Device index %u is unavailable; found %u devices\n",
                    requested_device, physical_count);
            free(physical_devices);
            return 2;
        }

        for (uint32_t i = 0; i < physical_count; ++i) {
            VkPhysicalDeviceProperties candidate_properties;
            vkGetPhysicalDeviceProperties(physical_devices[i],
                                          &candidate_properties);
            if (strstr(candidate_properties.deviceName, "Turnip") == NULL)
                continue;
            if (selected_device != UINT32_MAX) {
                fprintf(stderr,
                        "Multiple Turnip devices found; use --device-index\n");
                free(physical_devices);
                return 2;
            }
            selected_device = i;
        }
        if (selected_device == UINT32_MAX) {
            fprintf(stderr,
                    "No Turnip device found; use --device-index to select "
                    "another implementation explicitly\n");
            free(physical_devices);
            return 2;
        }
    }
    VkPhysicalDevice physical_device = physical_devices[selected_device];
    free(physical_devices);

    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(physical_device, &device_properties);
    printf("device_index=%u device=%s api=%u.%u.%u driver=0x%x\n",
           selected_device,
           device_properties.deviceName,
           VK_VERSION_MAJOR(device_properties.apiVersion),
           VK_VERSION_MINOR(device_properties.apiVersion),
           VK_VERSION_PATCH(device_properties.apiVersion),
           device_properties.driverVersion);

    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(physical_device, VK_FORMAT_R32_SFLOAT,
                                        &format_properties);
    const VkFormatFeatureFlags needed =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if ((format_properties.optimalTilingFeatures & needed) != needed) {
        fprintf(stderr, "R32_SFLOAT lacks sampled/storage optimal support\n");
        return 2;
    }

    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_count,
                                             NULL);
    VkQueueFamilyProperties *queues = calloc(queue_count, sizeof(*queues));
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_count,
                                             queues);
    uint32_t queue_family = UINT32_MAX;
    for (uint32_t i = 0; i < queue_count; ++i) {
        if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            queue_family = i;
            break;
        }
    }
    free(queues);
    if (queue_family == UINT32_MAX) {
        fprintf(stderr, "No compute queue family\n");
        return 2;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    VkPhysicalDeviceImageViewMinLodFeaturesEXT supported_min_lod = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT,
    };
    VkPhysicalDeviceFeatures2 supported_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &supported_min_lod,
    };
    VkPhysicalDeviceImageViewMinLodFeaturesEXT enabled_min_lod = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT,
        .minLod = VK_TRUE,
    };
    vkGetPhysicalDeviceFeatures2(physical_device, &supported_features);
    if (!supported_features.features.shaderStorageImageExtendedFormats) {
        fprintf(stderr,
                "shaderStorageImageExtendedFormats feature is unsupported\n");
        return 2;
    }
    if (explicit_min_lod && !supported_min_lod.minLod) {
        fprintf(stderr,
                "VK_EXT_image_view_min_lod minLod feature is unsupported\n");
        return 2;
    }
    VkPhysicalDeviceFeatures enabled_features = {
        .shaderStorageImageExtendedFormats = VK_TRUE,
    };
    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = explicit_min_lod ? &enabled_min_lod : NULL,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .pEnabledFeatures = &enabled_features,
    };
    const char *device_extensions[] = {
        VK_EXT_IMAGE_VIEW_MIN_LOD_EXTENSION_NAME,
    };
    if (explicit_min_lod) {
        device_info.enabledExtensionCount = 1;
        device_info.ppEnabledExtensionNames = device_extensions;
    }
    VkDevice device;
    VK_CHECK(vkCreateDevice(physical_device, &device_info, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R32_SFLOAT,
        .extent = {1024, 1024, 1},
        .mipLevels = 11,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage image;
    VK_CHECK(vkCreateImage(device, &image_info, NULL, &image));
    VkMemoryRequirements image_requirements;
    vkGetImageMemoryRequirements(device, image, &image_requirements);
    VkMemoryAllocateInfo image_allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = image_requirements.size,
        .memoryTypeIndex = find_memory_type(
            physical_device, image_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    VkDeviceMemory image_memory;
    VK_CHECK(vkAllocateMemory(device, &image_allocation, NULL, &image_memory));
    VK_CHECK(vkBindImageMemory(device, image, image_memory, 0));

    VkImageViewMinLodCreateInfoEXT rebased_min_lod = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_MIN_LOD_CREATE_INFO_EXT,
        .minLod = 4.0f,
    };
    VkImageView full_view = create_view(device, image, 0, 11, NULL);
    VkImageView rebased_view = create_view(
        device, image, 4, 1, explicit_min_lod ? &rebased_min_lod : NULL);
    VkImageView storage_view = create_view(device, image, 4, 1, NULL);

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(float) * 4,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer result_buffer;
    VK_CHECK(vkCreateBuffer(device, &buffer_info, NULL, &result_buffer));
    VkMemoryRequirements buffer_requirements;
    vkGetBufferMemoryRequirements(device, result_buffer, &buffer_requirements);
    VkMemoryAllocateInfo buffer_allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = buffer_requirements.size,
        .memoryTypeIndex = find_memory_type(
            physical_device, buffer_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    VkDeviceMemory buffer_memory;
    VK_CHECK(vkAllocateMemory(device, &buffer_allocation, NULL, &buffer_memory));
    VK_CHECK(vkBindBufferMemory(device, result_buffer, buffer_memory, 0));
    float *mapped = NULL;
    VK_CHECK(vkMapMemory(device, buffer_memory, 0, sizeof(float) * 4, 0,
                         (void **)&mapped));
    memset(mapped, 0, sizeof(float) * 4);

    VkDescriptorSetLayoutBinding producer_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    VkDescriptorSetLayoutCreateInfo producer_set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &producer_binding,
    };
    VkDescriptorSetLayout producer_set_layout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &producer_set_info, NULL,
                                          &producer_set_layout));

    VkDescriptorSetLayoutBinding consumer_bindings[3] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    VkDescriptorSetLayoutCreateInfo consumer_set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3,
        .pBindings = consumer_bindings,
    };
    VkDescriptorSetLayout consumer_set_layout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &consumer_set_info, NULL,
                                          &consumer_set_layout));

    VkDescriptorPoolSize pool_sizes[3] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 2,
        .poolSizeCount = 3,
        .pPoolSizes = pool_sizes,
    };
    VkDescriptorPool descriptor_pool;
    VK_CHECK(vkCreateDescriptorPool(device, &pool_info, NULL,
                                    &descriptor_pool));
    VkDescriptorSetLayout set_layouts[2] = {
        producer_set_layout, consumer_set_layout};
    VkDescriptorSetAllocateInfo set_allocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 2,
        .pSetLayouts = set_layouts,
    };
    VkDescriptorSet sets[2];
    VK_CHECK(vkAllocateDescriptorSets(device, &set_allocate, sets));

    VkDescriptorImageInfo storage_image = {
        .imageView = storage_view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    VkDescriptorImageInfo full_image = {
        .imageView = full_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkDescriptorImageInfo rebased_image = {
        .imageView = rebased_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkDescriptorBufferInfo output_buffer = {
        .buffer = result_buffer,
        .offset = 0,
        .range = sizeof(float) * 4,
    };
    VkWriteDescriptorSet writes[4] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = sets[0],
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &storage_image,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = sets[1],
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &full_image,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = sets[1],
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &rebased_image,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = sets[1],
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &output_buffer,
        },
    };
    vkUpdateDescriptorSets(device, 4, writes, 0, NULL);

    VkPipelineLayoutCreateInfo producer_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &producer_set_layout,
    };
    VkPipelineLayout producer_pipeline_layout;
    VK_CHECK(vkCreatePipelineLayout(device, &producer_layout_info, NULL,
                                    &producer_pipeline_layout));
    VkPipelineLayoutCreateInfo consumer_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &consumer_set_layout,
    };
    VkPipelineLayout consumer_pipeline_layout;
    VK_CHECK(vkCreatePipelineLayout(device, &consumer_layout_info, NULL,
                                    &consumer_pipeline_layout));

    VkShaderModule producer_shader = create_shader(device, argv[1]);
    VkShaderModule consumer_shader = create_shader(device, argv[2]);
    VkComputePipelineCreateInfo producer_pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = producer_shader,
            .pName = "main",
        },
        .layout = producer_pipeline_layout,
    };
    VkPipeline producer_pipeline;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                                      &producer_pipeline_info, NULL,
                                      &producer_pipeline));
    VkComputePipelineCreateInfo consumer_pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = consumer_shader,
            .pName = "main",
        },
        .layout = consumer_pipeline_layout,
    };
    VkPipeline consumer_pipeline;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                                      &consumer_pipeline_info, NULL,
                                      &consumer_pipeline));

    VkCommandPoolCreateInfo command_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queue_family,
    };
    VkCommandPool command_pool;
    VK_CHECK(vkCreateCommandPool(device, &command_pool_info, NULL,
                                 &command_pool));
    VkCommandBufferAllocateInfo command_allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer command_buffer;
    VK_CHECK(vkAllocateCommandBuffers(device, &command_allocate,
                                      &command_buffer));
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

    VkImageMemoryBarrier to_general = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 11,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0,
                         NULL, 1, &to_general);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      producer_pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            producer_pipeline_layout, 0, 1, &sets[0], 0, NULL);
    vkCmdDispatch(command_buffer, 8, 8, 1);

    VkImageMemoryBarrier to_read = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 11,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0,
                         NULL, 1, &to_read);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      consumer_pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            consumer_pipeline_layout, 0, 1, &sets[1], 0, NULL);
    vkCmdDispatch(command_buffer, 1, 1, 1);

    VkBufferMemoryBarrier to_host = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = result_buffer,
        .offset = 0,
        .size = sizeof(float) * 4,
    };
    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &to_host,
                         0, NULL);
    VK_CHECK(vkEndCommandBuffer(command_buffer));

    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
    };
    VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));

    printf("full(base=0,lod=4)[0,0]=%.9f rebased(base=4,lod=0)[0,0]=%.9f\n",
           mapped[0], mapped[1]);
    printf("full(base=0,lod=4)[63,63]=%.9f rebased(base=4,lod=0)[63,63]=%.9f\n",
           mapped[2], mapped[3]);
    const float expected0 = 0.9f;
    const float expected63 = 0.9126f;
    int passed = fabsf(mapped[0] - expected0) < 0.00001f &&
                 fabsf(mapped[1] - expected0) < 0.00001f &&
                 fabsf(mapped[2] - expected63) < 0.00001f &&
                 fabsf(mapped[3] - expected63) < 0.00001f &&
                 fabsf(mapped[0] - mapped[1]) < 0.000001f &&
                 fabsf(mapped[2] - mapped[3]) < 0.000001f;
    printf("result=%s\n", passed ? "PASS" : "FAIL");

    vkDeviceWaitIdle(device);
    vkUnmapMemory(device, buffer_memory);
    vkDestroyCommandPool(device, command_pool, NULL);
    vkDestroyPipeline(device, consumer_pipeline, NULL);
    vkDestroyPipeline(device, producer_pipeline, NULL);
    vkDestroyShaderModule(device, consumer_shader, NULL);
    vkDestroyShaderModule(device, producer_shader, NULL);
    vkDestroyPipelineLayout(device, consumer_pipeline_layout, NULL);
    vkDestroyPipelineLayout(device, producer_pipeline_layout, NULL);
    vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    vkDestroyDescriptorSetLayout(device, consumer_set_layout, NULL);
    vkDestroyDescriptorSetLayout(device, producer_set_layout, NULL);
    vkDestroyBuffer(device, result_buffer, NULL);
    vkFreeMemory(device, buffer_memory, NULL);
    vkDestroyImageView(device, storage_view, NULL);
    vkDestroyImageView(device, rebased_view, NULL);
    vkDestroyImageView(device, full_view, NULL);
    vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, image_memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return passed ? 0 : 1;
}
