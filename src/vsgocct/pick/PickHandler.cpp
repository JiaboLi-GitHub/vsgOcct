#include <vsgocct/pick/PickHandler.h>

#include <cstring>

namespace vsgocct::pick
{

PickHandler::PickHandler(
    vsg::ref_ptr<vsg::Image> idImage,
    vsg::ref_ptr<vsg::Device> device,
    const scene::AssemblySceneData& sceneData)
    : _idImage(std::move(idImage))
    , _device(std::move(device))
    , _sceneData(sceneData)
{
    _imageWidth = _idImage->extent.width;
    _imageHeight = _idImage->extent.height;
}

void PickHandler::setPickCallback(PickCallback callback)
{
    _callback = std::move(callback);
}

void PickHandler::apply(vsg::ButtonPressEvent& event)
{
    if (event.button == 1)
    {
        _pickX = event.x;
        _pickY = event.y;
        _pendingPick = true;
    }
}

void PickHandler::apply(vsg::FrameEvent& /*event*/)
{
    if (_pendingPick)
    {
        _pendingPick = false;
        performReadback();
    }
}

void PickHandler::performReadback()
{
    if (!_callback) return;

    // Bounds check
    if (_pickX < 0 || _pickY < 0 ||
        static_cast<uint32_t>(_pickX) >= _imageWidth ||
        static_cast<uint32_t>(_pickY) >= _imageHeight)
    {
        scene::PickResult result;
        result.faceId = 0;
        result.faceInfo = nullptr;
        _callback(result);
        return;
    }

    const uint32_t deviceID = _device->deviceID;

    // Get a graphics queue for command submission
    auto queue = _device->getQueue(0); // queue family 0 (graphics)
    if (!queue) return;

    uint32_t queueFamilyIndex = queue->queueFamilyIndex();

    // Create a staging buffer (4 bytes is enough for one RGBA pixel)
    const VkDeviceSize bufferSize = 4;
    auto stagingBuffer = vsg::createBufferAndMemory(
        _device,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (!stagingBuffer) return;

    stagingBuffer->compile(_device);

    // Create command pool and allocate command buffer
    auto commandPool = vsg::CommandPool::create(_device, queueFamilyIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    auto commandBuffer = commandPool->allocate();

    VkCommandBuffer vkCmdBuf = commandBuffer->vk();
    VkImage vkImage = _idImage->vk(deviceID);
    VkBuffer vkBuffer = stagingBuffer->vk(deviceID);

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(vkCmdBuf, &beginInfo);

    // Transition image layout: color attachment optimal -> transfer src optimal
    VkImageMemoryBarrier preBarrier{};
    preBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    preBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    preBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    preBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    preBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    preBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preBarrier.image = vkImage;
    preBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    preBarrier.subresourceRange.baseMipLevel = 0;
    preBarrier.subresourceRange.levelCount = 1;
    preBarrier.subresourceRange.baseArrayLayer = 0;
    preBarrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        vkCmdBuf,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &preBarrier);

    // Copy 1x1 pixel region from image to staging buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {_pickX, _pickY, 0};
    region.imageExtent = {1, 1, 1};

    vkCmdCopyImageToBuffer(
        vkCmdBuf,
        vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        vkBuffer,
        1, &region);

    // Transition image layout back: transfer src optimal -> color attachment optimal
    VkImageMemoryBarrier postBarrier{};
    postBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    postBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    postBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    postBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    postBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    postBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postBarrier.image = vkImage;
    postBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    postBarrier.subresourceRange.baseMipLevel = 0;
    postBarrier.subresourceRange.levelCount = 1;
    postBarrier.subresourceRange.baseArrayLayer = 0;
    postBarrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        vkCmdBuf,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &postBarrier);

    vkEndCommandBuffer(vkCmdBuf);

    // Submit with fence and wait
    auto fence = vsg::Fence::create(_device);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = commandBuffer->data();

    queue->submit(submitInfo, fence);
    fence->wait(std::numeric_limits<uint64_t>::max());

    // Map the staging buffer and read the pixel data
    auto* deviceMemory = stagingBuffer->getDeviceMemory(deviceID);
    VkDeviceSize memoryOffset = stagingBuffer->getMemoryOffset(deviceID);

    void* mappedData = nullptr;
    deviceMemory->map(memoryOffset, bufferSize, 0, &mappedData);

    uint8_t pixel[4] = {};
    if (mappedData)
    {
        std::memcpy(pixel, mappedData, 4);
    }
    deviceMemory->unmap();

    // Decode face ID from R, G, B bytes
    uint8_t r = pixel[0];
    uint8_t g = pixel[1];
    uint8_t b = pixel[2];
    uint32_t faceId = decodeFaceIdFromBytes(r, g, b);

    // Look up in face registry
    scene::PickResult result;
    result.faceId = faceId;
    result.faceInfo = nullptr;

    auto it = _sceneData.faceRegistry.find(faceId);
    if (it != _sceneData.faceRegistry.end())
    {
        result.faceInfo = &it->second;
    }

    _callback(result);
}

} // namespace vsgocct::pick
