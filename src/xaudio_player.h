#pragma once

#include <librespotc/librespotc.h>
#include <xaudio2.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace gsr {

// What the mixer should be doing right now. Written from the game thread,
// applied by the player's own mix thread so fades stay smooth even when the
// game hitches.
struct MixTarget {
    // False stops the voice without discarding queued audio, so playback
    // resumes exactly where it left off.
    bool playing = false;
    float gain = 0.0f;
    float cutoff_hz = 22050.0f;
};

class XAudioPlayer final : private IXAudio2VoiceCallback {
public:
    XAudioPlayer() = default;
    ~XAudioPlayer();

    XAudioPlayer(const XAudioPlayer&) = delete;
    XAudioPlayer& operator=(const XAudioPlayer&) = delete;

    // fade_ms controls how fast gain changes are followed; buffer_ms caps how
    // much decoded audio is held locally before applying backpressure.
    bool initialize(int fade_ms, int buffer_ms, bool use_filter);
    void shutdown();

    bool submit(const int16_t* pcm, size_t frame_count,
                const librespotc::AudioFormat& format);

    void set_target(const MixTarget& target);
    // Call once per game frame. If frames stop arriving the mixer silences
    // itself, which covers loading screens and alt-tab.
    void heartbeat();

    // True once the output has actually faded to silence and stopped.
    bool is_silent() const { return silent_.load(std::memory_order_acquire); }
    void flush();

private:
    struct Buffer;

    bool ensure_source_voice(const librespotc::AudioFormat& format);
    void mix_loop();
    void apply_step(float dt_seconds);
    void apply_filter(float cutoff_hz);

    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void* context) override;
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT error) override;

    IXAudio2* engine_ = nullptr;
    IXAudio2MasteringVoice* mastering_voice_ = nullptr;
    IXAudio2SourceVoice* source_voice_ = nullptr;

    std::mutex voice_mutex_;
    std::mutex target_mutex_;
    MixTarget target_;

    std::thread mix_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> silent_{true};
    std::atomic<size_t> queued_bytes_{0};
    std::atomic<int64_t> last_heartbeat_ms_{0};

    float current_gain_ = 0.0f;
    float current_cutoff_ = 22050.0f;
    float applied_filter_ = -1.0f;
    bool voice_started_ = false;

    float fade_seconds_ = 0.09f;
    size_t max_queued_bytes_ = 256 * 1024;
    bool use_filter_ = true;
    uint32_t sample_rate_ = 0;
    uint16_t channels_ = 0;

    static constexpr int kMixIntervalMs = 5;
    // Longer than a frame at any playable rate, short enough that a loading
    // screen does not leak a second of music.
    static constexpr int64_t kHeartbeatTimeoutMs = 350;
};

} // namespace gsr
