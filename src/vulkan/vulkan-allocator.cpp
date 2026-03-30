/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include "vulkan-backend.h"

namespace nvrhi::vulkan
{

    static vk::MemoryPropertyFlags pickBufferMemoryProperties(const BufferDesc& d)
    {
        vk::MemoryPropertyFlags flags{};

        switch(d.cpuAccess)
        {
        case CpuAccessMode::None:
            flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
            break;
        case CpuAccessMode::Read:
            flags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached;
            break;
        case CpuAccessMode::Write:
            flags = vk::MemoryPropertyFlagBits::eHostVisible;
            break;
        }

        return flags;
    }

    void VulkanAllocator::trackAllocation(VkDeviceMemory memory, uint64_t size, ResourceType type, bool deviceLocal)
    {
        {
            std::lock_guard<std::mutex> lock(m_StatsMutex);
            m_AllocationMap[memory] = { size, type, deviceLocal };
        }

        switch (type)
        {
        case ResourceType::Texture: m_TextureBytes += size; m_TextureCount++; break;
        case ResourceType::Buffer:  m_BufferBytes += size;  m_BufferCount++;  break;
        case ResourceType::Other:   m_OtherBytes += size;   m_OtherCount++;   break;
        }

        if (deviceLocal)
            m_DeviceLocalBytes += size;
        else
            m_HostVisibleBytes += size;
    }

    void VulkanAllocator::untrackAllocation(VkDeviceMemory memory)
    {
        AllocationRecord record;
        {
            std::lock_guard<std::mutex> lock(m_StatsMutex);
            auto it = m_AllocationMap.find(memory);
            if (it == m_AllocationMap.end())
                return;
            record = it->second;
            m_AllocationMap.erase(it);
        }

        switch (record.type)
        {
        case ResourceType::Texture: m_TextureBytes -= record.size; m_TextureCount--; break;
        case ResourceType::Buffer:  m_BufferBytes -= record.size;  m_BufferCount--;  break;
        case ResourceType::Other:   m_OtherBytes -= record.size;   m_OtherCount--;   break;
        }

        if (record.deviceLocal)
            m_DeviceLocalBytes -= record.size;
        else
            m_HostVisibleBytes -= record.size;
    }

    GPUMemoryStats VulkanAllocator::getStats() const
    {
        GPUMemoryStats stats;
        stats.TextureBytes = m_TextureBytes.load();
        stats.TextureCount = m_TextureCount.load();
        stats.BufferBytes = m_BufferBytes.load();
        stats.BufferCount = m_BufferCount.load();
        stats.OtherBytes = m_OtherBytes.load();
        stats.OtherCount = m_OtherCount.load();
        stats.DeviceLocalBytes = m_DeviceLocalBytes.load();
        stats.HostVisibleBytes = m_HostVisibleBytes.load();
        stats.TotalAllocatedBytes = stats.TextureBytes + stats.BufferBytes + stats.OtherBytes;
        stats.TotalAllocationCount = stats.TextureCount + stats.BufferCount + stats.OtherCount;
        return stats;
    }

    vk::Result VulkanAllocator::allocateBufferMemory(Buffer *buffer, bool enableDeviceAddress)
    {
        // figure out memory requirements
        vk::MemoryRequirements memRequirements;
        m_Context.device.getBufferMemoryRequirements(buffer->buffer, &memRequirements);

        // allocate memory
        const auto memPropFlags = pickBufferMemoryProperties(buffer->desc);
        const bool enableMemoryExport = (buffer->desc.sharedResourceFlags & SharedResourceFlags::Shared) != 0;
        const vk::Result res = allocateMemory(buffer, memRequirements, memPropFlags, enableDeviceAddress, enableMemoryExport, nullptr, buffer->buffer);
        CHECK_VK_RETURN(res)

        bool deviceLocal = (memPropFlags & vk::MemoryPropertyFlagBits::eDeviceLocal) != vk::MemoryPropertyFlags{};
        trackAllocation(buffer->memory, memRequirements.size, ResourceType::Buffer, deviceLocal);

        m_Context.device.bindBufferMemory(buffer->buffer, buffer->memory, 0);

        return vk::Result::eSuccess;
    }

