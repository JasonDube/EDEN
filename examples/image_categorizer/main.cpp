// Image Categorizer — manual sorter for ~/Downloads art references into
// /home/jasondube/Desktop/paint subjects/<category>/ with sequential renaming,
// resumable via .categorization_log.tsv, undo-able, ImGui hotkey driven.

#include "Renderer/VulkanApplicationBase.hpp"
#include "Renderer/VulkanContext.hpp"
#include "Renderer/Swapchain.hpp"
#include "Renderer/ImGuiManager.hpp"

#include <eden/Input.hpp>
#include <eden/Window.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using namespace eden;

namespace {
const fs::path kDownloadsDir = "/home/jasondube/Downloads";
const fs::path kProjectDir   = "/home/jasondube/Desktop/paint subjects";
const fs::path kManifestPath = kProjectDir / ".manifest.txt";
const fs::path kLogPath      = kProjectDir / ".categorization_log.tsv";
}

class ImageCategorizerApp : public VulkanApplicationBase {
public:
    ImageCategorizerApp()
        : VulkanApplicationBase(1600, 1000, "Image Categorizer") {}

protected:
    void onInit() override {
        m_imguiManager.init(getContext(), getSwapchain(), getWindow().getHandle(),
                            "image_categorizer_imgui.ini");
        loadManifest();
        loadLog();
        scanCategories();
        advanceToNextPending();
        loadCurrentImage();
    }

    void onCleanup() override {
        vkDeviceWaitIdle(getContext().getDevice());
        freeCurrentImage();
        m_imguiManager.cleanup();
    }

    void update(float /*dt*/) override {
        // Process any action requested by last frame's UI / hotkeys BEFORE we
        // start the next ImGui frame. Doing the placement (which frees the
        // current image's descriptor) mid-renderUI() race the in-flight frame
        // that's still using that descriptor → silent GPU crash.
        applyPendingAction();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        Input::update();
        handleHotkeys();
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) override {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        auto& swapchain = getSwapchain();
        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = swapchain.getRenderPass();
        rp.framebuffer = swapchain.getFramebuffers()[imageIndex];
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = swapchain.getExtent();
        std::array<VkClearValue, 2> clears{};
        clears[0].color = {{0.10f, 0.10f, 0.12f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};
        rp.clearValueCount = static_cast<uint32_t>(clears.size());
        rp.pClearValues = clears.data();
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        renderUI();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

private:
    // ----- state -----
    ImGuiManager m_imguiManager;

    std::vector<std::string> m_manifest;
    std::unordered_set<std::string> m_done;
    size_t m_currentIndex = 0;

    // category path (relative to kProjectDir, e.g. "portraits" or "portraits/old_men")
    // -> file count
    std::map<std::string, int> m_categoryCounts;
    std::vector<std::string> m_orderedCategories;  // sorted by count desc, then name

    struct UndoEntry {
        size_t manifest_idx;
        std::string dest_rel;   // "portraits/portraits_0008.jpg"
        std::string category;   // "portraits" or "portraits/old_men"
    };
    std::vector<UndoEntry> m_undoStack;

    // Current image GPU resources
    VkImage m_curImage = VK_NULL_HANDLE;
    VkDeviceMemory m_curImageMemory = VK_NULL_HANDLE;
    VkImageView m_curView = VK_NULL_HANDLE;
    VkSampler m_curSampler = VK_NULL_HANDLE;
    VkDescriptorSet m_curDescriptor = VK_NULL_HANDLE;
    int m_curWidth = 0;
    int m_curHeight = 0;
    std::string m_curFilename;
    bool m_loadFailed = false;

    char m_newCategoryInput[256] = {0};

    // Deferred-action machinery — UI/hotkeys set this; applyPendingAction()
    // executes it between frames.
    enum class PendingAction { None, Place, Skip, Undo };
    PendingAction m_pending = PendingAction::None;
    std::string m_pendingCategory;

    void requestPlacement(std::string cat) {
        if (m_pending != PendingAction::None) return;  // one action per frame
        m_pending = PendingAction::Place;
        m_pendingCategory = std::move(cat);
    }
    void requestSkip() {
        if (m_pending != PendingAction::None) return;
        m_pending = PendingAction::Skip;
    }
    void requestUndo() {
        if (m_pending != PendingAction::None) return;
        m_pending = PendingAction::Undo;
    }
    void applyPendingAction() {
        PendingAction act = m_pending;
        m_pending = PendingAction::None;
        switch (act) {
            case PendingAction::None:  return;
            case PendingAction::Place: placeInCategory(m_pendingCategory); break;
            case PendingAction::Skip:  placeInCategory("unsorted");        break;
            case PendingAction::Undo:  undoLast();                         break;
        }
    }

    // ----- bookkeeping -----
    void loadManifest() {
        std::ifstream f(kManifestPath);
        if (!f) {
            std::cerr << "Could not open " << kManifestPath << "\n";
            return;
        }
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty()) m_manifest.push_back(line);
        }
        std::cout << "Manifest: " << m_manifest.size() << " files\n";
    }

