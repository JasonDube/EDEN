#include "TextureBrowser.hpp"
#include "../Renderer/VulkanContext.hpp"
#include "../Renderer/Buffer.hpp"

#include <stb_image.h>
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <cmath>

// BC7 decoder — single-header library
#define BCDEC_IMPLEMENTATION
#include "../Renderer/bcdec.h"

namespace eden {

TextureBrowser::~TextureBrowser() {
    cleanup();
}

void TextureBrowser::init(VulkanContext& context) {
    m_context = &context;
}

void TextureBrowser::cleanup() {
    freePage();
    m_context = nullptr;
}

void TextureBrowser::setDirectory(const std::string& path) {
    if (m_directory != path) {
        m_directory = path;
        std::strncpy(m_pathInput, path.c_str(), sizeof(m_pathInput) - 1);
        m_pathInput[sizeof(m_pathInput) - 1] = '\0';
        m_needsRescan = true;
    }
}

// ── Directory scanning ─────────────────────────────────────────────────

void TextureBrowser::scanDirectory() {
    m_allEntries.clear();
    m_filteredIndices.clear();
    m_selectedIndex = -1;
    m_selectedPath.clear();
    m_currentPage = 0;

    if (m_directory.empty() || !std::filesystem::exists(m_directory)) return;

    // Add parent directory entry
    if (m_directory != "/" && m_directory.size() > 1) {
        FileEntry parent;
        parent.filename = "..";
        parent.fullPath = std::filesystem::path(m_directory).parent_path().string();
        parent.isDirectory = true;
        m_allEntries.push_back(std::move(parent));
    }

    try {
        std::vector<FileEntry> dirs, files;
        for (const auto& entry : std::filesystem::directory_iterator(m_directory)) {
            FileEntry fe;
            fe.filename = entry.path().filename().string();
            fe.fullPath = entry.path().string();

            if (entry.is_directory()) {
                fe.isDirectory = true;
                dirs.push_back(std::move(fe));
            } else {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".dds" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                    ext == ".bmp" || ext == ".tga") {
                    fe.isDirectory = false;
                    files.push_back(std::move(fe));
                }
            }
        }

        // Sort alphabetically
        auto cmp = [](const FileEntry& a, const FileEntry& b) { return a.filename < b.filename; };
        std::sort(dirs.begin(), dirs.end(), cmp);
        std::sort(files.begin(), files.end(), cmp);

        for (auto& d : dirs) m_allEntries.push_back(std::move(d));
        for (auto& f : files) m_allEntries.push_back(std::move(f));
    } catch (const std::exception& e) {
        std::cerr << "[TextureBrowser] scan error: " << e.what() << std::endl;
    }

    // Build filtered indices (all match initially)
    for (int i = 0; i < static_cast<int>(m_allEntries.size()); i++) {
        m_filteredIndices.push_back(i);
    }

    m_needsPageLoad = true;
}

// ── DDS thumbnail loading ──────────────────────────────────────────────

// DDS header structures
#pragma pack(push, 1)
struct DDSPixelFormat {
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rBitMask, gBitMask, bBitMask, aBitMask;
};

struct DDSHeader {
    uint32_t magic;         // 'DDS '
    uint32_t size;          // 124
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth;
    uint32_t mipMapCount;
    uint32_t reserved1[11];
    DDSPixelFormat pixelFormat;
    uint32_t caps1;
    uint32_t caps2;
    uint32_t reserved2[3];
};

struct DX10Header {
    uint32_t dxgiFormat;
    uint32_t resourceDimension;
    uint32_t miscFlag;
    uint32_t arraySize;
    uint32_t miscFlags2;
};
#pragma pack(pop)

static constexpr uint32_t DXGI_FORMAT_BC7_UNORM      = 98;
static constexpr uint32_t DXGI_FORMAT_BC7_UNORM_SRGB  = 99;
static constexpr uint32_t DXGI_FORMAT_BC1_UNORM       = 71;
static constexpr uint32_t DXGI_FORMAT_BC1_UNORM_SRGB  = 72;
static constexpr uint32_t DXGI_FORMAT_BC3_UNORM       = 77;
static constexpr uint32_t DXGI_FORMAT_BC3_UNORM_SRGB  = 78;

