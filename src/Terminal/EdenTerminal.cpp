#include "Terminal/EdenTerminal.hpp"

#include <pty.h>       // forkpty
#include <unistd.h>    // read, write, close
#include <fcntl.h>     // fcntl, O_NONBLOCK
#include <sys/wait.h>  // waitpid
#include <sys/ioctl.h> // ioctl, TIOCSWINSZ
#include <signal.h>
#include <poll.h>
#include <cstring>
#include <iostream>
#include <algorithm>

namespace eden {

// ── libvterm screen callbacks ──────────────────────────────────────────

static VTermScreenCallbacks s_screenCallbacks = {
    .damage      = EdenTerminal::onDamage,
    .moverect    = nullptr,
    .movecursor  = EdenTerminal::onMoveCursor,
    .settermprop = nullptr,
    .bell        = EdenTerminal::onBell,
    .resize      = nullptr,
    .sb_pushline = nullptr,
    .sb_popline  = nullptr,
    .sb_clear    = nullptr,
};

// ── Constructor / Destructor ───────────────────────────────────────────

EdenTerminal::EdenTerminal() = default;

EdenTerminal::~EdenTerminal() {
    shutdown();
}

// ── init ───────────────────────────────────────────────────────────────

bool EdenTerminal::init(int cols, int rows, const std::string& shell) {
    m_cols = cols;
    m_rows = rows;

    // Allocate cell buffer
    m_cells.resize(m_rows, std::vector<TermCell>(m_cols));

    // Create libvterm instance
    m_vterm = vterm_new(m_rows, m_cols);
    if (!m_vterm) {
        std::cerr << "[EdenTerminal] Failed to create vterm" << std::endl;
        return false;
    }

    vterm_set_utf8(m_vterm, 1);

    // Get screen and set callbacks
    m_vtermScreen = vterm_obtain_screen(m_vterm);
    vterm_screen_set_callbacks(m_vtermScreen, &s_screenCallbacks, this);
    vterm_screen_reset(m_vtermScreen, 1);

    // Fork PTY
    struct winsize ws;
    ws.ws_col = m_cols;
    ws.ws_row = m_rows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    m_childPid = forkpty(&m_masterFd, nullptr, nullptr, &ws);
    if (m_childPid < 0) {
        std::cerr << "[EdenTerminal] forkpty failed" << std::endl;
        vterm_free(m_vterm);
        m_vterm = nullptr;
        return false;
    }

    if (m_childPid == 0) {
        // Child process — exec shell
        const char* shellCmd = shell.empty() ? getenv("SHELL") : shell.c_str();
        if (!shellCmd) shellCmd = "/bin/bash";

        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);

        execlp(shellCmd, shellCmd, "--login", nullptr);
        _exit(127); // exec failed
    }

    // Parent — set master fd non-blocking
    int flags = fcntl(m_masterFd, F_GETFL, 0);
    fcntl(m_masterFd, F_SETFL, flags | O_NONBLOCK);

    // std::cout << "[EdenTerminal] Started shell (pid=" << m_childPid
    //           << ", fd=" << m_masterFd << ", " << m_cols << "x" << m_rows << ")" << std::endl;

    return true;
}

// ── update ─────────────────────────────────────────────────────────────

void EdenTerminal::update() {
    if (m_masterFd < 0) return;

    // Non-blocking read from PTY
    char buf[4096];
    for (;;) {
        ssize_t n = read(m_masterFd, buf, sizeof(buf));
        if (n > 0) {
            vterm_input_write(m_vterm, buf, n);
            m_dirty = true;
        } else {
            break;
        }
    }

    // Check if child is still alive
    if (m_childPid > 0) {
        int status;
        pid_t result = waitpid(m_childPid, &status, WNOHANG);
        if (result == m_childPid) {
            // std::cout << "[EdenTerminal] Shell exited" << std::endl;
            m_childPid = -1;
        }
    }

    // Sync cell buffer from vterm screen
    syncCells();

    // Tick cursor blink timer (needed for 3D texture rendering)
    m_cursorBlinkTimer += ImGui::GetIO().DeltaTime;
    if (m_cursorBlinkTimer > 1.0f) m_cursorBlinkTimer -= 1.0f;
    // Mark dirty on blink transition so texture updates
    bool cursorOn = m_cursorBlinkTimer < 0.5f;
    static bool lastCursorOn = true;
    if (cursorOn != lastCursorOn) { m_dirty = true; lastCursorOn = cursorOn; }
}

