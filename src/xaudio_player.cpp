#include "xaudio_player.h"

#include "log.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <sstream>
#include <vector>

namespace gsr {

struct XAudioPlayer::Buffer {
    XAUDIO2_BUFFER xa{};
    std::vector<uint8_t> bytes;
};

XAudioPlayer::~XAudioPlayer() {
    std::lock_guard lock(mutex_);
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

bool XAudioPlayer::initialize() {
    std::lock_guard lock(mutex_);
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
    log::info("XAudio2 output initialized");
    return true;
}

bool XAudioPlayer::ensure_source_voice(const librespotc::AudioFormat& format) {
    if (source_voice_ && sample_rate_ == format.sample_rate &&
        channels_ == format.channels) {
        return true;
    }
    if (source_voice_) {
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

    const HRESULT hr = engine_->CreateSourceVoice(
        &source_voice_, &wave, 0, XAUDIO2_DEFAULT_FREQ_RATIO, this);
    if (FAILED(hr)) {
        log::error("CreateSourceVoice failed: " + std::to_string(hr));
        source_voice_ = nullptr;
        return false;
    }
    sample_rate_ = format.sample_rate;
    channels_ = format.channels;
    source_voice_->SetVolume(active_.load() ? volume_ : 0.0f);
    source_voice_->Start(0);
    log::info("Spotify PCM voice created: " + std::to_string(sample_rate_) +
              " Hz, " + std::to_string(channels_) + " channels");
    return true;
}

bool XAudioPlayer::submit(const int16_t* pcm, size_t frame_count,
                          const librespotc::AudioFormat& format) {
    if (!pcm || frame_count == 0 || format.bits_per_sample != 16 ||
        format.channels == 0) {
        return true;
    }
    const size_t byte_count = frame_count * format.channels * sizeof(int16_t);
    if (queued_bytes_.load(std::memory_order_acquire) + byte_count >
        kMaxQueuedBytes) {
        return false;
    }

    std::lock_guard lock(mutex_);
    if (!engine_ || !ensure_source_voice(format)) return false;

    XAUDIO2_VOICE_STATE state{};
    source_voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if (state.BuffersQueued >= 48) return false;

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

void XAudioPlayer::set_active(bool active) {
    active_.store(active, std::memory_order_release);
    std::lock_guard lock(mutex_);
    if (source_voice_) source_voice_->SetVolume(active ? volume_ : 0.0f);
}

void XAudioPlayer::set_volume(float volume) {
    volume_ = std::clamp(volume, 0.0f, 2.0f);
    std::lock_guard lock(mutex_);
    if (source_voice_) {
        source_voice_->SetVolume(active_.load() ? volume_ : 0.0f);
    }
}

void XAudioPlayer::clear() {
    std::lock_guard lock(mutex_);
    if (!source_voice_) return;
    source_voice_->Stop(0);
    source_voice_->FlushSourceBuffers();
    source_voice_->Start(0);
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
