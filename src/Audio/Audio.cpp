// Enable all decoders
#define MA_ENABLE_VORBIS
#define MA_ENABLE_FLAC
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <eden/Audio.hpp>
#include <iostream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace eden {

struct Audio::Impl {
    ma_engine engine;
    std::vector<ma_sound*> sounds;
    std::unordered_map<int, ma_sound*> loops;
    int nextLoopId = 1;

    // Recording state
    ma_device captureDevice;
    bool captureInitialized = false;
    std::vector<int16_t> recordBuffer;
    std::mutex recordMutex;
    uint32_t sampleRate = 16000;  // 16kHz for speech recognition
    uint32_t channels = 1;        // mono
};

Audio& Audio::getInstance() {
    static Audio instance;
    return instance;
}

Audio::~Audio() {
    shutdown();
}

bool Audio::init() {
    if (m_initialized) return true;

    m_impl = std::make_unique<Impl>();

    ma_result result = ma_engine_init(nullptr, &m_impl->engine);
    if (result != MA_SUCCESS) {
        std::cerr << "Failed to initialize audio engine" << std::endl;
        m_impl.reset();
        return false;
    }

    m_initialized = true;
    return true;
}

void Audio::shutdown() {
    if (!m_initialized) return;

    // Stop recording if active
    if (m_recording && m_impl && m_impl->captureInitialized) {
        ma_device_stop(&m_impl->captureDevice);
        ma_device_uninit(&m_impl->captureDevice);
        m_impl->captureInitialized = false;
        m_recording = false;
    }

    // Clean up sounds
    if (m_impl) {
        for (auto* sound : m_impl->sounds) {
            if (sound) {
                ma_sound_uninit(sound);
                delete sound;
            }
        }
        m_impl->sounds.clear();

        // Clean up loops
        for (auto& pair : m_impl->loops) {
            if (pair.second) {
                ma_sound_uninit(pair.second);
                delete pair.second;
            }
        }
        m_impl->loops.clear();

        ma_engine_uninit(&m_impl->engine);
        m_impl.reset();
    }

    m_soundCache.clear();
    m_initialized = false;
}

int Audio::loadSound(const std::string& filepath) {
    if (!m_initialized) {
        std::cerr << "Audio not initialized" << std::endl;
        return -1;
    }

    // Check cache
    auto it = m_soundCache.find(filepath);
    if (it != m_soundCache.end()) {
        return it->second;
    }

    // Load the sound
    ma_sound* sound = new ma_sound();
    ma_result result = ma_sound_init_from_file(
        &m_impl->engine,
        filepath.c_str(),
        MA_SOUND_FLAG_DECODE,  // Decode to memory for faster playback
        nullptr, nullptr,
        sound
    );

    if (result != MA_SUCCESS) {
        std::cerr << "Failed to load sound: " << filepath << std::endl;
        delete sound;
        return -1;
    }

    int handle = m_nextHandle++;
    m_impl->sounds.push_back(sound);
    m_soundCache[filepath] = handle;

    std::cout << "Loaded sound: " << filepath << " (handle " << handle << ")" << std::endl;
    return handle;
}

void Audio::playSound(int handle, float volume) {
    if (!m_initialized || handle < 1) return;

    // Find the sound by handle
    for (const auto& pair : m_soundCache) {
        if (pair.second == handle) {
            playSound(pair.first, volume);
            return;
        }
    }
}

void Audio::playSound(const std::string& filepath, float volume) {
    if (!m_initialized) return;

    // Play the sound using the engine's one-shot API
    // Volume is baked into the sound object for proper control
    ma_sound* sound = new ma_sound();
    if (ma_sound_init_from_file(&m_impl->engine, filepath.c_str(), MA_SOUND_FLAG_DECODE,
                                nullptr, nullptr, sound) != MA_SUCCESS) {
        // Try absolute path as fallback
        std::string absPath = std::filesystem::absolute(filepath).string();
        if (ma_sound_init_from_file(&m_impl->engine, absPath.c_str(), MA_SOUND_FLAG_DECODE,
                                    nullptr, nullptr, sound) != MA_SUCCESS) {
            delete sound;
            return;
        }
    }
    ma_sound_set_volume(sound, volume);
    ma_sound_start(sound);

    // Clean up finished sounds (limit check frequency)
    if (m_impl->sounds.size() > 8) {
        m_impl->sounds.erase(
            std::remove_if(m_impl->sounds.begin(), m_impl->sounds.end(),
                [](ma_sound* s) {
                    if (!ma_sound_is_playing(s)) {
                        ma_sound_uninit(s);
                        delete s;
                        return true;
                    }
                    return false;
                }),
            m_impl->sounds.end());
    }
    m_impl->sounds.push_back(sound);
}