// ── syncCells ──────────────────────────────────────────────────────────

void EdenTerminal::syncCells() {
    if (!m_vtermScreen) return;

    for (int row = 0; row < m_rows; ++row) {
        for (int col = 0; col < m_cols; ++col) {
            VTermPos pos = {row, col};
            VTermScreenCell cell;
            vterm_screen_get_cell(m_vtermScreen, pos, &cell);

            TermCell& tc = m_cells[row][col];

            // Character
            if (cell.chars[0] == 0 || cell.chars[0] == (uint32_t)-1) {
                tc.ch = ' ';
            } else {
                tc.ch = cell.chars[0];
            }

            // Foreground color
            if (VTERM_COLOR_IS_DEFAULT_FG(&cell.fg)) {
                tc.fg = IM_COL32(204, 204, 204, 255);
            } else if (VTERM_COLOR_IS_INDEXED(&cell.fg)) {
                VTermColor resolved = cell.fg;
                vterm_screen_convert_color_to_rgb(m_vtermScreen, &resolved);
                tc.fg = IM_COL32(resolved.rgb.red, resolved.rgb.green, resolved.rgb.blue, 255);
            } else if (VTERM_COLOR_IS_RGB(&cell.fg)) {
                tc.fg = IM_COL32(cell.fg.rgb.red, cell.fg.rgb.green, cell.fg.rgb.blue, 255);
            }

            // Background color
            if (VTERM_COLOR_IS_DEFAULT_BG(&cell.bg)) {
                tc.bg = IM_COL32(30, 30, 30, 255);
            } else if (VTERM_COLOR_IS_INDEXED(&cell.bg)) {
                VTermColor resolved = cell.bg;
                vterm_screen_convert_color_to_rgb(m_vtermScreen, &resolved);
                tc.bg = IM_COL32(resolved.rgb.red, resolved.rgb.green, resolved.rgb.blue, 255);
            } else if (VTERM_COLOR_IS_RGB(&cell.bg)) {
                tc.bg = IM_COL32(cell.bg.rgb.red, cell.bg.rgb.green, cell.bg.rgb.blue, 255);
            }

            // Attributes
            tc.bold = cell.attrs.bold;
            tc.italic = cell.attrs.italic;
            tc.underline = cell.attrs.underline != 0;
        }
    }
}

// ── renderImGui ────────────────────────────────────────────────────────

