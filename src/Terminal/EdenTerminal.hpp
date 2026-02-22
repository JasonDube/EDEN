#pragma once

#include <vterm.h>
#include <imgui.h>
#include <string>
#include <vector>
#include <cstdint>

namespace eden {

struct TermCell {
    char32_t ch = ' ';
    ImU32 fg = IM_COL32(204, 204, 204, 255);  // Default light gray
    ImU32 bg = IM_COL32(30, 30, 30, 255);      // Default dark background
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

class EdenTerminal {
public:
    EdenTerminal();
    ~EdenTerminal();

    // No copy
    EdenTerminal(const EdenTerminal&) = delete;
    EdenTerminal& operator=(const EdenTerminal&) = delete;

    // Initialize terminal with given size and optional shell command
    bool init(int cols = 120, int rows = 40, const std::string& shell = "");

    // Call every frame — reads PTY output, feeds to libvterm
    void update();

    // Render the terminal as an ImGui window
    void renderImGui(bool* p_open, ImFont* monoFont = nullptr);

    // Send a command string to the terminal (e.g., auto-launch claude)
    void sendCommand(const std::string& cmd);

    // Render terminal cells to an RGBA pixel buffer for 3D texture mapping
    // Returns true if pixels were updated (content changed since last call)
    // variant: 0=normal, 1=flipH, 2=flipV, 3=flip both (rotate 180)
    bool renderToPixels(std::vector<unsigned char>& pixelBuffer, int texWidth, int texHeight, int variant = 0);

    // Check if terminal is active
    bool isAlive() const { return m_masterFd >= 0 && m_childPid > 0; }

    // Clear dirty flag (call after all variants rendered)
    void clearDirty() { m_dirty = false; }

    // Lock size — prevents ImGui window resize from changing terminal cols/rows
    void setLockSize(bool lock) { m_lockSize = lock; }

    // Accessors
    int getCols() const { return m_cols; }
    int getRows() const { return m_rows; }

    // Shutdown the terminal
    void shutdown();

private:
    // PTY
    int m_masterFd = -1;
    pid_t m_childPid = -1;

    // libvterm
    VTerm* m_vterm = nullptr;
    VTermScreen* m_vtermScreen = nullptr;

    // Cell buffer
    int m_cols = 120;
    int m_rows = 40;
    std::vector<std::vector<TermCell>> m_cells;

    // Cursor
    int m_cursorRow = 0;
    int m_cursorCol = 0;
    bool m_cursorVisible = true;
    float m_cursorBlinkTimer = 0.0f;

    // Scrollback
    int m_scrollOffset = 0;

    // Dirty tracking for 3D texture rendering
    bool m_dirty = true;

    // When true, ImGui window resize won't change terminal cols/rows
    bool m_lockSize = false;

    // Internal
    void syncCells();
    void handleKeyInput();
    void handleResize(int newCols, int newRows);
    void writeToPty(const char* data, size_t len);
    static ImU32 vtermColorToImU32(VTermColor color);

public:
    // libvterm callbacks (public so static init can reference them)
    static int onDamage(VTermRect rect, void* user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user);
    static int onBell(void* user);
};

} // namespace eden
