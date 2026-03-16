#pragma once

#include <string>
#include <vector>
#include <functional>
#include <vulkan/vulkan.h>

namespace eden {

class VulkanContext;

class TextureBrowser {
public:
    TextureBrowser() = default;
    ~TextureBrowser();

    void init(VulkanContext& context);
    void cleanup();

    void setDirectory(const std::string& path);
    const std::string& getDirectory() const { return m_directory; }

    // Render the ImGui window. Pass pointer to bool for close button.
    void render(bool* open);

    // Currently selected file path (empty if none)
    const std::string& getSelectedPath() const { return m_selectedPath; }

    using TextureSelectedCallback = std::function<void(const std::string& path)>;
    void setTextureSelectedCallback(TextureSelectedCallback cb) { m_onSelected = std::move(cb); }

    // Public DDS loader — reads a small mip and decodes BC7/BC3/BC1 to RGBA
    bool loadDDS(const std::string& path, std::vector<unsigned char>& rgbaOut,
                 int& outWidth, int& outHeight);

private:
    struct FileEntry {
        std::string filename;
        std::string fullPath;
        bool isDirectory = false;
    };

    struct Thumbnail {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
    };

    void scanDirectory();
    void loadPage(int page);
    void freePage();

    // Load a DDS thumbnail by reading a small mip level and decoding BC7
    bool loadDDSThumbnail(const std::string& path, std::vector<unsigned char>& rgbaOut,
                          int& outWidth, int& outHeight);

    // Load PNG/JPG thumbnail via stb_image
    bool loadStbiThumbnail(const std::string& path, std::vector<unsigned char>& rgbaOut,
                           int& outWidth, int& outHeight);

    Thumbnail uploadThumbnail(const unsigned char* rgba, int width, int height);
    void destroyThumbnail(Thumbnail& thumb);

    VulkanContext* m_context = nullptr;

    std::string m_directory;
    std::vector<FileEntry> m_allEntries;    // All files + subdirs
    std::vector<int> m_filteredIndices;     // Indices into m_allEntries matching search

    int m_currentPage = 0;
    static constexpr int ENTRIES_PER_PAGE = 200;
    static constexpr int THUMB_SIZE = 64;
    std::vector<Thumbnail> m_thumbnails;    // GPU thumbnails for current page

    int m_selectedIndex = -1;               // Index into m_filteredIndices
    std::string m_selectedPath;

    char m_searchFilter[256] = "";
    char m_pathInput[1024] = "";
    bool m_needsRescan = false;
    bool m_needsPageLoad = false;

    TextureSelectedCallback m_onSelected;
};

} // namespace eden