void EdenTerminal::renderImGui(bool* p_open, ImFont* monoFont) {
    if (p_open && !*p_open) return;

    if (monoFont) ImGui::PushFont(monoFont);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(900, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f - 450,
               viewport->WorkPos.y + viewport->WorkSize.y * 0.5f - 250),
        ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (!ImGui::Begin("Terminal", p_open, flags)) {
        ImGui::End();
        if (monoFont) ImGui::PopFont();
        return;
    }

    // Handle keyboard input when focused
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        handleKeyInput();
    }

    // Calculate cell size from font — use CalcTextSize for reliable metrics
    ImVec2 charSize = ImGui::CalcTextSize("M");
    ImVec2 cellSize(charSize.x, charSize.y);

    // Check if window was resized — update terminal dimensions
    // Skip resize when locked (3D screen is bound and needs fixed size)
    if (!m_lockSize) {
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        int newCols = std::max(10, (int)(contentSize.x / cellSize.x));
        int newRows = std::max(5, (int)(contentSize.y / cellSize.y));
        if (newCols != m_cols || newRows != m_rows) {
            handleResize(newCols, newRows);
        }
    }

    // Use a child region so the ImGui panel scrolls when terminal is larger than window
    float totalW = m_cols * cellSize.x;
    float totalH = m_rows * cellSize.y;
    ImGui::BeginChild("TermContent", ImVec2(0, 0), false,
        ImGuiWindowFlags_HorizontalScrollbar);

    // Draw cells
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    for (int row = 0; row < m_rows; ++row) {
        for (int col = 0; col < m_cols; ++col) {
            const TermCell& tc = m_cells[row][col];
            float x = origin.x + col * cellSize.x;
            float y = origin.y + row * cellSize.y;

            // Draw background
            ImU32 bgDefault = IM_COL32(30, 30, 30, 255);
            if (tc.bg != bgDefault) {
                drawList->AddRectFilled(
                    ImVec2(x, y),
                    ImVec2(x + cellSize.x, y + cellSize.y),
                    tc.bg);
            }

            // Draw character
            if (tc.ch != ' ' && tc.ch != 0) {
                char utf8[5] = {};
                if (tc.ch < 0x80) {
                    utf8[0] = (char)tc.ch;
                } else if (tc.ch < 0x800) {
                    utf8[0] = 0xC0 | (tc.ch >> 6);
                    utf8[1] = 0x80 | (tc.ch & 0x3F);
                } else if (tc.ch < 0x10000) {
                    utf8[0] = 0xE0 | (tc.ch >> 12);
                    utf8[1] = 0x80 | ((tc.ch >> 6) & 0x3F);
                    utf8[2] = 0x80 | (tc.ch & 0x3F);
                } else {
                    utf8[0] = 0xF0 | (tc.ch >> 18);
                    utf8[1] = 0x80 | ((tc.ch >> 12) & 0x3F);
                    utf8[2] = 0x80 | ((tc.ch >> 6) & 0x3F);
                    utf8[3] = 0x80 | (tc.ch & 0x3F);
                }
                drawList->AddText(ImVec2(x, y), tc.fg, utf8);
            }

            // Underline
            if (tc.underline) {
                drawList->AddLine(
                    ImVec2(x, y + cellSize.y - 1),
                    ImVec2(x + cellSize.x, y + cellSize.y - 1),
                    tc.fg);
            }
        }
    }

    // Cursor
    m_cursorBlinkTimer += ImGui::GetIO().DeltaTime;
    if (m_cursorBlinkTimer > 1.0f) m_cursorBlinkTimer -= 1.0f;
    bool cursorOn = m_cursorBlinkTimer < 0.5f;

    if (m_cursorVisible && cursorOn &&
        m_cursorRow >= 0 && m_cursorRow < m_rows &&
        m_cursorCol >= 0 && m_cursorCol < m_cols) {
        float cx = origin.x + m_cursorCol * cellSize.x;
        float cy = origin.y + m_cursorRow * cellSize.y;
        drawList->AddRectFilled(
            ImVec2(cx, cy),
            ImVec2(cx + cellSize.x, cy + cellSize.y),
            IM_COL32(200, 200, 200, 180));
    }

    // Reserve space so ImGui knows the content size, auto-scroll to cursor
    ImGui::Dummy(ImVec2(totalW, totalH));
    float cursorY = m_cursorRow * cellSize.y;
    float scrollY = ImGui::GetScrollY();
    float visibleH = ImGui::GetWindowHeight();
    if (cursorY > scrollY + visibleH - cellSize.y * 2) {
        ImGui::SetScrollY(cursorY - visibleH + cellSize.y * 2);
    }
    ImGui::EndChild();

    ImGui::End();
    if (monoFont) ImGui::PopFont();
}

// ── handleKeyInput ─────────────────────────────────────────────────────

