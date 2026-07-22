// ─────────────────────────────────────────────────────────────────────────────
// EDEN OS · KMS display spike
//
// The one risky question for "EDEN boots itself as the display server":
// can Vulkan take the real display straight from a DRM fd (no X11, no Wayland,
// no compositor) on this NVIDIA box, modeset it, and present frames?
//
// This program does ONLY that: acquire /dev/dri/card0's connected connector via
// VK_EXT_acquire_drm_display, create a display-plane surface + swapchain, and
// flash an animated colour for a few seconds. If your TV shows a pulsing colour
// from a bare TTY, the foundation is proven and the engine port is mechanical.
//
// Build:  ./build.sh          Run (from a BARE TTY):  ./run-tty.sh
// ─────────────────────────────────────────────────────────────────────────────
#include <vulkan/vulkan.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <vector>
#include <string>

// ── tiny helpers ─────────────────────────────────────────────────────────────
#define CK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { \
    fprintf(stderr, "[FAIL] %s -> VkResult %d (line %d)\n", #x, (int)_r, __LINE__); \
    return false; } } while (0)

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int) { g_stop = 1; }

static double now_s() {
    timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

// ── everything lives here so cleanup is straightforward ──────────────────────
struct Probe {
    int          drmFd   = -1;
    uint32_t     connId  = 0;
    uint32_t     modeW = 0, modeH = 0;

    VkInstance   inst   = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice     dev    = VK_NULL_HANDLE;
    uint32_t     qfam   = 0;
    VkQueue      queue  = VK_NULL_HANDLE;

    VkDisplayKHR display = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swap  = VK_NULL_HANDLE;
    VkFormat     fmt     = VK_FORMAT_B8G8R8A8_UNORM;
    std::vector<VkImage>     images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> fbs;
    VkRenderPass rp = VK_NULL_HANDLE;
    VkCommandPool cpool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cbufs;
    VkSemaphore semAcq = VK_NULL_HANDLE, semDone = VK_NULL_HANDLE;
    VkFence      fence = VK_NULL_HANDLE;

    // dynamically-loaded EXT entrypoints
    PFN_vkGetDrmDisplayEXT     pGetDrmDisplay = nullptr;
    PFN_vkAcquireDrmDisplayEXT pAcquireDrmDisplay = nullptr;

    // ── 1. find the connected DRM connector + its mode ───────────────────────
    bool pickConnector() {
        drmFd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
        if (drmFd < 0) { perror("open /dev/dri/card0"); return false; }

        // Become DRM master so Vulkan may modeset. On a bare TTY with no
        // compositor running this succeeds (root, or the active-VT session).
        if (drmSetMaster(drmFd) != 0)
            fprintf(stderr, "[warn] drmSetMaster: %s (continuing; may already hold it)\n",
                    strerror(errno));

        drmModeRes *res = drmModeGetResources(drmFd);
        if (!res) { perror("drmModeGetResources"); return false; }

        for (int i = 0; i < res->count_connectors; ++i) {
            drmModeConnector *c = drmModeGetConnector(drmFd, res->connectors[i]);
            if (!c) continue;
            if (c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
                connId = c->connector_id;
                // mode 0 is the preferred/native mode
                modeW = c->modes[0].hdisplay;
                modeH = c->modes[0].vdisplay;
                printf("[ok]  connector %u connected: %ux%u @ %uHz\n",
                       connId, modeW, modeH, c->modes[0].vrefresh);
                drmModeFreeConnector(c);
                break;
            }
            drmModeFreeConnector(c);
        }
        drmModeFreeResources(res);
        if (!connId) { fprintf(stderr, "[FAIL] no connected connector with a mode\n"); return false; }
        return true;
    }

    // ── 2. Vulkan instance with the display extensions ───────────────────────
    bool makeInstance() {
        const char *exts[] = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_DISPLAY_EXTENSION_NAME,
            VK_KHR_GET_DISPLAY_PROPERTIES_2_EXTENSION_NAME,
            VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME,
            VK_EXT_ACQUIRE_DRM_DISPLAY_EXTENSION_NAME,
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        };
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "eden-kms-probe";
        app.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = sizeof(exts)/sizeof(exts[0]);
        ci.ppEnabledExtensionNames = exts;
        CK(vkCreateInstance(&ci, nullptr, &inst));

        pGetDrmDisplay = (PFN_vkGetDrmDisplayEXT)
            vkGetInstanceProcAddr(inst, "vkGetDrmDisplayEXT");
        pAcquireDrmDisplay = (PFN_vkAcquireDrmDisplayEXT)
            vkGetInstanceProcAddr(inst, "vkAcquireDrmDisplayEXT");
        if (!pGetDrmDisplay || !pAcquireDrmDisplay) {
            fprintf(stderr, "[FAIL] acquire_drm_display entrypoints not found\n");
            return false;
        }
        printf("[ok]  instance + display extensions\n");
        return true;
    }

    // ── 3. pick the physical device (prefer discrete) ────────────────────────
    bool pickPhysical() {
        uint32_t n = 0; CK(vkEnumeratePhysicalDevices(inst, &n, nullptr));
        std::vector<VkPhysicalDevice> devs(n);
        CK(vkEnumeratePhysicalDevices(inst, &n, devs.data()));
        for (auto d : devs) {
            VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(d, &p);
            if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                phys = d; printf("[ok]  GPU: %s\n", p.deviceName); return true;
            }
            if (phys == VK_NULL_HANDLE) phys = d;
        }
        if (phys == VK_NULL_HANDLE) { fprintf(stderr, "[FAIL] no Vulkan device\n"); return false; }
        return true;
    }

    // ── 4. acquire the display straight from the DRM fd ──────────────────────
    bool acquireDisplay() {
        CK(pGetDrmDisplay(phys, drmFd, connId, &display));
        printf("[ok]  vkGetDrmDisplayEXT -> VkDisplay %p\n", (void*)display);
        CK(pAcquireDrmDisplay(phys, drmFd, display));
        printf("[ok]  vkAcquireDrmDisplayEXT (display is ours)\n");
        return true;
    }

    // ── 5. display mode + plane -> a presentable surface ─────────────────────
    bool makeSurface() {
        // pick a display mode whose visible region matches the DRM native mode
        uint32_t mc = 0;
        CK(vkGetDisplayModePropertiesKHR(phys, display, &mc, nullptr));
        std::vector<VkDisplayModePropertiesKHR> modes(mc);
        CK(vkGetDisplayModePropertiesKHR(phys, display, &mc, modes.data()));
        VkDisplayModeKHR chosen = VK_NULL_HANDLE;
        VkExtent2D ext{modeW, modeH};
        for (auto &m : modes) {
            if (m.parameters.visibleRegion.width == modeW &&
                m.parameters.visibleRegion.height == modeH) { chosen = m.displayMode; break; }
        }
        if (chosen == VK_NULL_HANDLE && mc > 0) {           // fall back to the first
            chosen = modes[0].displayMode;
            ext = modes[0].parameters.visibleRegion;
            modeW = ext.width; modeH = ext.height;
        }
        if (chosen == VK_NULL_HANDLE) { fprintf(stderr, "[FAIL] no display modes\n"); return false; }
        printf("[ok]  display mode %ux%u\n", ext.width, ext.height);

        // find a plane that can drive this display
        uint32_t pc = 0;
        CK(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(phys, &pc, nullptr));
        std::vector<VkDisplayPlanePropertiesKHR> planes(pc);
        CK(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(phys, &pc, planes.data()));
        uint32_t planeIndex = UINT32_MAX, stackIndex = 0;
        for (uint32_t i = 0; i < pc; ++i) {
            uint32_t sc = 0;
            vkGetDisplayPlaneSupportedDisplaysKHR(phys, i, &sc, nullptr);
            if (sc == 0) continue;
            std::vector<VkDisplayKHR> sup(sc);
            vkGetDisplayPlaneSupportedDisplaysKHR(phys, i, &sc, sup.data());
            for (auto d : sup) if (d == display) {
                planeIndex = i; stackIndex = planes[i].currentStackIndex; break;
            }
            if (planeIndex != UINT32_MAX) break;
        }
        if (planeIndex == UINT32_MAX) { fprintf(stderr, "[FAIL] no plane supports the display\n"); return false; }
        printf("[ok]  plane %u (stack %u)\n", planeIndex, stackIndex);

        VkDisplaySurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
        sci.displayMode      = chosen;
        sci.planeIndex       = planeIndex;
        sci.planeStackIndex  = stackIndex;
        sci.transform        = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        sci.globalAlpha      = 1.0f;
        sci.alphaMode        = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
        sci.imageExtent      = ext;
        CK(vkCreateDisplayPlaneSurfaceKHR(inst, &sci, nullptr, &surface));
        printf("[ok]  display-plane surface created\n");
        return true;
    }

    // ── 6. logical device + graphics/present queue ───────────────────────────
    bool makeDevice() {
        uint32_t qn = 0; vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, qf.data());
        bool found = false;
        for (uint32_t i = 0; i < qn; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &present);
            if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { qfam = i; found = true; break; }
        }
        if (!found) { fprintf(stderr, "[FAIL] no graphics+present queue\n"); return false; }

        float pri = 1.0f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = qfam; qci.queueCount = 1; qci.pQueuePriorities = &pri;
        const char *dexts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = dexts;
        CK(vkCreateDevice(phys, &dci, nullptr, &dev));
        vkGetDeviceQueue(dev, qfam, 0, &queue);
        printf("[ok]  logical device + queue (family %u)\n", qfam);
        return true;
    }

    // ── 7. swapchain + render pass + per-image framebuffers/cmdbufs ───────────
    bool makeSwapchain() {
        VkSurfaceCapabilitiesKHR caps;
        CK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps));

        uint32_t fc = 0; CK(vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fc, nullptr));
        std::vector<VkSurfaceFormatKHR> formats(fc);
        CK(vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fc, formats.data()));
        VkColorSpaceKHR cs = formats[0].colorSpace; fmt = formats[0].format;
        for (auto &f : formats)
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM) { fmt = f.format; cs = f.colorSpace; break; }

        VkExtent2D ext = caps.currentExtent.width != UINT32_MAX
                         ? caps.currentExtent : VkExtent2D{modeW, modeH};
        uint32_t imgCount = caps.minImageCount + 1;
        if (caps.maxImageCount && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;

        VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        sc.surface = surface; sc.minImageCount = imgCount;
        sc.imageFormat = fmt; sc.imageColorSpace = cs; sc.imageExtent = ext;
        sc.imageArrayLayers = 1; sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sc.preTransform = caps.currentTransform;
        sc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sc.presentMode = VK_PRESENT_MODE_FIFO_KHR; sc.clipped = VK_TRUE;
        CK(vkCreateSwapchainKHR(dev, &sc, nullptr, &swap));

        uint32_t ic = 0; CK(vkGetSwapchainImagesKHR(dev, swap, &ic, nullptr));
        images.resize(ic); CK(vkGetSwapchainImagesKHR(dev, swap, &ic, images.data()));
        printf("[ok]  swapchain: %u images, %ux%u\n", ic, ext.width, ext.height);

        // render pass: single colour attachment, clear -> present
        VkAttachmentDescription at{};
        at.format = fmt; at.samples = VK_SAMPLE_COUNT_1_BIT;
        at.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; at.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        at.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        at.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        at.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        at.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ar{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sp{}; sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1; sp.pColorAttachments = &ar;
        VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpci.attachmentCount = 1; rpci.pAttachments = &at;
        rpci.subpassCount = 1; rpci.pSubpasses = &sp;
        CK(vkCreateRenderPass(dev, &rpci, nullptr, &rp));

        views.resize(ic); fbs.resize(ic);
        for (uint32_t i = 0; i < ic; ++i) {
            VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vci.image = images[i]; vci.viewType = VK_IMAGE_VIEW_TYPE_2D; vci.format = fmt;
            vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            CK(vkCreateImageView(dev, &vci, nullptr, &views[i]));
            VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fci.renderPass = rp; fci.attachmentCount = 1; fci.pAttachments = &views[i];
            fci.width = ext.width; fci.height = ext.height; fci.layers = 1;
            CK(vkCreateFramebuffer(dev, &fci, nullptr, &fbs[i]));
        }

        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = qfam;
        CK(vkCreateCommandPool(dev, &pci, nullptr, &cpool));
        cbufs.resize(ic);
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = cpool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = ic;
        CK(vkAllocateCommandBuffers(dev, &ai, cbufs.data()));

        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        CK(vkCreateSemaphore(dev, &sem, nullptr, &semAcq));
        CK(vkCreateSemaphore(dev, &sem, nullptr, &semDone));
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        CK(vkCreateFence(dev, &fci, nullptr, &fence));

        m_ext = ext;
        return true;
    }
    VkExtent2D m_ext{};

    // ── 8. present an animated clear colour until stop/timeout ───────────────
    bool renderLoop(double seconds) {
        printf("[go]  presenting for up to %.0fs — Ctrl+C or wait to exit\n", seconds);
        double t0 = now_s();
        uint64_t frame = 0;
        while (!g_stop) {
            double t = now_s() - t0;
            if (t > seconds) break;

            vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
            vkResetFences(dev, 1, &fence);

            uint32_t idx = 0;
            VkResult ar = vkAcquireNextImageKHR(dev, swap, UINT64_MAX, semAcq, VK_NULL_HANDLE, &idx);
            if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
                fprintf(stderr, "[FAIL] acquire image -> %d\n", (int)ar); return false;
            }

            VkCommandBuffer cb = cbufs[idx];
            vkResetCommandBuffer(cb, 0);
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            vkBeginCommandBuffer(cb, &bi);
            // pulsing colour so we can SEE it's live, not a frozen buffer
            float r = 0.5f + 0.5f * sinf((float)t * 1.7f);
            float g = 0.5f + 0.5f * sinf((float)t * 1.1f + 2.0f);
            float b = 0.5f + 0.5f * sinf((float)t * 0.7f + 4.0f);
            VkClearValue clear{}; clear.color = {{r, g, b, 1.0f}};
            VkRenderPassBeginInfo rpb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rpb.renderPass = rp; rpb.framebuffer = fbs[idx];
            rpb.renderArea = {{0,0}, m_ext};
            rpb.clearValueCount = 1; rpb.pClearValues = &clear;
            vkCmdBeginRenderPass(cb, &rpb, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdEndRenderPass(cb);
            vkEndCommandBuffer(cb);

            VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.waitSemaphoreCount = 1; si.pWaitSemaphores = &semAcq; si.pWaitDstStageMask = &wait;
            si.commandBufferCount = 1; si.pCommandBuffers = &cb;
            si.signalSemaphoreCount = 1; si.pSignalSemaphores = &semDone;
            CK(vkQueueSubmit(queue, 1, &si, fence));

            VkPresentInfoKHR pr{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            pr.waitSemaphoreCount = 1; pr.pWaitSemaphores = &semDone;
            pr.swapchainCount = 1; pr.pSwapchains = &swap; pr.pImageIndices = &idx;
            VkResult prr = vkQueuePresentKHR(queue, &pr);
            if (prr != VK_SUCCESS && prr != VK_SUBOPTIMAL_KHR) {
                fprintf(stderr, "[FAIL] present -> %d\n", (int)prr); return false;
            }
            frame++;
        }
        vkDeviceWaitIdle(dev);
        printf("[ok]  presented %llu frames — SUCCESS\n", (unsigned long long)frame);
        return true;
    }

    void cleanup() {
        if (dev) {
            vkDeviceWaitIdle(dev);
            if (fence)   vkDestroyFence(dev, fence, nullptr);
            if (semAcq)  vkDestroySemaphore(dev, semAcq, nullptr);
            if (semDone) vkDestroySemaphore(dev, semDone, nullptr);
            for (auto f : fbs)   if (f) vkDestroyFramebuffer(dev, f, nullptr);
            for (auto v : views) if (v) vkDestroyImageView(dev, v, nullptr);
            if (rp)    vkDestroyRenderPass(dev, rp, nullptr);
            if (cpool) vkDestroyCommandPool(dev, cpool, nullptr);
            if (swap)  vkDestroySwapchainKHR(dev, swap, nullptr);
            vkDestroyDevice(dev, nullptr);
        }
        if (surface) vkDestroySurfaceKHR(inst, surface, nullptr);
        if (inst)    vkDestroyInstance(inst, nullptr);
        if (drmFd >= 0) { drmDropMaster(drmFd); close(drmFd); }
    }
};

int main() {
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    double seconds = 8.0;
    if (const char *e = getenv("KMS_SECONDS")) seconds = atof(e);

    printf("── EDEN KMS display spike ─────────────────────────────\n");
    Probe p;
    bool ok = p.pickConnector()
           && p.makeInstance()
           && p.pickPhysical()
           && p.acquireDisplay()
           && p.makeSurface()
           && p.makeDevice()
           && p.makeSwapchain()
           && p.renderLoop(seconds);
    p.cleanup();
    printf("───────────────────────────────────────────────────────\n");
    printf(ok ? "RESULT: SUCCESS — Vulkan drove the display directly.\n"
              : "RESULT: FAILED — see the [FAIL] line above.\n");
    return ok ? 0 : 1;
}
