#include "ImageReferences.hpp"
#include "../Renderer/VulkanContext.hpp"

#include <stb_image.h>
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <nfd.h>

#include <filesystem>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace eden {

ImageReferences::~ImageReferences() {
    cleanup();
}

void ImageReferences::init(VulkanContext& context) {
    m_context = &context;
}

void ImageReferences::cleanup() {
    for (auto& img : m_images) destroyImage(img);
    m_images.clear();
    m_context = nullptr;
}

// ── Image loading ──────────────────────────────────────────────────────

bool ImageReferences::loadImage(const std::string& path) {
    if (!m_context) return false;

    int w, h, channels;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        std::cerr << "[ImageReferences] Failed to load: " << path << std::endl;
        return false;
    }

    std::string name = std::filesystem::path(path).filename().string();
    RefImage img = uploadImage(pixels, w, h, name, path);
    stbi_image_free(pixels);

    if (img.descriptor) {
        m_images.push_back(std::move(img));
        m_selectedTab = static_cast<int>(m_images.size()) - 1;
        return true;
    }
    return false;
}

// ── Vulkan texture upload ──────────────────────────────────────────────

ImageReferences::RefImage ImageReferences::uploadImage(const unsigned char* rgba, int width, int height,
                                                        const std::string& name, const std::string& path) {
    RefImage img;
    img.name = name;
    img.filepath = path;
    img.width = width;
    img.height = height;

    VkDevice device = m_context->getDevice();
    VkDeviceSize imageSize = width * height * 4;

    // Staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    m_context->createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingMemory);

    void* data;
    vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data);
    memcpy(data, rgba, imageSize);
    vkUnmapMemory(device, stagingMemory);

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(device, &imageInfo, nullptr, &img.image);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, img.image, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = m_context->findMemoryType(memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device, &allocInfo, nullptr, &img.memory);
    vkBindImageMemory(device, img.image, img.memory, 0);

    // Transition + copy
    VkCommandBuffer cmd = m_context->beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = img.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, img.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    m_context->endSingleTimeCommands(cmd);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    // View + sampler
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = img.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(device, &viewInfo, nullptr, &img.view);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &samplerInfo, nullptr, &img.sampler);

    img.descriptor = ImGui_ImplVulkan_AddTexture(img.sampler, img.view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    return img;
}

void ImageReferences::destroyImage(RefImage& img) {
    if (!m_context) return;
    VkDevice device = m_context->getDevice();
    m_context->waitIdle();
    if (img.descriptor) ImGui_ImplVulkan_RemoveTexture(img.descriptor);
    if (img.sampler) vkDestroySampler(device, img.sampler, nullptr);
    if (img.view) vkDestroyImageView(device, img.view, nullptr);
    if (img.image) vkDestroyImage(device, img.image, nullptr);
    if (img.memory) vkFreeMemory(device, img.memory, nullptr);
    img = {};
}

// ── ImGui rendering ────────────────────────────────────────────────────