void EdenTerminal::handleKeyInput() {
    if (m_masterFd < 0) return;

    ImGuiIO& io = ImGui::GetIO();

    // Process text input (printable characters)
    if (!io.InputQueueCharacters.empty()) {
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            ImWchar wc = io.InputQueueCharacters[i];
            // Skip Enter/Tab/Backspace/Escape — handled as special keys below
            if (wc == '\r' || wc == '\n' || wc == '\t' || wc == 127 || wc == 27) continue;
            if (wc < 128) {
                char c = (char)wc;
                // Handle Ctrl+key combos
                if (io.KeyCtrl && wc >= 'a' && wc <= 'z') {
                    c = (char)(wc - 'a' + 1);
                }
                writeToPty(&c, 1);
            } else {
                // UTF-8 encode
                char utf8[4];
                int len = 0;
                if (wc < 0x80) {
                    utf8[0] = (char)wc; len = 1;
                } else if (wc < 0x800) {
                    utf8[0] = 0xC0 | (wc >> 6);
                    utf8[1] = 0x80 | (wc & 0x3F);
                    len = 2;
                } else {
                    utf8[0] = 0xE0 | (wc >> 12);
                    utf8[1] = 0x80 | ((wc >> 6) & 0x3F);
                    utf8[2] = 0x80 | (wc & 0x3F);
                    len = 3;
                }
                writeToPty(utf8, len);
            }
        }
        io.InputQueueCharacters.clear();
    }

    // Special keys
    auto sendEsc = [this](const char* seq) {
        writeToPty(seq, strlen(seq));
    };

    // Ctrl+C / Ctrl+D / Ctrl+Z
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_C)) sendEsc("\x03");
        if (ImGui::IsKeyPressed(ImGuiKey_D)) sendEsc("\x04");
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) sendEsc("\x1a");
        if (ImGui::IsKeyPressed(ImGuiKey_L)) sendEsc("\x0c");
        if (ImGui::IsKeyPressed(ImGuiKey_A)) sendEsc("\x01");
        if (ImGui::IsKeyPressed(ImGuiKey_E)) sendEsc("\x05");
        if (ImGui::IsKeyPressed(ImGuiKey_U)) sendEsc("\x15");
        if (ImGui::IsKeyPressed(ImGuiKey_K)) sendEsc("\x0b");
        if (ImGui::IsKeyPressed(ImGuiKey_W)) sendEsc("\x17");
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Enter))     sendEsc("\r");
    if (ImGui::IsKeyPressed(ImGuiKey_Tab))        sendEsc("\t");
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace))  sendEsc("\x7f");
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))     sendEsc("\x1b");
    if (ImGui::IsKeyPressed(ImGuiKey_Delete))     sendEsc("\x1b[3~");

    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))    sendEsc("\x1b[A");
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))  sendEsc("\x1b[B");
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) sendEsc("\x1b[C");
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  sendEsc("\x1b[D");

    if (ImGui::IsKeyPressed(ImGuiKey_Home))       sendEsc("\x1b[H");
    if (ImGui::IsKeyPressed(ImGuiKey_End))        sendEsc("\x1b[F");
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp))     sendEsc("\x1b[5~");
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown))   sendEsc("\x1b[6~");

    if (ImGui::IsKeyPressed(ImGuiKey_Insert))     sendEsc("\x1b[2~");

    // F1-F12
    if (ImGui::IsKeyPressed(ImGuiKey_F1))  sendEsc("\x1bOP");
    if (ImGui::IsKeyPressed(ImGuiKey_F2))  sendEsc("\x1bOQ");
    if (ImGui::IsKeyPressed(ImGuiKey_F3))  sendEsc("\x1bOR");
    if (ImGui::IsKeyPressed(ImGuiKey_F4))  sendEsc("\x1bOS");
    if (ImGui::IsKeyPressed(ImGuiKey_F5))  sendEsc("\x1b[15~");
    if (ImGui::IsKeyPressed(ImGuiKey_F6))  sendEsc("\x1b[17~");
    if (ImGui::IsKeyPressed(ImGuiKey_F7))  sendEsc("\x1b[18~");
    if (ImGui::IsKeyPressed(ImGuiKey_F8))  sendEsc("\x1b[19~");
    if (ImGui::IsKeyPressed(ImGuiKey_F9))  sendEsc("\x1b[20~");
    if (ImGui::IsKeyPressed(ImGuiKey_F10)) sendEsc("\x1b[21~");
    if (ImGui::IsKeyPressed(ImGuiKey_F11)) sendEsc("\x1b[23~");
    if (ImGui::IsKeyPressed(ImGuiKey_F12)) sendEsc("\x1b[24~");
}

