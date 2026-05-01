#include "VideoEditor.hpp"

#include "Renderer/VulkanContext.hpp"
#include "Video/VideoPlayer.hpp"

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <nfd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

namespace eden {

VideoEditor::VideoEditor(VulkanContext& ctx) : m_ctx(ctx) {}

VideoEditor::~VideoEditor() {
    closeVideo();
    destroyPreviewTexture();
}

void VideoEditor::closeVideo() {
    if (m_player) m_player->close();
    m_player.reset();
    m_videoPath.clear();
    m_trimStart = 0.0f;
    m_trimEnd = 0.0f;
    m_playing = false;
    m_imageReady = false;
}

void VideoEditor::loadVideo(const std::string& path) {
    closeVideo();
    m_player = std::make_unique<VideoPlayer>();
    if (!m_player->open(path)) {
        m_status = "Open failed: " + m_player->errorMessage();
        m_player.reset();
        return;
    }
    m_videoPath = path;
    m_trimStart = 0.0f;
    m_trimEnd   = m_player->duration();
    m_playing   = false;
    m_player->pause();   // start paused so the user scrubs/inspects first
    ensurePreviewTexture(m_player->width(), m_player->height());
    if (m_player->hasFrame()) {
        uploadPreviewFrame(m_player->currentFrame().data());
    }
    m_status = "Loaded " + path;
}

void VideoEditor::destroyPreviewTexture() {
    VkDevice device = m_ctx.getDevice();
    if (m_imguiDescriptor) { ImGui_ImplVulkan_RemoveTexture(m_imguiDescriptor); m_imguiDescriptor = VK_NULL_HANDLE; }
    if (m_sampler)         { vkDestroySampler(device, m_sampler, nullptr);     m_sampler = VK_NULL_HANDLE; }
    if (m_imageView)       { vkDestroyImageView(device, m_imageView, nullptr); m_imageView = VK_NULL_HANDLE; }
    if (m_image)           { vkDestroyImage(device, m_image, nullptr);          m_image = VK_NULL_HANDLE; }
    if (m_imageMemory)     { vkFreeMemory(device, m_imageMemory, nullptr);      m_imageMemory = VK_NULL_HANDLE; }
    if (m_stagingMapped)   { vkUnmapMemory(device, m_stagingMem); m_stagingMapped = nullptr; }
    if (m_stagingBuf)      { vkDestroyBuffer(device, m_stagingBuf, nullptr);    m_stagingBuf = VK_NULL_HANDLE; }
    if (m_stagingMem)      { vkFreeMemory(device, m_stagingMem, nullptr);       m_stagingMem = VK_NULL_HANDLE; }
    m_imageW = m_imageH = 0;
    m_stagingSize = 0;
    m_imageReady = false;
}

void VideoEditor::ensurePreviewTexture(int w, int h) {
    if (m_image && m_imageW == w && m_imageH == h) return;
    destroyPreviewTexture();

    VkDevice device = m_ctx.getDevice();
    m_imageW = w;
    m_imageH = h;
    m_stagingSize = static_cast<size_t>(w) * h * 4;

    // Persistently mapped staging buffer for fast per-frame upload.
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size  = m_stagingSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device, &bufInfo, nullptr, &m_stagingBuf);

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, m_stagingBuf, &memReq);
    VkMemoryAllocateInfo memInfo{};
    memInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memInfo.allocationSize = memReq.size;
    memInfo.memoryTypeIndex = m_ctx.findMemoryType(memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device, &memInfo, nullptr, &m_stagingMem);
    vkBindBufferMemory(device, m_stagingBuf, m_stagingMem, 0);
    vkMapMemory(device, m_stagingMem, 0, m_stagingSize, 0, &m_stagingMapped);

    // Device-local image we'll cmdCopyBuffer into each frame.
    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateImage(device, &imgInfo, nullptr, &m_image);

    VkMemoryRequirements imgReq;
    vkGetImageMemoryRequirements(device, m_image, &imgReq);
    VkMemoryAllocateInfo imgMemInfo{};
    imgMemInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imgMemInfo.allocationSize = imgReq.size;
    imgMemInfo.memoryTypeIndex = m_ctx.findMemoryType(imgReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device, &imgMemInfo, nullptr, &m_imageMemory);
    vkBindImageMemory(device, m_image, m_imageMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(device, &viewInfo, nullptr, &m_imageView);

    VkSamplerCreateInfo sampInfo{};
    sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampInfo.magFilter = VK_FILTER_LINEAR;
    sampInfo.minFilter = VK_FILTER_LINEAR;
    sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    vkCreateSampler(device, &sampInfo, nullptr, &m_sampler);

    m_imguiDescriptor = ImGui_ImplVulkan_AddTexture(m_sampler, m_imageView,
                                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Initial transition: UNDEFINED -> SHADER_READ_ONLY so the descriptor
    // is valid before the first frame upload. Subsequent uploads do the
    // SHADER_READ_ONLY <-> TRANSFER_DST round-trip.
    VkCommandBuffer cmd = m_ctx.beginSingleTimeCommands();
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = m_image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    m_ctx.endSingleTimeCommands(cmd);
}

void VideoEditor::uploadPreviewFrame(const unsigned char* rgba) {
    if (!m_image || !m_stagingMapped) return;
    std::memcpy(m_stagingMapped, rgba, m_stagingSize);

    VkCommandBuffer cmd = m_ctx.beginSingleTimeCommands();

    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = m_image;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {static_cast<uint32_t>(m_imageW), static_cast<uint32_t>(m_imageH), 1};
    vkCmdCopyBufferToImage(cmd, m_stagingBuf, m_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toShader = toDst;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShader);

    m_ctx.endSingleTimeCommands(cmd);
    m_imageReady = true;
}

void VideoEditor::update(float deltaTime) {
    if (!m_player) return;
    if (m_playing) m_player->play(); else m_player->pause();
    m_player->update(deltaTime, /*loop=*/true);
    if (m_player->frameJustChanged()) {
        uploadPreviewFrame(m_player->currentFrame().data());
    }
}

void VideoEditor::exportTrimmedClip(const std::string& outputPath) {
    if (!m_player || m_videoPath.empty()) return;

    float startT = std::max(0.0f, m_trimStart);
    float endT   = (m_trimEnd > startT) ? m_trimEnd : m_player->duration();
    char startBuf[32], endBuf[32];
    std::snprintf(startBuf, sizeof(startBuf), "%.3f", startT);
    std::snprintf(endBuf,   sizeof(endBuf),   "%.3f", endT);

    // Stream-copy first (lossless, instant). If you find the cut isn't on
    // a keyframe (audible/visible glitch at the start), swap -c copy for
    // "-c:v libx264 -preset veryfast -crf 18 -c:a aac" to re-encode.
    std::string cmd = "ffmpeg -y -loglevel error";
    cmd += " -ss " + std::string(startBuf);
    cmd += " -to " + std::string(endBuf);
    cmd += " -i \"" + m_videoPath + "\"";
    cmd += " -c copy";
    cmd += " \"" + outputPath + "\"";

    m_status = "Exporting...";
    int rc = std::system(cmd.c_str());
    if (rc == 0) m_status = "Saved: " + outputPath;
    else         m_status = "ffmpeg failed (rc=" + std::to_string(rc) + ")";
    std::cout << "[VideoEditor] " << m_status << std::endl;
}

void VideoEditor::renderUI(bool* visible) {
    if (!visible || !*visible) return;

    ImGui::SetNextWindowSize(ImVec2(640, 580), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Video Editor", visible)) {
        ImGui::End();
        return;
    }

    // Load row.
    if (ImGui::Button("Load Video...")) {
        nfdchar_t* outPath = nullptr;
        nfdfilteritem_t filters[1] = {{"Video", "mp4,mov,mkv,webm,avi"}};
        const char* defaultDir = m_defaultDir.empty() ? nullptr : m_defaultDir.c_str();
        if (NFD_OpenDialog(&outPath, filters, 1, defaultDir) == NFD_OKAY) {
            loadVideo(outPath);
            NFD_FreePath(outPath);
        }
    }
    ImGui::SameLine();
    if (m_player) {
        ImGui::TextDisabled("%s  (%dx%d, %.2fs)",
                            m_videoPath.c_str(), m_player->width(),
                            m_player->height(), m_player->duration());
    } else {
        ImGui::TextDisabled("(no video loaded)");
    }
    ImGui::Separator();

    // Preview image.
    if (m_player && m_imguiDescriptor && m_imageW > 0 && m_imageH > 0) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        // Reserve ~150 px below for transport + trim + export rows.
        float reserveBottom = 160.0f;
        float maxW = avail.x;
        float maxH = std::max(80.0f, avail.y - reserveBottom);
        float aspect = static_cast<float>(m_imageW) / static_cast<float>(m_imageH);
        float w = maxW, h = w / aspect;
        if (h > maxH) { h = maxH; w = h * aspect; }
        ImVec2 cur = ImGui::GetCursorScreenPos();
        cur.x += (maxW - w) * 0.5f;
        ImGui::SetCursorScreenPos(cur);
        ImGui::Image((ImTextureID)m_imguiDescriptor, ImVec2(w, h));
    } else {
        ImGui::Dummy(ImVec2(1, 200));
        ImGui::TextDisabled("Load a video to preview.");
    }

    ImGui::Separator();

    // Transport row.
    if (m_player) {
        if (ImGui::Button(m_playing ? "Pause##vid" : "Play##vid", ImVec2(70, 0))) {
            m_playing = !m_playing;
        }
        ImGui::SameLine();
        if (ImGui::Button("|<")) {
            m_player->seek(0.0f);
            m_playing = false;
        }
        ImGui::SameLine();
        ImGui::Text("%.2f / %.2fs", m_player->currentTime(), m_player->duration());

        // Scrub bar.
        float t = m_player->currentTime();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##scrub", &t, 0.0f, m_player->duration(), "%.2fs")) {
            m_player->seek(t);
            m_playing = false;
            // Force-upload the seeked frame so the preview catches up
            // even when paused (frameJustChanged may have already fired
            // during seek).
            if (m_player->hasFrame()) {
                uploadPreviewFrame(m_player->currentFrame().data());
            }
        }

        ImGui::Separator();

        // Trim row.
        if (ImGui::Button("Set Start")) m_trimStart = m_player->currentTime();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("##start", &m_trimStart, 0.05f, 0.0f, m_player->duration(), "Start %.2fs");

        ImGui::SameLine();
        if (ImGui::Button("Set End")) m_trimEnd = m_player->currentTime();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("##end", &m_trimEnd, 0.05f, 0.0f, m_player->duration(), "End %.2fs");

        ImGui::SameLine();
        if (ImGui::Button("Reset Trim")) {
            m_trimStart = 0.0f;
            m_trimEnd = m_player->duration();
        }

        // Range guard.
        if (m_trimEnd <= m_trimStart) m_trimEnd = std::min(m_trimStart + 0.1f, m_player->duration());

        ImGui::Text("Clip length: %.2fs", std::max(0.0f, m_trimEnd - m_trimStart));

        // Export row.
        if (ImGui::Button("Export Clip...")) {
            nfdchar_t* outPath = nullptr;
            nfdfilteritem_t filters[1] = {{"Video", "mp4"}};
            const char* defaultDir = m_defaultDir.empty() ? nullptr : m_defaultDir.c_str();
            if (NFD_SaveDialog(&outPath, filters, 1, defaultDir, "clip.mp4") == NFD_OKAY) {
                std::string path = outPath;
                NFD_FreePath(outPath);
                if (path.size() < 4 || path.substr(path.size() - 4) != ".mp4") path += ".mp4";
                exportTrimmedClip(path);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Stream-copy via ffmpeg (-c copy). Lossless, instant.\n"
                              "Cuts snap to nearest keyframe; if the start glitches,\n"
                              "tell me and I'll switch to re-encode.");
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("%s", m_status.c_str());

    ImGui::End();
}

} // namespace eden