    void loadLog() {
        std::ifstream f(kLogPath);
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            auto tab = line.find('\t');
            if (tab == std::string::npos) continue;
            m_done.insert(line.substr(0, tab));
        }
        std::cout << "Already done: " << m_done.size() << " files\n";
    }

    void scanCategories() {
        m_categoryCounts.clear();
        if (!fs::is_directory(kProjectDir)) return;
        for (auto& entry : fs::directory_iterator(kProjectDir)) {
            if (!entry.is_directory()) continue;
            std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            if (name == "current") continue;  // pre-existing unrelated folder

            int count = 0;
            for (auto& f : fs::directory_iterator(entry.path())) {
                if (f.is_regular_file()) {
                    count++;
                } else if (f.is_directory()) {
                    std::string sub = name + "/" + f.path().filename().string();
                    int subCount = 0;
                    for (auto& g : fs::directory_iterator(f.path())) {
                        if (g.is_regular_file()) subCount++;
                    }
                    m_categoryCounts[sub] = subCount;
                }
            }
            m_categoryCounts[name] = count;
        }
        rebuildOrderedCategories();
    }

    void rebuildOrderedCategories() {
        m_orderedCategories.clear();
        for (auto& kv : m_categoryCounts) m_orderedCategories.push_back(kv.first);
        std::sort(m_orderedCategories.begin(), m_orderedCategories.end(),
            [this](const std::string& a, const std::string& b) {
                int ca = m_categoryCounts[a], cb = m_categoryCounts[b];
                if (ca != cb) return ca > cb;
                return a < b;
            });
    }

    void advanceToNextPending() {
        while (m_currentIndex < m_manifest.size() && m_done.count(m_manifest[m_currentIndex])) {
            m_currentIndex++;
        }
    }

    // ----- image GPU upload/free -----
    void freeCurrentImage() {
        VkDevice device = getContext().getDevice();
        if (m_curDescriptor) {
            ImGui_ImplVulkan_RemoveTexture(m_curDescriptor);
            m_curDescriptor = VK_NULL_HANDLE;
        }
        if (m_curSampler) { vkDestroySampler(device, m_curSampler, nullptr); m_curSampler = VK_NULL_HANDLE; }
        if (m_curView)    { vkDestroyImageView(device, m_curView, nullptr);  m_curView = VK_NULL_HANDLE; }
        if (m_curImage)   { vkDestroyImage(device, m_curImage, nullptr);     m_curImage = VK_NULL_HANDLE; }
        if (m_curImageMemory) { vkFreeMemory(device, m_curImageMemory, nullptr); m_curImageMemory = VK_NULL_HANDLE; }
    }

    void loadCurrentImage() {
        VkDevice device = getContext().getDevice();
        vkDeviceWaitIdle(device);   // ensure previous frame done with old descriptor
        freeCurrentImage();

        m_curWidth = m_curHeight = 0;
        m_loadFailed = false;
        m_curFilename.clear();

        if (m_currentIndex >= m_manifest.size()) return;

        m_curFilename = m_manifest[m_currentIndex];
        fs::path src = kDownloadsDir / m_curFilename;

        int w, h, ch;
        unsigned char* pixels = stbi_load(src.c_str(), &w, &h, &ch, STBI_rgb_alpha);
        if (!pixels) {
            std::cerr << "stbi_load failed: " << src << "\n";
            m_loadFailed = true;
            return;
        }

        VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;

        // Staging buffer
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        getContext().createBuffer(imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer, stagingMemory);

        void* data = nullptr;
        vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data);
        std::memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device, stagingMemory);
        stbi_image_free(pixels);

        // Image
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        vkCreateImage(device, &imageInfo, nullptr, &m_curImage);

        VkMemoryRequirements memReq{};
        vkGetImageMemoryRequirements(device, m_curImage, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = getContext().findMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &allocInfo, nullptr, &m_curImageMemory);
        vkBindImageMemory(device, m_curImage, m_curImageMemory, 0);

        // Transition + copy + transition
        VkCommandBuffer cmd = getContext().beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = m_curImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, m_curImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        getContext().endSingleTimeCommands(cmd);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);

        // View
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_curImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &viewInfo, nullptr, &m_curView);

        // Sampler
        VkSamplerCreateInfo s{};
        s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        s.magFilter = VK_FILTER_LINEAR;
        s.minFilter = VK_FILTER_LINEAR;
        s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        s.maxLod = 1.0f;
        s.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        vkCreateSampler(device, &s, nullptr, &m_curSampler);

        m_curDescriptor = ImGui_ImplVulkan_AddTexture(m_curSampler, m_curView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_curWidth = w;
        m_curHeight = h;
    }

    // ----- placement / undo -----
    int nextCounterFor(const std::string& cat) {
        fs::path catDir = kProjectDir / cat;
        if (!fs::is_directory(catDir)) return 1;
        std::string leaf = fs::path(cat).filename().string();
        std::string prefix = leaf + "_";
        int maxN = 0;
        for (auto& f : fs::directory_iterator(catDir)) {
            if (!f.is_regular_file()) continue;
            std::string fname = f.path().filename().string();
            if (fname.size() < prefix.size() + 4) continue;
            if (fname.compare(0, prefix.size(), prefix) != 0) continue;
            try {
                int n = std::stoi(fname.substr(prefix.size(), 4));
                if (n > maxN) maxN = n;
            } catch (...) {}
        }
        return maxN + 1;
    }

    void placeInCategory(std::string cat) {
        if (m_currentIndex >= m_manifest.size()) return;

        // sanitize: strip leading/trailing slashes & spaces
        auto trim = [](std::string s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '/')) s.erase(s.begin());
            while (!s.empty() && (s.back()  == ' ' || s.back()  == '\t' || s.back()  == '/')) s.pop_back();
            return s;
        };
        cat = trim(cat);
        if (cat.empty()) return;

        fs::path catDir = kProjectDir / cat;
        std::error_code ec;
        fs::create_directories(catDir, ec);
        if (ec) {
            std::cerr << "Could not create " << catDir << ": " << ec.message() << "\n";
            return;
        }

        int nextNum = nextCounterFor(cat);
        const std::string& srcName = m_manifest[m_currentIndex];
        fs::path src = kDownloadsDir / srcName;
        std::string ext = fs::path(srcName).extension().string();
        if (ext.empty()) ext = ".jpg";

        std::string leaf = fs::path(cat).filename().string();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s_%04d%s", leaf.c_str(), nextNum, ext.c_str());
        std::string destName = buf;
        fs::path dest = catDir / destName;
        std::string destRel = cat + "/" + destName;

        fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "Copy failed (" << src << " -> " << dest << "): " << ec.message() << "\n";
            return;
        }

        // Append to log
        {
            std::ofstream logf(kLogPath, std::ios::app);
            logf << srcName << "\t" << destRel << "\n";
        }

        m_undoStack.push_back({m_currentIndex, destRel, cat});
        m_categoryCounts[cat] = m_categoryCounts[cat] + 1;
        m_done.insert(srcName);
        rebuildOrderedCategories();

        m_currentIndex++;
        advanceToNextPending();
        loadCurrentImage();
    }

    void skipToUnsorted() { requestSkip(); }

    void undoLast() {
        if (m_undoStack.empty()) return;
        UndoEntry e = m_undoStack.back();
        m_undoStack.pop_back();

        // Delete the placed file
        std::error_code ec;
        fs::remove(kProjectDir / e.dest_rel, ec);

        // Rewrite log without the last entry whose src matches
        const std::string& srcName = m_manifest[e.manifest_idx];
        std::vector<std::string> lines;
        {
            std::ifstream f(kLogPath);
            std::string ln;
            while (std::getline(f, ln)) lines.push_back(ln);
        }
        std::string prefix = srcName + "\t";
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            if (it->size() >= prefix.size() && it->compare(0, prefix.size(), prefix) == 0) {
                lines.erase(std::next(it).base());
                break;
            }
        }
        {
            std::ofstream out(kLogPath, std::ios::trunc);
            for (auto& l : lines) out << l << "\n";
        }

        m_done.erase(srcName);
        auto it = m_categoryCounts.find(e.category);
        if (it != m_categoryCounts.end()) {
            it->second = std::max(0, it->second - 1);
        }
        rebuildOrderedCategories();
        m_currentIndex = e.manifest_idx;
        loadCurrentImage();
    }

    // ----- UI -----
    void renderUI() {
        ImGui::NewFrame();

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("Categorizer", nullptr, flags);

        size_t total = m_manifest.size();
        size_t done = m_done.size();
        size_t remaining = total > done ? total - done : 0;
        ImGui::Text("Image %zu / %zu   (%zu done, %zu remaining)",
                    std::min(m_currentIndex + 1, total), total, done, remaining);
        ImGui::SameLine();
        ImGui::TextDisabled("  Hotkeys: 1-9 = first 9 categories  |  Space = skip  |  U = undo  |  Esc = quit");
        ImGui::Separator();

        float availW = ImGui::GetContentRegionAvail().x;
        float availH = ImGui::GetContentRegionAvail().y;
        const float rightW = 340.0f;
        float leftW = availW - rightW - 8.0f;

        // LEFT: image
        ImGui::BeginChild("ImagePanel", ImVec2(leftW, availH), true);
        if (m_currentIndex >= m_manifest.size()) {
            ImGui::TextWrapped("All images categorized. You're done!");
        } else if (m_loadFailed) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load: %s", m_curFilename.c_str());
            if (ImGui::Button("Skip this one")) skipToUnsorted();
        } else if (m_curDescriptor != VK_NULL_HANDLE) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            avail.y -= 40;  // reserve a strip for filename text
            float aspect = static_cast<float>(m_curWidth) / static_cast<float>(m_curHeight);
            float drawW = avail.x;
            float drawH = drawW / aspect;
            if (drawH > avail.y) { drawH = avail.y; drawW = drawH * aspect; }
            float xPad = (avail.x - drawW) * 0.5f;
            if (xPad > 0) { ImGui::Dummy(ImVec2(xPad, 0)); ImGui::SameLine(); }
            ImGui::Image(reinterpret_cast<ImTextureID>(m_curDescriptor), ImVec2(drawW, drawH));
            ImGui::TextWrapped("%s", m_curFilename.c_str());
            ImGui::Text("%d x %d", m_curWidth, m_curHeight);
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // RIGHT: controls
        ImGui::BeginChild("Controls", ImVec2(rightW, availH), true);
        if (m_currentIndex < m_manifest.size() && !m_loadFailed) {
            ImGui::Text("Place in:");
            ImGui::Separator();
            int hotkey = 1;
            for (auto& cat : m_orderedCategories) {
                std::string label;
                if (hotkey <= 9) {
                    label = "[" + std::to_string(hotkey) + "] " + cat
                          + " (" + std::to_string(m_categoryCounts[cat]) + ")";
                    hotkey++;
                } else {
                    label = cat + " (" + std::to_string(m_categoryCounts[cat]) + ")";
                }
                if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) {
                    requestPlacement(cat);
                }
            }

            ImGui::Separator();
            ImGui::Text("New category");
            ImGui::TextDisabled("(use / for subcategory)");
            ImGui::SetNextItemWidth(-60);
            bool entered = ImGui::InputText("##newcat", m_newCategoryInput, sizeof(m_newCategoryInput),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            bool add = ImGui::Button("Add");
            if (entered || add) {
                std::string newCat = m_newCategoryInput;
                m_newCategoryInput[0] = '\0';
                requestPlacement(newCat);
            }

            ImGui::Separator();
            if (ImGui::Button("[Space] Skip to unsorted", ImVec2(-1, 0))) requestSkip();
            if (ImGui::Button("[U] Undo last", ImVec2(-1, 0))) requestUndo();
        }
        ImGui::EndChild();

        ImGui::End();
        ImGui::Render();
    }

    void handleHotkeys() {
        if (m_currentIndex >= m_manifest.size()) return;
        // Don't grab hotkeys while typing in any ImGui input widget
        if (ImGui::GetIO().WantTextInput) return;

        for (int i = 0; i < 9 && i < static_cast<int>(m_orderedCategories.size()); ++i) {
            int key = Input::KEY_1 + i;
            if (Input::isKeyPressed(key)) {
                requestPlacement(m_orderedCategories[i]);
                return;
            }
        }
        if (Input::isKeyPressed(Input::KEY_SPACE)) { requestSkip(); return; }
        if (Input::isKeyPressed(Input::KEY_U))     { requestUndo(); return; }
    }
};

int main() {
    try {
        ImageCategorizerApp app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