// ── handleResize ───────────────────────────────────────────────────────

void EdenTerminal::handleResize(int newCols, int newRows) {
    if (!m_vterm || m_masterFd < 0) return;
    if (newCols == m_cols && newRows == m_rows) return;

    m_cols = newCols;
    m_rows = newRows;

    // Resize libvterm
    vterm_set_size(m_vterm, m_rows, m_cols);

    // Resize PTY
    struct winsize ws;
    ws.ws_col = m_cols;
    ws.ws_row = m_rows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    ioctl(m_masterFd, TIOCSWINSZ, &ws);

    // Resize cell buffer
    m_cells.resize(m_rows);
    for (auto& row : m_cells) {
        row.resize(m_cols);
    }

    // Re-sync
    syncCells();
}

// ── writeToPty ─────────────────────────────────────────────────────────

void EdenTerminal::writeToPty(const char* data, size_t len) {
    if (m_masterFd < 0) return;
    ssize_t ret = write(m_masterFd, data, len);
    (void)ret;
}

// ── sendCommand ────────────────────────────────────────────────────────

void EdenTerminal::sendCommand(const std::string& cmd) {
    std::string line = cmd + "\n";
    writeToPty(line.c_str(), line.size());
}

// ── shutdown ───────────────────────────────────────────────────────────

void EdenTerminal::shutdown() {
    if (m_childPid > 0) {
        kill(m_childPid, SIGTERM);
        int status;
        waitpid(m_childPid, &status, WNOHANG);
        m_childPid = -1;
    }

    if (m_masterFd >= 0) {
        close(m_masterFd);
        m_masterFd = -1;
    }

    if (m_vterm) {
        vterm_free(m_vterm);
        m_vterm = nullptr;
        m_vtermScreen = nullptr;
    }

    m_cells.clear();
}

// ── renderToPixels ─────────────────────────────────────────────────────

// Embedded 8x16 bitmap font (CP437-style) for ASCII 32-126
// Each character is 16 bytes (16 rows of 8 bits each)
#include "EdenTerminalFont.inc"