void ImageReferences::render(bool* open) {
    if (open && !*open) return;

    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Image References", open)) {
        ImGui::End();
        return;
    }

    // Load button
    if (ImGui::Button("Load Image...")) {
        nfdchar_t* outPath = nullptr;
        nfdfilteritem_t filters[1] = { { "Images", "png,jpg,jpeg,bmp,tga" } };
        nfdresult_t result = NFD_OpenDialog(&outPath, filters, 1, nullptr);
        if (result == NFD_OKAY && outPath) {
            loadImage(outPath);
            NFD_FreePath(outPath);
        }
    }

    if (m_images.empty()) {
        ImGui::TextDisabled("No images loaded. Click 'Load Image...' to add reference images.");
        ImGui::End();
        return;
    }

    // Tabs for each loaded image
    if (ImGui::BeginTabBar("##imagetabs")) {
        // Track which tab to remove (if any)
        int removeIdx = -1;

        for (int i = 0; i < static_cast<int>(m_images.size()); i++) {
            bool tabOpen = true;
            ImGuiTabItemFlags flags = 0;
            if (i == m_selectedTab) flags |= ImGuiTabItemFlags_SetSelected;

            if (ImGui::BeginTabItem(m_images[i].name.c_str(), &tabOpen, flags)) {
                m_selectedTab = i;
                auto& img = m_images[i];

                // Info + controls bar
                ImGui::Text("%dx%d  Zoom: %.0f%%", img.width, img.height, img.zoom * 100.0f);
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset View")) {
                    img.zoom = 1.0f;
                    img.panX = 0.0f;
                    img.panY = 0.0f;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Fit")) {
                    // Calculate zoom to fit image in canvas
                    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
                    if (canvasSize.x > 0 && canvasSize.y > 0 && img.width > 0 && img.height > 0) {
                        float scaleX = canvasSize.x / static_cast<float>(img.width);
                        float scaleY = canvasSize.y / static_cast<float>(img.height);
                        img.zoom = std::min(scaleX, scaleY);
                        img.panX = 0.0f;
                        img.panY = 0.0f;
                    }
                }

                // Canvas area for the image
                ImVec2 canvasPos = ImGui::GetCursorScreenPos();
                ImVec2 canvasSize = ImGui::GetContentRegionAvail();
                if (canvasSize.y < 50) canvasSize.y = 50;

                // Invisible button to capture input over the canvas
                ImGui::InvisibleButton("##canvas", canvasSize);
                bool isHovered = ImGui::IsItemHovered();
                bool isActive = ImGui::IsItemActive();

                // Draw background
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(canvasPos,
                    ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                    IM_COL32(30, 30, 30, 255));

                // Calculate display size maintaining aspect ratio
                float imgAspect = static_cast<float>(img.width) / img.height;
                float displayW = img.width * img.zoom;
                float displayH = img.height * img.zoom;

                // Center image + apply pan
                float imgX = canvasPos.x + (canvasSize.x - displayW) * 0.5f + img.panX;
                float imgY = canvasPos.y + (canvasSize.y - displayH) * 0.5f + img.panY;

                // Clip to canvas
                drawList->PushClipRect(canvasPos,
                    ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

                // Draw the image
                if (img.descriptor) {
                    drawList->AddImage(
                        (ImTextureID)img.descriptor,
                        ImVec2(imgX, imgY),
                        ImVec2(imgX + displayW, imgY + displayH));
                }

                drawList->PopClipRect();

                // Pan with middle mouse button OR left mouse button
                if (isHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    img.panX += delta.x;
                    img.panY += delta.y;
                } else if (isActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    img.panX += delta.x;
                    img.panY += delta.y;
                }

                // Zoom with mouse wheel (zoom toward cursor)
                if (isHovered) {
                    float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0f) {
                        float oldZoom = img.zoom;
                        float zoomFactor = 1.0f + wheel * 0.1f;
                        img.zoom = std::clamp(img.zoom * zoomFactor, 0.05f, 20.0f);

                        // Scale pan so the image point under cursor stays put
                        float ratio = img.zoom / oldZoom;
                        ImVec2 mousePos = ImGui::GetIO().MousePos;
                        float mx = mousePos.x - (canvasPos.x + canvasSize.x * 0.5f);
                        float my = mousePos.y - (canvasPos.y + canvasSize.y * 0.5f);
                        img.panX = mx - ratio * (mx - img.panX);
                        img.panY = my - ratio * (my - img.panY);
                    }
                }

                ImGui::EndTabItem();
            }

            if (!tabOpen) {
                removeIdx = i;
            }
        }

        ImGui::EndTabBar();

        // Remove closed tab
        if (removeIdx >= 0) {
            destroyImage(m_images[removeIdx]);
            m_images.erase(m_images.begin() + removeIdx);
            if (m_selectedTab >= static_cast<int>(m_images.size())) {
                m_selectedTab = static_cast<int>(m_images.size()) - 1;
            }
        }
    }

    ImGui::End();
}

} // namespace eden