static uint32_t makeFourCC(char a, char b, char c, char d) {
    return static_cast<uint32_t>(a) |
           (static_cast<uint32_t>(b) << 8) |
           (static_cast<uint32_t>(c) << 16) |
           (static_cast<uint32_t>(d) << 24);
}

bool TextureBrowser::loadDDSThumbnail(const std::string& path,
                                       std::vector<unsigned char>& rgbaOut,
                                       int& outWidth, int& outHeight) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    DDSHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || header.magic != makeFourCC('D', 'D', 'S', ' ')) return false;

    bool hasDX10 = false;
    DX10Header dx10{};
    uint32_t dxgiFormat = 0;

    // Check for DX10 extended header
    if (header.pixelFormat.fourCC == makeFourCC('D', 'X', '1', '0')) {
        file.read(reinterpret_cast<char*>(&dx10), sizeof(dx10));
        if (!file) return false;
        hasDX10 = true;
        dxgiFormat = dx10.dxgiFormat;
    }

    // Determine block format and size
    int blockBytes = 0;
    enum BCFormat { BC_NONE, BC_1, BC_3, BC_7 } bcFormat = BC_NONE;

    if (hasDX10) {
        if (dxgiFormat == DXGI_FORMAT_BC7_UNORM || dxgiFormat == DXGI_FORMAT_BC7_UNORM_SRGB) {
            bcFormat = BC_7;
            blockBytes = BCDEC_BC7_BLOCK_SIZE;
        } else if (dxgiFormat == DXGI_FORMAT_BC1_UNORM || dxgiFormat == DXGI_FORMAT_BC1_UNORM_SRGB) {
            bcFormat = BC_1;
            blockBytes = BCDEC_BC1_BLOCK_SIZE;
        } else if (dxgiFormat == DXGI_FORMAT_BC3_UNORM || dxgiFormat == DXGI_FORMAT_BC3_UNORM_SRGB) {
            bcFormat = BC_3;
            blockBytes = BCDEC_BC3_BLOCK_SIZE;
        }
    } else {
        uint32_t fcc = header.pixelFormat.fourCC;
        if (fcc == makeFourCC('D', 'X', 'T', '1')) {
            bcFormat = BC_1;
            blockBytes = BCDEC_BC1_BLOCK_SIZE;
        } else if (fcc == makeFourCC('D', 'X', 'T', '5')) {
            bcFormat = BC_3;
            blockBytes = BCDEC_BC3_BLOCK_SIZE;
        }
    }

    if (bcFormat == BC_NONE) return false;

    uint32_t w = header.width;
    uint32_t h = header.height;
    uint32_t mipCount = header.mipMapCount > 0 ? header.mipMapCount : 1;

    // Find a small mip level for thumbnail (target ~16x16 or whatever is smallest >=4x4)
    // We seek past larger mips to find a small one
    size_t dataOffset = sizeof(DDSHeader) + (hasDX10 ? sizeof(DX10Header) : 0);
    uint32_t mipW = w, mipH = h;
    int targetMip = -1;

    // Find the mip level we want (prefer 16x16, but accept 4x4 up to 64x64)
    for (uint32_t m = 0; m < mipCount; m++) {
        if (mipW <= 64 && mipH <= 64 && targetMip < 0) {
            targetMip = static_cast<int>(m);
            break;
        }
        uint32_t bw = (mipW + 3) / 4;
        uint32_t bh = (mipH + 3) / 4;
        dataOffset += bw * bh * blockBytes;
        mipW = std::max(1u, mipW / 2);
        mipH = std::max(1u, mipH / 2);
    }

    if (targetMip < 0) {
        // No small mip found, just use mip 0 but cap read
        dataOffset = sizeof(DDSHeader) + (hasDX10 ? sizeof(DX10Header) : 0);
        mipW = w;
        mipH = h;
        if (mipW > 256 || mipH > 256) return false; // Too large without small mips
    }

    uint32_t blocksW = (mipW + 3) / 4;
    uint32_t blocksH = (mipH + 3) / 4;
    size_t compressedSize = blocksW * blocksH * blockBytes;

    std::vector<unsigned char> compressedData(compressedSize);
    file.seekg(dataOffset);
    file.read(reinterpret_cast<char*>(compressedData.data()), compressedSize);
    if (!file) return false;

    // Decode blocks into RGBA
    // Pixel dimensions rounded up to multiple of 4 for block decoding
    uint32_t pixW = blocksW * 4;
    uint32_t pixH = blocksH * 4;
    std::vector<unsigned char> decoded(pixW * pixH * 4);

    for (uint32_t by = 0; by < blocksH; by++) {
        for (uint32_t bx = 0; bx < blocksW; bx++) {
            const unsigned char* block = compressedData.data() + (by * blocksW + bx) * blockBytes;
            unsigned char blockPixels[4 * 4 * 4]; // 4x4 pixels * 4 bytes RGBA

            switch (bcFormat) {
                case BC_7:
                    bcdec_bc7(block, blockPixels, 4 * 4);
                    break;
                case BC_3:
                    bcdec_bc3(block, blockPixels, 4 * 4);
                    break;
                case BC_1:
                    bcdec_bc1(block, blockPixels, 4 * 4);
                    break;
                default:
                    break;
            }

            // Copy 4x4 block into decoded image
            for (int py = 0; py < 4; py++) {
                for (int px = 0; px < 4; px++) {
                    uint32_t dx = bx * 4 + px;
                    uint32_t dy = by * 4 + py;
                    if (dx < mipW && dy < mipH) {
                        int srcIdx = (py * 4 + px) * 4;
                        int dstIdx = (dy * pixW + dx) * 4;
                        decoded[dstIdx + 0] = blockPixels[srcIdx + 0];
                        decoded[dstIdx + 1] = blockPixels[srcIdx + 1];
                        decoded[dstIdx + 2] = blockPixels[srcIdx + 2];
                        decoded[dstIdx + 3] = blockPixels[srcIdx + 3];
                    }
                }
            }
        }
    }

    outWidth = static_cast<int>(mipW);
    outHeight = static_cast<int>(mipH);
    rgbaOut.assign(decoded.begin(), decoded.begin() + mipW * mipH * 4);

    // Wait — the decoded buffer has pixW stride, need to compact
    rgbaOut.resize(mipW * mipH * 4);
    for (uint32_t y = 0; y < mipH; y++) {
        memcpy(rgbaOut.data() + y * mipW * 4,
               decoded.data() + y * pixW * 4,
               mipW * 4);
    }

    return true;
}