int Audio::startLoop(const std::string& filepath, float volume) {
    if (!m_initialized) return -1;

    // Get absolute path
    std::string absPath = filepath;
    if (!std::filesystem::path(filepath).is_absolute()) {
        absPath = std::filesystem::absolute(filepath).string();
    }

    // Create and configure the looping sound
    ma_sound* sound = new ma_sound();
    ma_result result = ma_sound_init_from_file(
        &m_impl->engine,
        absPath.c_str(),
        MA_SOUND_FLAG_DECODE,
        nullptr, nullptr,
        sound
    );

    if (result != MA_SUCCESS) {
        delete sound;
        return -1;
    }

    ma_sound_set_looping(sound, MA_TRUE);
    ma_sound_set_volume(sound, volume);
    ma_sound_start(sound);

    int loopId = m_impl->nextLoopId++;
    m_impl->loops[loopId] = sound;
    return loopId;
}

int Audio::startCrossfadeLoop(const std::string& filepath, float volume) {
    if (!m_initialized) return -1;

    std::string absPath = filepath;
    if (!std::filesystem::path(filepath).is_absolute()) {
        absPath = std::filesystem::absolute(filepath).string();
    }

    // Create first copy
    ma_sound* sound1 = new ma_sound();
    ma_result r1 = ma_sound_init_from_file(
        &m_impl->engine, absPath.c_str(), MA_SOUND_FLAG_DECODE,
        nullptr, nullptr, sound1);
    if (r1 != MA_SUCCESS) { delete sound1; return -1; }

    // Create second copy
    ma_sound* sound2 = new ma_sound();
    ma_result r2 = ma_sound_init_from_file(
        &m_impl->engine, absPath.c_str(), MA_SOUND_FLAG_DECODE,
        nullptr, nullptr, sound2);
    if (r2 != MA_SUCCESS) {
        ma_sound_uninit(sound1); delete sound1; delete sound2;
        return -1;
    }

    // Get duration and seek second copy to halfway
    float lengthSec = 0.0f;
    ma_sound_get_length_in_seconds(sound1, &lengthSec);
    ma_uint64 lengthFrames = 0;
    ma_sound_get_length_in_pcm_frames(sound1, &lengthFrames);
    ma_uint64 halfFrames = lengthFrames / 2;

    ma_sound_set_looping(sound1, MA_TRUE);
    ma_sound_set_volume(sound1, volume);
    ma_sound_set_looping(sound2, MA_TRUE);
    ma_sound_set_volume(sound2, volume);

    // Seek second sound to halfway point
    ma_sound_seek_to_pcm_frame(sound2, halfFrames);

    ma_sound_start(sound1);
    ma_sound_start(sound2);

    // Store both under consecutive IDs, return first
    int loopId1 = m_impl->nextLoopId++;
    int loopId2 = m_impl->nextLoopId++;
    m_impl->loops[loopId1] = sound1;
    m_impl->loops[loopId2] = sound2;
    return loopId1; // stopLoop on loopId1 will stop one; caller should stop loopId1+1 too
}

void Audio::stopLoop(int loopId) {
    if (!m_initialized || !m_impl) return;

    auto it = m_impl->loops.find(loopId);
    if (it != m_impl->loops.end()) {
        if (it->second) {
            ma_sound_stop(it->second);
            ma_sound_uninit(it->second);
            delete it->second;
        }
        m_impl->loops.erase(it);
    }
}

void Audio::setLoopVolume(int loopId, float volume) {
    if (!m_initialized || !m_impl) return;
    auto it = m_impl->loops.find(loopId);
    if (it != m_impl->loops.end() && it->second) {
        ma_sound_set_volume(it->second, volume);
    }
}

bool Audio::isLoopPlaying(int loopId) const {
    if (!m_initialized || !m_impl) return false;

    auto it = m_impl->loops.find(loopId);
    if (it != m_impl->loops.end() && it->second) {
        return ma_sound_is_playing(it->second);
    }
    return false;
}