    void VulkanAllocator::freeBufferMemory(Buffer *buffer)
    {
        untrackAllocation(buffer->memory);
        freeMemory(buffer);
    }

    vk::Result VulkanAllocator::allocateTextureMemory(Texture *texture)
    {
        // grab the image memory requirements
        vk::MemoryRequirements memRequirements;
        m_Context.device.getImageMemoryRequirements(texture->image, &memRequirements);

        // allocate memory
        const vk::MemoryPropertyFlags memProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        const bool enableDeviceAddress = false;
        const bool enableMemoryExport = (texture->desc.sharedResourceFlags & SharedResourceFlags::Shared) != 0;
        const vk::Result res = allocateMemory(texture, memRequirements, memProperties, enableDeviceAddress, enableMemoryExport, texture->image, nullptr);
        CHECK_VK_RETURN(res)

        trackAllocation(texture->memory, memRequirements.size, ResourceType::Texture, true);

        m_Context.device.bindImageMemory(texture->image, texture->memory, 0);

        return vk::Result::eSuccess;
    }

    void VulkanAllocator::freeTextureMemory(Texture *texture)
    {
        untrackAllocation(texture->memory);
        freeMemory(texture);
    }

    vk::Result VulkanAllocator::allocateMemory(MemoryResource *res,
                                               vk::MemoryRequirements memRequirements,
                                               vk::MemoryPropertyFlags memPropertyFlags,
                                                bool enableDeviceAddress,
                                                bool enableExportMemory,
                                                VkImage dedicatedImage,
                                                VkBuffer dedicatedBuffer)
    {
        res->managed = true;

        // find a memory space that satisfies the requirements
        vk::PhysicalDeviceMemoryProperties memProperties;
        m_Context.physicalDevice.getMemoryProperties(&memProperties);

        uint32_t memTypeIndex;
        for(memTypeIndex = 0; memTypeIndex < memProperties.memoryTypeCount; memTypeIndex++)
        {
            if ((memRequirements.memoryTypeBits & (1 << memTypeIndex)) &&
                ((memProperties.memoryTypes[memTypeIndex].propertyFlags & memPropertyFlags) == memPropertyFlags))
            {
                break;
            }
        }

        if (memTypeIndex == memProperties.memoryTypeCount)
        {
            // xxxnsubtil: this is incorrect; need better error reporting
            return vk::Result::eErrorOutOfDeviceMemory;
        }

        // allocate memory
        auto allocFlags = vk::MemoryAllocateFlagsInfo();
        if (enableDeviceAddress)
            allocFlags.flags |= vk::MemoryAllocateFlagBits::eDeviceAddress;
        const void* pNext = &allocFlags;

        auto dedicatedAllocation = vk::MemoryDedicatedAllocateInfo()
            .setImage(dedicatedImage)
            .setBuffer(dedicatedBuffer)
            .setPNext(pNext);

        if (dedicatedImage || dedicatedBuffer)
        {
            // Append the VkMemoryDedicatedAllocateInfo structure to the chain
            pNext = &dedicatedAllocation;
        }

#ifdef _WIN32
        const auto handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
#else
        const auto handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
#endif
        auto exportInfo = vk::ExportMemoryAllocateInfo()
            .setHandleTypes(handleType)
            .setPNext(pNext);

        if(enableExportMemory)
        {
            // Append the VkExportMemoryAllocateInfo structure to the chain
            pNext = &exportInfo;
        }

        auto allocInfo = vk::MemoryAllocateInfo()
                            .setAllocationSize(memRequirements.size)
                            .setMemoryTypeIndex(memTypeIndex)
                            .setPNext(pNext);

        return m_Context.device.allocateMemory(&allocInfo, m_Context.allocationCallbacks, &res->memory);
    }

    void VulkanAllocator::freeMemory(MemoryResource *res)
    {
        assert(res->managed);

        m_Context.device.freeMemory(res->memory, m_Context.allocationCallbacks);
        res->memory = vk::DeviceMemory(nullptr);
    }

} // namespace nvrhi::vulkan
