#pragma once
// Captures std::cout / std::cerr into an in-memory ring buffer so TED can show
// the console in an in-app window instead of a separate terminal. A tee
// streambuf forwards to the original stream (so the log file / any terminal still
// works) AND stores lines here. Thread-safe: logs come from worker threads too.

#include <iostream>
#include <streambuf>
#include <deque>
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <cstddef>

namespace eden {

class ConsoleCapture {
public:
    static ConsoleCapture& get() { static ConsoleCapture c; return c; }

    // Redirect std::cout / std::cerr through the tee. Call once, as early as
    // possible, so boot logs are captured.
    void install();

    void appendChars(const char* s, size_t n) {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (size_t i = 0; i < n; ++i) {
            char c = s[i];
            if (c == '\n') {
                m_lines.push_back(std::move(m_partial));
                m_partial.clear();
                if (m_lines.size() > kMaxLines) m_lines.pop_front();
            } else if (c != '\r') {
                m_partial += c;
            }
        }
        ++m_version;
    }

    // A copy of the lines (plus any in-progress partial line). outVersion lets a
    // viewer skip re-copying when nothing changed.
    std::vector<std::string> snapshot(uint64_t* outVersion = nullptr) {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::vector<std::string> out(m_lines.begin(), m_lines.end());
        if (!m_partial.empty()) out.push_back(m_partial);
        if (outVersion) *outVersion = m_version;
        return out;
    }
    uint64_t version() { std::lock_guard<std::mutex> lk(m_mutex); return m_version; }
    void clear() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_lines.clear(); m_partial.clear(); ++m_version;
    }

private:
    static constexpr size_t kMaxLines = 5000;
    std::mutex m_mutex;
    std::deque<std::string> m_lines;
    std::string m_partial;
    uint64_t m_version = 0;
};

// Forward to the original streambuf, then capture. Original write happens FIRST,
// so even if capture (mutex) stalls in a crash handler, the terminal/log already
// got the text.
class TeeStreamBuf : public std::streambuf {
public:
    explicit TeeStreamBuf(std::streambuf* orig) : m_orig(orig) {}
protected:
    int overflow(int c) override {
        if (traits_type::eq_int_type(c, traits_type::eof())) return c;
        char ch = traits_type::to_char_type(c);
        m_orig->sputc(ch);
        ConsoleCapture::get().appendChars(&ch, 1);
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        m_orig->sputn(s, n);
        ConsoleCapture::get().appendChars(s, static_cast<size_t>(n));
        return n;
    }
    int sync() override { return m_orig->pubsync(); }
private:
    std::streambuf* m_orig;
};

inline void ConsoleCapture::install() {
    static TeeStreamBuf coutTee(std::cout.rdbuf());
    static TeeStreamBuf cerrTee(std::cerr.rdbuf());
    std::cout.rdbuf(&coutTee);
    std::cerr.rdbuf(&cerrTee);
}

} // namespace eden