bool TextureBrowser::loadDDS(const std::string& path,
                              std::vector<unsigned char>& rgbaOut,
                              int& outWidth, int& outHeight) {
    return loadDDSThumbnail(path, rgbaOut, outWidth, outHeight);
}

// ── stb_image thumbnail loading ────────────────────────────────────────

bool TextureBrowser::loadStbiThumbnail(const std::string& path,
                                        std::vector<unsigned char>& rgbaOut,
                                        int& outWidth, int& outHeight) {
    int w, h, channels;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) return false;

    outWidth = w;
    outHeight = h;
    rgbaOut.assign(pixels, pixels + w * h * 4);
    stbi_image_free(pixels);
    return true;
}

// ── Vulkan thumbnail upload ────────────────────────────────────────────

TextureBrowser::Thumbnail TextureBrowser::uploadThumbnail(const unsigned char* rgba,
                                                           int width, int height) {
    Thumbnail thumb;
    if (!m_context) return thumb;

    VkDevice device = m_context->getDevice();

    // Resample to THUMB_SIZE x THUMB_SIZE
    std::vector<unsigned char> resized(THUMB_SIZE * THUMB_SIZE * 4);
    for (int ty = 0; ty < THUMB_SIZE; ty++) {
        for (int tx = 0; tx < THUMB_SIZE; tx++) {
            int sx = tx * width / THUMB_SIZE;
            int sy = ty * height / THUMB_SIZE;
            sx = std::min(sx, width - 1);
            sy = std::min(sy, height - 1);
            int srcIdx = (sy * width + sx) * 4;
            int dstIdx = (ty * THUMB_SIZE + tx) * 4;
            resized[dstIdx + 0] = rgba[srcIdx + 0];
            resized[dstIdx + 1] = rgba[srcIdx + 1];
            resized[dstIdx + 2] = rgba[srcIdx + 2];
            resized[dstIdx + 3] = rgba[srcIdx + 3];
        }
    }

    VkDeviceSize imageSize = THUMB_SIZE * THUMB_SIZE * 4;

    // Staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    m_context->createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingMemory);

    void* data;
    vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data);
    memcpy(data, resized.data(), imageSize);
    vkUnmapMemory(device, stagingMemory);

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent = {THUMB_SIZE, THUMB_SIZE, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(device, &imageInfo, nullptr, &thumb.image);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, thumb.image, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = m_context->findMemoryType(memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device, &allocInfo, nullptr, &thumb.memory);
    vkBindImageMemory(device, thumb.image, thumb.memory, 0);

    // Transition + copy
    VkCommandBuffer cmd = m_context->beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = thumb.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {THUMB_SIZE, THUMB_SIZE, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, thumb.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    m_context->endSingleTimeCommands(cmd);

    // Cleanup staging
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    // Create view + sampler
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = thumb.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(device, &viewInfo, nullptr, &thumb.view);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &samplerInfo, nullptr, &thumb.sampler);

    thumb.descriptor = ImGui_ImplVulkan_AddTexture(thumb.sampler, thumb.view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    return thumb;
}

void TextureBrowser::destroyThumbnail(Thumbnail& thumb) {
    if (!m_context) return;
    VkDevice device = m_context->getDevice();
    if (thumb.descriptor) ImGui_ImplVulkan_RemoveTexture(thumb.descriptor);
    if (thumb.sampler) vkDestroySampler(device, thumb.sampler, nullptr);
    if (thumb.view) vkDestroyImageView(device, thumb.view, nullptr);
    if (thumb.image) vkDestroyImage(device, thumb.image, nullptr);
    if (thumb.memory) vkFreeMemory(device, thumb.memory, nullptr);
    thumb = {};
}

// ── Page loading ───────────────────────────────────────────────────────

void TextureBrowser::freePage() {
    for (auto& t : m_thumbnails) destroyThumbnail(t);
    m_thumbnails.clear();
}

void TextureBrowser::loadPage(int page) {
    freePage();
    if (!m_context) return;

    int start = page * ENTRIES_PER_PAGE;
    int end = std::min(start + ENTRIES_PER_PAGE, static_cast<int>(m_filteredIndices.size()));

    m_thumbnails.resize(end - start);

    for (int i = start; i < end; i++) {
        int entryIdx = m_filteredIndices[i];
        const auto& entry = m_allEntries[entryIdx];

        if (entry.isDirectory) {
            m_thumbnails[i - start] = {}; // No thumbnail for directories
            continue;
        }

        std::vector<unsigned char> rgba;
        int w = 0, h = 0;
        bool loaded = false;

        std::string ext = std::filesystem::path(entry.fullPath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".dds") {
            loaded = loadDDSThumbnail(entry.fullPath, rgba, w, h);
        } else {
            loaded = loadStbiThumbnail(entry.fullPath, rgba, w, h);
        }

        if (loaded && w > 0 && h > 0) {
            m_thumbnails[i - start] = uploadThumbnail(rgba.data(), w, h);
        }
    }
}

// ── ImGui rendering ────────────────────────────────────────────────────

void TextureBrowser::render(bool* open) {
    if (open && !*open) return;

    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Texture Browser", open)) {
        ImGui::End();
        return;
    }

    // Path bar
    ImGui::SetNextItemWidth(-120);
    if (ImGui::InputText("##path", m_pathInput, sizeof(m_pathInput),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        m_directory = m_pathInput;
        m_needsRescan = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse", ImVec2(110, 0))) {
        m_directory = m_pathInput;
        m_needsRescan = true;
    }

    // Search filter
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##search", m_searchFilter, sizeof(m_searchFilter))) {
        // Rebuild filtered indices
        m_filteredIndices.clear();
        std::string filter(m_searchFilter);
        std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

        for (int i = 0; i < static_cast<int>(m_allEntries.size()); i++) {
            if (filter.empty()) {
                m_filteredIndices.push_back(i);
            } else {
                std::string lower = m_allEntries[i].filename;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower.find(filter) != std::string::npos) {
                    m_filteredIndices.push_back(i);
                }
            }
        }
        m_currentPage = 0;
        m_needsPageLoad = true;
    }
    if (strlen(m_searchFilter) == 0) {
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - ImGui::CalcTextSize("Search...").x - 16);
    }

    // Handle rescan
    if (m_needsRescan) {
        m_needsRescan = false;
        scanDirectory();
    }

    // Handle page load
    if (m_needsPageLoad) {
        m_needsPageLoad = false;
        loadPage(m_currentPage);
    }

    ImGui::Separator();

    // Info bar
    int totalFiltered = static_cast<int>(m_filteredIndices.size());
    int totalPages = std::max(1, (totalFiltered + ENTRIES_PER_PAGE - 1) / ENTRIES_PER_PAGE);
    ImGui::Text("%d items | Page %d/%d", totalFiltered, m_currentPage + 1, totalPages);

    ImGui::SameLine();
    if (ImGui::Button("<") && m_currentPage > 0) {
        m_currentPage--;
        m_needsPageLoad = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(">") && m_currentPage < totalPages - 1) {
        m_currentPage++;
        m_needsPageLoad = true;
    }

    ImGui::Separator();

    // Thumbnail grid
    ImGui::BeginChild("##thumbs", ImVec2(0, -30), false, ImGuiWindowFlags_HorizontalScrollbar);

    const float thumbSize = static_cast<float>(THUMB_SIZE);
    const float spacing = 8.0f;
    float windowWidth = ImGui::GetContentRegionAvail().x;
    int columns = std::max(1, static_cast<int>(windowWidth / (thumbSize + spacing)));

    int pageStart = m_currentPage * ENTRIES_PER_PAGE;
    int pageEnd = std::min(pageStart + ENTRIES_PER_PAGE, totalFiltered);

    for (int i = pageStart; i < pageEnd; i++) {
        int thumbIdx = i - pageStart;
        int entryIdx = m_filteredIndices[i];
        const auto& entry = m_allEntries[entryIdx];

        int col = (i - pageStart) % columns;
        if (col != 0) ImGui::SameLine();

        ImGui::PushID(i);

        bool selected = (m_selectedIndex == i);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
        }

        bool clicked = false;
        if (entry.isDirectory) {
            // Directory: show folder icon text
            clicked = ImGui::Button(entry.filename.c_str(), ImVec2(thumbSize, thumbSize));
        } else if (thumbIdx < static_cast<int>(m_thumbnails.size()) &&
                   m_thumbnails[thumbIdx].descriptor) {
            clicked = ImGui::ImageButton("##thumb",
                (ImTextureID)m_thumbnails[thumbIdx].descriptor,
                ImVec2(thumbSize, thumbSize));
        } else {
            // Fallback text button
            std::string label = entry.filename.substr(0, 8);
            clicked = ImGui::Button(label.c_str(), ImVec2(thumbSize, thumbSize));
        }

        if (selected) {
            ImGui::PopStyleColor(2);
        }

        if (clicked) {
            if (entry.isDirectory) {
                // Navigate into directory
                setDirectory(entry.fullPath);
            } else {
                m_selectedIndex = i;
                m_selectedPath = entry.fullPath;
                if (m_onSelected) {
                    m_onSelected(m_selectedPath);
                }
            }
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", entry.filename.c_str());
        }

        ImGui::PopID();
    }

    ImGui::EndChild();

    // Bottom status bar
    if (!m_selectedPath.empty()) {
        ImGui::Text("Selected: %s", std::filesystem::path(m_selectedPath).filename().c_str());
    } else {
        ImGui::TextDisabled("No texture selected");
    }

    ImGui::End();
}

} // namespace eden