bool EdenTerminal::renderToPixels(std::vector<unsigned char>& pixelBuffer, int texWidth, int texHeight, int variant) {
    // Note: m_dirty is cleared after ALL variants are rendered in the caller's loop.
    // Here we just check if content changed or buffer is uninitialized.
    if (!m_dirty && !pixelBuffer.empty()) return false;

    // Margins for aesthetics
    const int marginLeft = 40;
    const int marginRight = 40;
    const int marginTop = 40;
    const int marginBottom = 40;

    // Compute scale dynamically to fit all terminal rows/cols in the texture
    int availW = texWidth - marginLeft - marginRight;
    int availH = texHeight - marginTop - marginBottom;
    int scaleByRows = (m_rows > 0) ? (availH / (16 * m_rows)) : 3;
    int scaleByCols = (m_cols > 0) ? (availW / (8 * m_cols)) : 3;
    int scale = std::max(1, std::min(scaleByRows, scaleByCols));
    const int cellW = 8 * scale;
    const int cellH = 16 * scale;

    // Ensure buffer is correct size
    size_t bufSize = texWidth * texHeight * 4;
    if (pixelBuffer.size() != bufSize) {
        pixelBuffer.resize(bufSize);
    }

    // Clear to terminal background
    for (size_t i = 0; i < bufSize; i += 4) {
        pixelBuffer[i + 0] = 30;
        pixelBuffer[i + 1] = 30;
        pixelBuffer[i + 2] = 30;
        pixelBuffer[i + 3] = 255;
    }

    // Clamp rendering to available area within margins
    int maxCols = availW / cellW;
    int maxRows = availH / cellH;
    int renderCols = std::min(m_cols, maxCols);
    int renderRows = std::min(m_rows, maxRows);

    // Helper to set a pixel with bounds check
    auto setPixel = [&](int px, int py, unsigned char r, unsigned char g, unsigned char b) {
        if (px >= 0 && px < texWidth && py >= 0 && py < texHeight) {
            int idx = (py * texWidth + px) * 4;
            pixelBuffer[idx + 0] = r;
            pixelBuffer[idx + 1] = g;
            pixelBuffer[idx + 2] = b;
            pixelBuffer[idx + 3] = 255;
        }
    };

    // Render each cell with margin offset
    for (int row = 0; row < renderRows; ++row) {
        for (int col = 0; col < renderCols; ++col) {
            const TermCell& tc = m_cells[row][col];

            int px0 = marginLeft + col * cellW;
            int py0 = marginTop + row * cellH;

            unsigned char fgR = (tc.fg >> 0) & 0xFF;
            unsigned char fgG = (tc.fg >> 8) & 0xFF;
            unsigned char fgB = (tc.fg >> 16) & 0xFF;
            unsigned char bgR = (tc.bg >> 0) & 0xFF;
            unsigned char bgG = (tc.bg >> 8) & 0xFF;
            unsigned char bgB = (tc.bg >> 16) & 0xFF;

            ImU32 bgDefault = IM_COL32(30, 30, 30, 255);
            if (tc.bg != bgDefault) {
                for (int y = 0; y < cellH; ++y)
                    for (int x = 0; x < cellW; ++x)
                        setPixel(px0 + x, py0 + y, bgR, bgG, bgB);
            }

            char32_t ch = tc.ch;
            if (ch >= 32 && ch <= 126) {
                const unsigned char* glyph = &kTermFont8x16[(ch - 32) * 16];
                for (int y = 0; y < 16; ++y) {
                    unsigned char bits = glyph[y];
                    for (int x = 0; x < 8; ++x) {
                        if (bits & (0x80 >> x)) {
                            for (int sy = 0; sy < scale; ++sy)
                                for (int sx = 0; sx < scale; ++sx)
                                    setPixel(px0 + x * scale + sx, py0 + y * scale + sy, fgR, fgG, fgB);
                        }
                    }
                }
            }

            if (tc.underline) {
                for (int x = 0; x < cellW; ++x) {
                    setPixel(px0 + x, py0 + cellH - 1, fgR, fgG, fgB);
                    setPixel(px0 + x, py0 + cellH - 2, fgR, fgG, fgB);
                }
            }
        }
    }

    // Visual selection highlight (blue-tinted background)
    if (m_selection.active) {
        for (int row = 0; row < renderRows; ++row) {
            for (int col = 0; col < renderCols; ++col) {
                if (isCellSelected(row, col)) {
                    int px0 = marginLeft + col * cellW;
                    int py0 = marginTop + row * cellH;
                    for (int y = 0; y < cellH; ++y)
                        for (int x = 0; x < cellW; ++x)
                            setPixel(px0 + x, py0 + y, 60, 80, 140);
                }
            }
        }
        // Re-render text on top of selection highlight
        for (int row = 0; row < renderRows; ++row) {
            for (int col = 0; col < renderCols; ++col) {
                if (!isCellSelected(row, col)) continue;
                const TermCell& tc = m_cells[row][col];
                char32_t ch = tc.ch;
                if (ch >= 32 && ch <= 126) {
                    int px0 = marginLeft + col * cellW;
                    int py0 = marginTop + row * cellH;
                    const unsigned char* glyph = &kTermFont8x16[(ch - 32) * 16];
                    for (int y = 0; y < 16; ++y) {
                        unsigned char bits = glyph[y];
                        for (int x = 0; x < 8; ++x) {
                            if (bits & (0x80 >> x)) {
                                for (int sy = 0; sy < scale; ++sy)
                                    for (int sx = 0; sx < scale; ++sx)
                                        setPixel(px0 + x * scale + sx, py0 + y * scale + sy, 220, 220, 255);
                            }
                        }
                    }
                }
            }
        }
    }

    // Copy mode cursor (bright white block)
    if (m_copyModeActive && m_copyCursorRow >= 0 && m_copyCursorRow < renderRows &&
        m_copyCursorCol >= 0 && m_copyCursorCol < renderCols) {
        int cx0 = marginLeft + m_copyCursorCol * cellW;
        int cy0 = marginTop + m_copyCursorRow * cellH;
        for (int y = 0; y < cellH; ++y)
            for (int x = 0; x < cellW; ++x)
                setPixel(cx0 + x, cy0 + y, 255, 255, 255);
        // Re-render the character under the copy cursor with dark color
        const TermCell& cc = m_cells[m_copyCursorRow][m_copyCursorCol];
        if (cc.ch >= 32 && cc.ch <= 126) {
            const unsigned char* glyph = &kTermFont8x16[(cc.ch - 32) * 16];
            for (int y = 0; y < 16; ++y) {
                unsigned char bits = glyph[y];
                for (int x = 0; x < 8; ++x) {
                    if (bits & (0x80 >> x)) {
                        for (int sy = 0; sy < scale; ++sy)
                            for (int sx = 0; sx < scale; ++sx)
                                setPixel(cx0 + x * scale + sx, cy0 + y * scale + sy, 30, 30, 30);
                    }
                }
            }
        }
    }

    // Copy mode indicator text
    if (m_copyModeActive) {
        const char* indicator = m_selection.active ? "-- VISUAL --" : "-- COPY --";
        int indicatorLen = (int)strlen(indicator);
        // Render at bottom of terminal area, centered
        int indicatorY = marginTop + renderRows * cellH + 4;
        int indicatorX = marginLeft + (renderCols * cellW - indicatorLen * 8 * scale) / 2;
        for (int i = 0; i < indicatorLen; ++i) {
            char ch = indicator[i];
            if (ch >= 32 && ch <= 126) {
                const unsigned char* glyph = &kTermFont8x16[(ch - 32) * 16];
                for (int y = 0; y < 16; ++y) {
                    unsigned char bits = glyph[y];
                    for (int x = 0; x < 8; ++x) {
                        if (bits & (0x80 >> x)) {
                            for (int sy = 0; sy < scale; ++sy)
                                for (int sx = 0; sx < scale; ++sx)
                                    setPixel(indicatorX + i * 8 * scale + x * scale + sx,
                                             indicatorY + y * scale + sy, 180, 180, 50);
                        }
                    }
                }
            }
        }
    }

    // Cursor (blink using same timer as ImGui render) — hide when copy mode active
    bool cursorOn = m_cursorBlinkTimer < 0.5f;
    if (!m_copyModeActive && m_cursorVisible && cursorOn && m_cursorRow >= 0 && m_cursorRow < renderRows && m_cursorCol >= 0 && m_cursorCol < renderCols) {
        int cx0 = marginLeft + m_cursorCol * cellW;
        int cy0 = marginTop + m_cursorRow * cellH;
        for (int y = 0; y < cellH; ++y)
            for (int x = 0; x < cellW; ++x)
                setPixel(cx0 + x, cy0 + y, 200, 200, 200);
    }

    // Apply post-process flip based on variant
    if (variant == 1 || variant == 3) {
        // Flip horizontal (full texture)
        for (int y = 0; y < texHeight; ++y) {
            for (int x = 0; x < texWidth / 2; ++x) {
                int li = (y * texWidth + x) * 4;
                int ri = (y * texWidth + (texWidth - 1 - x)) * 4;
                for (int c = 0; c < 4; ++c)
                    std::swap(pixelBuffer[li + c], pixelBuffer[ri + c]);
            }
        }
    }
    if (variant == 2 || variant == 3) {
        // Flip vertical (full texture)
        for (int y = 0; y < texHeight / 2; ++y) {
            for (int x = 0; x < texWidth; ++x) {
                int ti = (y * texWidth + x) * 4;
                int bi = ((texHeight - 1 - y) * texWidth + x) * 4;
                for (int c = 0; c < 4; ++c)
                    std::swap(pixelBuffer[ti + c], pixelBuffer[bi + c]);
            }
        }
    }

    return true;
}

