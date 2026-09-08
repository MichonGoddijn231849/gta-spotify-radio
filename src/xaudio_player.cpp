#include "xaudio_player.h"

#include "log.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

float move_towards(float current, float target, float max_delta) {
    if (target > current) return std::min(target, current + max_delta);
    return std::max(target, current - max_delta);
}

} // namespace

namespace gsr {

struct XAudioPlayer::Buffer {
    XAUDIO2_BUFFER xa{};
    std::vector<uint8_t> bytes;
};

XAudioPlayer::~XAudioPlayer() {
    shutdown();
}

bool XAudioPlayer::initialize(int fade_ms, int buffer_ms, bool use_filter) {
    fade_seconds_ = std::max(fade_ms, 1) / 1000.0f;
    use_filter_ = use_filter;
    // 16-bit stereo at 48 kHz is the worst case we are likely to see.
    max_queued_bytes_ = static_cast<size_t>(buffer_ms) * 48 * 2 * 2;

    {
        std::lock_guard lock(voice_mutex_);
        if (engine_) return true;

        HRESULT hr = XAudio2Create(&engine_, 0, XAUDIO2_DEFAULT_PROCESSOR);
        if (FAILED(hr)) {
            log::error("XAudio2Create failed: " + std::to_string(hr));
            return false;
        }
        hr = engine_->CreateMasteringVoice(&mastering_voice_);
        if (FAILED(hr)) {
            log::error("CreateMasteringVoice failed: " + std::to_string(hr));
            engine_->Release();
            engine_ = nullptr;
            return false;
        }
    }

    last_heartbeat_ms_.store(now_ms(), std::memory_order_release);
    running_.store(true, std::memory_order_release);
    mix_thread_ = std::thread(&XAudioPlayer::mix_loop, this);
    log::info("XAudio2 output initialized (" + std::to_string(buffer_ms) + " ms buffer, " +
              std::to_string(fade_ms) + " ms fades)");
    return true;
}

void XAudioPlayer::shutdown() {
    if (running_.exchange(false, std::memory_order_acq_rel)) {
        if (mix_thread_.joinable()) mix_thread_.join();
    }

    std::lock_guard lock(voice_mutex_);
    if (source_voice_) {
        source_voice_->Stop(0);
        source_voice_->FlushSourceBuffers();
        source_voice_->DestroyVoice();
        source_voice_ = nullptr;
    }
    if (mastering_voice_) {
        mastering_voice_->DestroyVoice();
        mastering_voice_ = nullptr;
    }
    if (engine_) {
        engine_->Release();
        engine_ = nullptr;
    }
}

bool XAudioPlayer::ensure_source_voice(const librespotc::AudioFormat& format) {
    if (source_voice_) {
        if (sample_rate_ == format.sample_rate && channels_ == format.channels) return true;
        log::error("Spotify changed PCM format while audio was active");
        return false;
    }

    WAVEFORMATEX wave{};
    wave.wFormatTag = WAVE_FORMAT_PCM;
    wave.nChannels = format.channels;
    wave.nSamplesPerSec = format.sample_rate;
    wave.wBitsPerSample = 16;
    wave.nBlockAlign = static_cast<WORD>(wave.nChannels * wave.wBitsPerSample / 8);
    wave.nAvgBytesPerSec = wave.nSamplesPerSec * wave.nBlockAlign;

    const UINT32 flags = use_filter_ ? XAUDIO2_VOICE_USEFILTER : 0u;
    const HRESULT hr = engine_->CreateSourceVoice(
        &source_voice_, &wave, flags, XAUDIO2_DEFAULT_FREQ_RATIO, this);
    if (FAILED(hr)) {
        log::error("CreateSourceVoice failed: " + std::to_string(hr));
        source_voice_ = nullptr;
        return false;
    }
    sample_rate_ = format.sample_rate;
    channels_ = format.channels;
    // The mix thread owns Start/Stop and volume from here on.
    source_voice_->SetVolume(current_gain_);
    applied_filter_ = -1.0f;
    log::info("Spotify PCM voice created: " + std::to_string(sample_rate_) + " Hz, " +
              std::to_string(channels_) + " channels");
    return true;
}

bool XAudioPlayer::submit(const int16_t* pcm, size_t frame_count,
                          const librespotc::AudioFormat& format) {
    if (!pcm || frame_count == 0 || format.bits_per_sample != 16 || format.channels == 0) {
        return true;
    }
    const size_t byte_count = frame_count * format.channels * sizeof(int16_t);
    if (queued_bytes_.load(std::memory_order_acquire) + byte_count > max_queued_bytes_) {
        return false;
    }

    std::lock_guard lock(voice_mutex_);
    if (!engine_ || !ensure_source_voice(format)) return false;

    XAUDIO2_VOICE_STATE state{};
    source_voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if (state.BuffersQueued >= XAUDIO2_MAX_QUEUED_BUFFERS - 4) return false;

    auto buffer = std::make_unique<Buffer>();
    buffer->bytes.resize(byte_count);
    std::memcpy(buffer->bytes.data(), pcm, byte_count);
    buffer->xa.AudioBytes = static_cast<UINT32>(buffer->bytes.size());
    buffer->xa.pAudioData = buffer->bytes.data();
    buffer->xa.pContext = buffer.get();

    queued_bytes_.fetch_add(byte_count, std::memory_order_release);
    const HRESULT hr = source_voice_->SubmitSourceBuffer(&buffer->xa);
    if (FAILED(hr)) {
        queued_bytes_.fetch_sub(byte_count, std::memory_order_release);
        log::error("SubmitSourceBuffer failed: " + std::to_string(hr));
        return false;
    }
    buffer.release();
    return true;
}

void XAudioPlayer::set_target(const MixTarget& target) {
    std::lock_guard lock(target_mutex_);
    target_ = target;
}

void XAudioPlayer::heartbeat() {
    last_heartbeat_ms_.store(now_ms(), std::memory_order_release);
}

void XAudioPlayer::flush() {
    std::lock_guard lock(voice_mutex_);
    if (!source_voice_) return;
    source_voice_->Stop(0);
    source_voice_->FlushSourceBuffers();
    voice_started_ = false;
}

void XAudioPlayer::mix_loop() {
    const float dt = kMixIntervalMs / 1000.0f;
    while (running_.load(std::memory_order_acquire)) {
        apply_step(dt);
        Sleep(kMixIntervalMs);
    }
}

void XAudioPlayer::apply_step(float dt_seconds) {
    MixTarget target;
    {
        std::lock_guard lock(target_mutex_);
        target = target_;
    }

    // No game frames for a while means the game is not running our script:
    // a loading screen, a stall, or the process losing focus.
    const int64_t stale = now_ms() - last_heartbeat_ms_.load(std::memory_order_acquire);
    if (stale > kHeartbeatTimeoutMs) {
        target.playing = false;
        target.gain = 0.0f;
    }

    const float wanted_gain = target.playing ? std::max(target.gain, 0.0f) : 0.0f;
    current_gain_ = move_towards(current_gain_, wanted_gain, dt_seconds / fade_seconds_);
    if (std::fabs(current_gain_ - wanted_gain) < 0.0005f) current_gain_ = wanted_gain;

    // Slide the corner frequency geometrically so a sweep sounds even.
    const float wanted_cutoff = std::clamp(target.cutoff_hz, 100.0f, 22050.0f);
    const float ratio = std::pow(wanted_cutoff / std::max(current_cutoff_, 1.0f),
                                 std::min(dt_seconds / 0.12f, 1.0f));
    current_cutoff_ = std::clamp(current_cutoff_ * ratio, 100.0f, 22050.0f);

    const bool should_run = target.playing || current_gain_ > 0.0f;
    silent_.store(!should_run, std::memory_order_release);

    std::lock_guard lock(voice_mutex_);
    if (!source_voice_) return;

    source_voice_->SetVolume(current_gain_);
    apply_filter(current_cutoff_);

    if (should_run && !voice_started_) {
        source_voice_->Start(0);
        voice_started_ = true;
    } else if (!should_run && voice_started_) {
        // Stop keeps the queued buffers, so the track resumes where it paused.
        source_voice_->Stop(0);
        voice_started_ = false;
    }
}

void XAudioPlayer::apply_filter(float cutoff_hz) {
    if (!use_filter_ || sample_rate_ == 0) return;

    // XAudio2's built-in filter tops out at nyquist/3, and its Frequency
    // parameter is 2*sin(pi*fc/fs) rather than hertz.
    const float max_cutoff = static_cast<float>(sample_rate_) / 6.0f;
    float frequency = XAUDIO2_MAX_FILTER_FREQUENCY;
    if (cutoff_hz < max_cutoff) {
        frequency = 2.0f * std::sin(3.14159265f * cutoff_hz / static_cast<float>(sample_rate_));
        frequency = std::clamp(frequency, 0.01f, XAUDIO2_MAX_FILTER_FREQUENCY);
    }
    if (std::fabs(frequency - applied_filter_) < 0.002f) return;
    applied_filter_ = frequency;

    XAUDIO2_FILTER_PARAMETERS params{};
    params.Type = LowPassFilter;
    params.Frequency = frequency;
    params.OneOverQ = 1.0f;
    source_voice_->SetFilterParameters(&params);
}

void STDMETHODCALLTYPE XAudioPlayer::OnBufferEnd(void* context) {
    auto* buffer = static_cast<Buffer*>(context);
    if (!buffer) return;
    queued_bytes_.fetch_sub(buffer->bytes.size(), std::memory_order_release);
    delete buffer;
}

void STDMETHODCALLTYPE XAudioPlayer::OnVoiceError(void*, HRESULT error) {
    log::error("XAudio2 voice error: " + std::to_string(error));
}

} // namespace gsr
