#include <iostream>
#include <fstream>

#include "vulkan/vulkan.hpp"

static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = (size_t)file.tellg();
    if (fileSize == 0) {
        std::cout << "File is empty" << std::endl;
    }

    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

int main()
{
    try
    {
        auto applicationInfo = vk::ApplicationInfo("vk-compute-example", 1, "", 1, VK_API_VERSION_1_2);
        auto instanceCreateInfo = vk::InstanceCreateInfo({}, &applicationInfo);
        auto instance = vk::createInstance(instanceCreateInfo);

        // First device is good for us
        auto physicalDevice = instance.enumeratePhysicalDevices().front();

        // Find index of queue family that supports compute
        auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
        auto propertyIter = std::find_if(
            queueFamilyProperties.begin(), queueFamilyProperties.end(), [](vk::QueueFamilyProperties const& props) {
                return props.queueFlags & vk::QueueFlagBits::eCompute;
            });
        uint32_t computeQueueFamilyIndex = std::distance(queueFamilyProperties.begin(), propertyIter);

        auto deviceQueueCreateInfo = vk::DeviceQueueCreateInfo({}, computeQueueFamilyIndex, 1);
        auto deviceCreateInfo = vk::DeviceCreateInfo().setQueueCreateInfos(deviceQueueCreateInfo);
        auto device = physicalDevice.createDevice(deviceCreateInfo);

        auto memoryProperties = physicalDevice.getMemoryProperties();
        
        const int32_t bufferLength = 16384;
        const uint32_t bufferSize = sizeof(int32_t) * bufferLength;

        const VkDeviceSize memorySize = bufferSize * 2;

        uint32_t memoryTypeIndex = VK_MAX_MEMORY_TYPES;
 
        for (uint32_t k = 0; k < memoryProperties.memoryTypeCount; k++) {
            auto memoryType = memoryProperties.memoryTypes[k];

            if ((vk::MemoryPropertyFlagBits::eHostVisible & memoryType.propertyFlags) &&
                (vk::MemoryPropertyFlagBits::eHostCoherent & memoryType.propertyFlags) &&
                (memorySize < memoryProperties.memoryHeaps[memoryType.heapIndex].size)) {
                memoryTypeIndex = k;
                break;
            }
        }

        if (memoryTypeIndex == VK_MAX_MEMORY_TYPES) {
            throw vk::OutOfHostMemoryError("");
        }

        const vk::MemoryAllocateInfo memoryAllocateInfo(memorySize, memoryTypeIndex);
        vk::DeviceMemory memory = device.allocateMemory(memoryAllocateInfo);
        uint32_t* payload;
        payload = (uint32_t*)device.mapMemory(memory, 0, memorySize, {});

        
        for (uint32_t k = 1; k < 32768; k++) {
            payload[k] = rand();
        }
        
        device.unmapMemory(memory);

        auto bufferCreateInfo = vk::BufferCreateInfo().setSize(bufferSize)
                                                      .setUsage(vk::BufferUsageFlagBits::eStorageBuffer)
                                                      .setSharingMode(vk::SharingMode::eExclusive)
                                                      .setQueueFamilyIndexCount(1)
                                                      .setQueueFamilyIndices(computeQueueFamilyIndex);
        auto inBuffer = device.createBuffer(bufferCreateInfo);
        device.bindBufferMemory(inBuffer, memory, 0);

        auto outBuffer = device.createBuffer(bufferCreateInfo);
        device.bindBufferMemory(outBuffer, memory, bufferSize);
        
        // C:/VulkanSDK/1.2.189.0/Bin32/glslc.exe D:/projektit/vhce/src/compute.comp -o D:/projektit/vhce/bin/x64/Debug/comp.spv
        // Expecting comp.spv next to .exe
        auto code = readFile("comp.spv");

        auto shaderCreateInfo = vk::ShaderModuleCreateInfo().setCodeSize(code.size())
                                                            .setPCode(reinterpret_cast<const uint32_t*>(code.data()));
        
        auto shaderModule = device.createShaderModule(shaderCreateInfo);

        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
            vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute)
        };

        auto descriptorInfo = vk::DescriptorSetLayoutCreateInfo().setBindings(bindings)
                                                                 .setBindingCount(2);
        auto descriptorSetLayout = device.createDescriptorSetLayout(descriptorInfo);
        auto pipelineLayoutCreateInfo = vk::PipelineLayoutCreateInfo().setSetLayoutCount(1)
                                                                      .setSetLayouts(descriptorSetLayout);
        
        auto pipelineLayout = device.createPipelineLayout(pipelineLayoutCreateInfo);

        auto shaderStageCreateInfo = vk::PipelineShaderStageCreateInfo().setStage(vk::ShaderStageFlagBits::eCompute)
                                                                        .setModule(shaderModule)
                                                                        .setPName("main");

        auto computePipelineCreateInfo = vk::ComputePipelineCreateInfo().setStage(shaderStageCreateInfo)
                                                                        .setLayout(pipelineLayout);

        auto pipeline = device.createComputePipelines({}, computePipelineCreateInfo);

        auto descriptorPoolSize = vk::DescriptorPoolSize().setType(vk::DescriptorType::eStorageBuffer)
                                                          .setDescriptorCount(2);
        auto descriptorPoolCreateInfo = vk::DescriptorPoolCreateInfo().setPoolSizeCount(1)
                                                                      .setMaxSets(1)
                                                                      .setPoolSizes(descriptorPoolSize);

        auto descriptorPool = device.createDescriptorPool(descriptorPoolCreateInfo);
        
        auto descriptorSetAllocInfo = vk::DescriptorSetAllocateInfo().setDescriptorPool(descriptorPool)
                                                                     .setSetLayouts(descriptorSetLayout)
                                                                     .setDescriptorSetCount(1);
        auto descriptorSet = device.allocateDescriptorSets(descriptorSetAllocInfo);

        auto inDescriptorBufferInfo = vk::DescriptorBufferInfo().setBuffer(inBuffer)
                                                                .setOffset(0)
                                                                .setRange(bufferSize);
        auto outDescriptorBufferInfo = vk::DescriptorBufferInfo().setBuffer(outBuffer)
                                                                 .setOffset(0)
                                                                 .setRange(bufferSize);
        
        auto writeDescriptorSets = std::array<vk::WriteDescriptorSet, 2> {
            vk::WriteDescriptorSet().setBufferInfo(inDescriptorBufferInfo)
                                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                                    .setDstSet(descriptorSet[0])
                                    .setDstBinding(0),
            vk::WriteDescriptorSet().setBufferInfo(outDescriptorBufferInfo)
                                    .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                                    .setDstSet(descriptorSet[0])
                                    .setDstBinding(1),
        };

        device.updateDescriptorSets(writeDescriptorSets, {}, {});
        auto commandPoolCreateInfo = vk::CommandPoolCreateInfo().setQueueFamilyIndex(computeQueueFamilyIndex);

        auto commandPool = device.createCommandPool(commandPoolCreateInfo);
        auto commandBufferAllocInfo = vk::CommandBufferAllocateInfo().setCommandPool(commandPool)
                                                                     .setCommandBufferCount(1)
                                                                     .setLevel(vk::CommandBufferLevel::ePrimary);
        auto commandBuffer = device.allocateCommandBuffers(commandBufferAllocInfo);
        auto cmd = &commandBuffer[0];

        auto commandBufferBeginInfo = vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        
        cmd->begin(commandBufferBeginInfo);
        cmd->bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.value[0]);
        cmd->bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptorSet, nullptr);

        cmd->dispatch(bufferSize / sizeof(int32_t), 1, 1);

        cmd->end();

        vk::Queue queue = device.getQueue(computeQueueFamilyIndex, 0);
        auto submitInfo = vk::SubmitInfo().setCommandBuffers(commandBuffer);
        queue.submit(submitInfo);

        queue.waitIdle();

        payload = (uint32_t*)device.mapMemory(memory, 0, memorySize, {});

        for (uint32_t k = 0, e = bufferLength; k < e; k++) {
            if (payload[k + e] != payload[k]) {
                std::cout << "Yo what" << std::endl;
            }
        }

        device.unmapMemory(memory);

        device.destroyDescriptorSetLayout(descriptorSetLayout);
        device.destroyBuffer(inBuffer);
        device.destroyBuffer(outBuffer);
        device.freeMemory(memory);
        device.destroyCommandPool(commandPool);
        device.destroy();
        instance.destroy();
    }
    catch (vk::SystemError& err)
    {
        std::cout << "vk::SystemError: " << err.what() << std::endl;
        exit(-1);
    }
    catch (std::exception& err)
    {
        std::cout << "std::exception: " << err.what() << std::endl;
        exit(-1);
    }
    catch (...)
    {
        std::cout << "unknown error\n";
        exit(-1);
    }

    return 0;
}