// ── Copy mode methods ──────────────────────────────────────────────────

void EdenTerminal::startCopyMode() {
    m_copyModeActive = true;
    m_copyCursorRow = m_cursorRow;
    m_copyCursorCol = m_cursorCol;
    m_selection.active = false;
    m_dirty = true;
}

void EdenTerminal::moveCopyCursor(int dRow, int dCol) {
    m_copyCursorRow = std::clamp(m_copyCursorRow + dRow, 0, m_rows - 1);
    m_copyCursorCol = std::clamp(m_copyCursorCol + dCol, 0, m_cols - 1);
    if (m_selection.active) {
        m_selection.cursorRow = m_copyCursorRow;
        m_selection.cursorCol = m_copyCursorCol;
    }
    m_dirty = true;
}

void EdenTerminal::startVisualSelect() {
    m_selection.active = true;
    m_selection.anchorRow = m_copyCursorRow;
    m_selection.anchorCol = m_copyCursorCol;
    m_selection.cursorRow = m_copyCursorRow;
    m_selection.cursorCol = m_copyCursorCol;
    m_dirty = true;
}

std::string EdenTerminal::yankSelection() {
    if (!m_selection.active) {
        // No visual selection — yank current line at copy cursor
        std::string line;
        for (int col = 0; col < m_cols; ++col) {
            char32_t ch = m_cells[m_copyCursorRow][col].ch;
            if (ch < 128) line += (char)ch;
            else line += '?';
        }
        // Trim trailing spaces
        while (!line.empty() && line.back() == ' ') line.pop_back();
        cancelCopyMode();
        return line;
    }

    // Determine start/end positions
    int startRow = m_selection.anchorRow, startCol = m_selection.anchorCol;
    int endRow = m_selection.cursorRow, endCol = m_selection.cursorCol;
    if (startRow > endRow || (startRow == endRow && startCol > endCol)) {
        std::swap(startRow, endRow);
        std::swap(startCol, endCol);
    }

    std::string result;
    for (int row = startRow; row <= endRow; ++row) {
        int colStart = (row == startRow) ? startCol : 0;
        int colEnd = (row == endRow) ? endCol : (m_cols - 1);
        std::string line;
        for (int col = colStart; col <= colEnd; ++col) {
            char32_t ch = m_cells[row][col].ch;
            if (ch < 128) line += (char)ch;
            else line += '?';
        }
        // Trim trailing spaces on each line
        while (!line.empty() && line.back() == ' ') line.pop_back();
        if (row > startRow) result += '\n';
        result += line;
    }

    cancelCopyMode();
    return result;
}

