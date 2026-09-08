#pragma once

#include <librespotc/librespotc.h>
#include <xaudio2.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace gsr {

class XAudioPlayer final : private IXAudio2VoiceCallback {
public:
    XAudioPlayer() = default;
    ~XAudioPlayer();

    XAudioPlayer(const XAudioPlayer&) = delete;
    XAudioPlayer& operator=(const XAudioPlayer&) = delete;

    bool initialize();
    bool submit(const int16_t* pcm, size_t frame_count,
                const librespotc::AudioFormat& format);
    void set_active(bool active);
    void set_volume(float volume);
    void clear();

private:
    struct Buffer;

    bool ensure_source_voice(const librespotc::AudioFormat& format);

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
    std::mutex mutex_;
    std::atomic<size_t> queued_bytes_{0};
    std::atomic<bool> active_{false};
    float volume_ = 0.8f;
    uint32_t sample_rate_ = 0;
    uint16_t channels_ = 0;
    static constexpr size_t kMaxQueuedBytes = 768 * 1024;
};

} // namespace gsr
