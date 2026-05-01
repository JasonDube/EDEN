#pragma once

// VideoEditor — small ImGui-based clip editor.
//
// Loads a video via VideoPlayer, decodes its frames into a Vulkan image
// that we register with ImGui (so we can preview it inside an
// ImGui::Image), provides scrub + transport controls, and exports a
// trimmed range to a new file by shelling out to ffmpeg.

#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace eden {

class VulkanContext;
class VideoPlayer;

class VideoEditor {
public:
    explicit VideoEditor(VulkanContext& ctx);
    ~VideoEditor();

    VideoEditor(const VideoEditor&) = delete;
    VideoEditor& operator=(const VideoEditor&) = delete;

    // Per-frame: advances the loaded video and uploads the new frame to the
    // preview texture if one was just decoded.
    void update(float deltaTime);

    // Draw the ImGui window. Pass &visible so the window's close-X can
    // toggle the menu's "Video Editor" checkbox in main.
    void renderUI(bool* visible);

    // Default folder for both the Load and Save file dialogs.
    void setDefaultDir(const std::string& dir) { m_defaultDir = dir; }

private:
    void loadVideo(const std::string& path);
    void closeVideo();
    void exportTrimmedClip(const std::string& outputPath);

    void ensurePreviewTexture(int w, int h);
    void destroyPreviewTexture();
    void uploadPreviewFrame(const unsigned char* rgba);

    VulkanContext& m_ctx;
    std::unique_ptr<VideoPlayer> m_player;

    // Preview texture state.
    VkImage         m_image           = VK_NULL_HANDLE;
    VkDeviceMemory  m_imageMemory     = VK_NULL_HANDLE;
    VkImageView     m_imageView       = VK_NULL_HANDLE;
    VkSampler       m_sampler         = VK_NULL_HANDLE;
    VkDescriptorSet m_imguiDescriptor = VK_NULL_HANDLE;
    VkBuffer        m_stagingBuf      = VK_NULL_HANDLE;
    VkDeviceMemory  m_stagingMem      = VK_NULL_HANDLE;
    void*           m_stagingMapped   = nullptr;
    size_t          m_stagingSize     = 0;
    int             m_imageW          = 0;
    int             m_imageH          = 0;
    bool            m_imageReady      = false;  // first frame uploaded yet

    // Editor state.
    std::string m_videoPath;
    std::string m_status;
    std::string m_defaultDir;  // initial folder for Load/Save dialogs
    bool        m_playing  = false;
    bool        m_dragging = false;
    float       m_trimStart = 0.0f;
    float       m_trimEnd   = 0.0f;  // 0 = use clip duration on export
};

} // namespace eden