void EdenTerminal::cancelCopyMode() {
    m_copyModeActive = false;
    m_selection.active = false;
    m_dirty = true;
}

bool EdenTerminal::isCellSelected(int row, int col) const {
    if (!m_selection.active) return false;
    int startRow = m_selection.anchorRow, startCol = m_selection.anchorCol;
    int endRow = m_selection.cursorRow, endCol = m_selection.cursorCol;
    if (startRow > endRow || (startRow == endRow && startCol > endCol)) {
        std::swap(startRow, endRow);
        std::swap(startCol, endCol);
    }
    if (row < startRow || row > endRow) return false;
    if (row == startRow && row == endRow) return col >= startCol && col <= endCol;
    if (row == startRow) return col >= startCol;
    if (row == endRow) return col <= endCol;
    return true;
}

// ── vtermColorToImU32 ──────────────────────────────────────────────────

ImU32 EdenTerminal::vtermColorToImU32(VTermColor color) {
    if (VTERM_COLOR_IS_RGB(&color)) {
        return IM_COL32(color.rgb.red, color.rgb.green, color.rgb.blue, 255);
    }
    // Fallback
    return IM_COL32(204, 204, 204, 255);
}

// ── libvterm callbacks ─────────────────────────────────────────────────

int EdenTerminal::onDamage(VTermRect rect, void* user) {
    (void)rect;
    (void)user;
    // We sync all cells every frame, so damage notification is not needed
    return 0;
}

int EdenTerminal::onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user) {
    (void)oldpos;
    auto* term = static_cast<EdenTerminal*>(user);
    term->m_cursorRow = pos.row;
    term->m_cursorCol = pos.col;
    term->m_cursorVisible = visible;
    return 0;
}

int EdenTerminal::onBell(void* user) {
    (void)user;
    // Could play a sound here
    return 0;
}

} // namespace eden