// Microphone capture callback — uses raw struct pointer cast to avoid private access
struct CaptureState {
    std::vector<int16_t>* buffer;
    std::mutex* mutex;
    uint32_t channels;
};

static void captureCallback(ma_device* pDevice, void* /*pOutput*/, const void* pInput, ma_uint32 frameCount) {
    auto* state = static_cast<CaptureState*>(pDevice->pUserData);
    const int16_t* samples = static_cast<const int16_t*>(pInput);
    std::lock_guard<std::mutex> lock(*state->mutex);
    state->buffer->insert(state->buffer->end(), samples, samples + frameCount * state->channels);
}

static CaptureState g_captureState;

bool Audio::startRecording() {
    if (!m_initialized || m_recording) return false;

    // Clear previous recording
    {
        std::lock_guard<std::mutex> lock(m_impl->recordMutex);
        m_impl->recordBuffer.clear();
    }

    // Initialize capture device
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_s16;
    config.capture.channels = m_impl->channels;
    config.sampleRate = m_impl->sampleRate;
    g_captureState.buffer = &m_impl->recordBuffer;
    g_captureState.mutex = &m_impl->recordMutex;
    g_captureState.channels = m_impl->channels;
    config.dataCallback = captureCallback;
    config.pUserData = &g_captureState;

    ma_result result = ma_device_init(nullptr, &config, &m_impl->captureDevice);
    if (result != MA_SUCCESS) {
        std::cerr << "[Audio] Failed to init capture device: " << result << std::endl;
        return false;
    }
    m_impl->captureInitialized = true;

    result = ma_device_start(&m_impl->captureDevice);
    if (result != MA_SUCCESS) {
        std::cerr << "[Audio] Failed to start capture: " << result << std::endl;
        ma_device_uninit(&m_impl->captureDevice);
        m_impl->captureInitialized = false;
        return false;
    }

    m_recording = true;
    std::cout << "[Audio] Recording started (16kHz mono)" << std::endl;
    return true;
}

bool Audio::stopRecording(const std::string& outputPath) {
    if (!m_recording || !m_impl->captureInitialized) return false;

    // Stop and cleanup device
    ma_device_stop(&m_impl->captureDevice);
    ma_device_uninit(&m_impl->captureDevice);
    m_impl->captureInitialized = false;
    m_recording = false;

    // Get the recorded samples
    std::vector<int16_t> samples;
    {
        std::lock_guard<std::mutex> lock(m_impl->recordMutex);
        samples = std::move(m_impl->recordBuffer);
    }

    if (samples.empty()) {
        std::cerr << "[Audio] No audio recorded" << std::endl;
        return false;
    }

    float durationSec = static_cast<float>(samples.size()) / (m_impl->sampleRate * m_impl->channels);
    std::cout << "[Audio] Recording stopped: " << samples.size() << " samples ("
              << durationSec << "s)" << std::endl;

    // Write WAV file
    std::ofstream file(outputPath, std::ios::binary);
    if (!file) {
        std::cerr << "[Audio] Failed to open output: " << outputPath << std::endl;
        return false;
    }

    uint32_t dataSize = samples.size() * sizeof(int16_t);
    uint32_t fileSize = 36 + dataSize;
    uint16_t bitsPerSample = 16;
    uint16_t blockAlign = m_impl->channels * bitsPerSample / 8;
    uint32_t byteRate = m_impl->sampleRate * blockAlign;

    // RIFF header
    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&fileSize), 4);
    file.write("WAVE", 4);

    // fmt chunk
    file.write("fmt ", 4);
    uint32_t fmtSize = 16;
    file.write(reinterpret_cast<const char*>(&fmtSize), 4);
    uint16_t audioFormat = 1; // PCM
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);
    uint16_t ch = m_impl->channels;
    file.write(reinterpret_cast<const char*>(&ch), 2);
    file.write(reinterpret_cast<const char*>(&m_impl->sampleRate), 4);
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

    // data chunk
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&dataSize), 4);
    file.write(reinterpret_cast<const char*>(samples.data()), dataSize);

    file.close();
    std::cout << "[Audio] Saved WAV: " << outputPath << " (" << dataSize << " bytes)" << std::endl;
    return true;
}

} // namespace eden
