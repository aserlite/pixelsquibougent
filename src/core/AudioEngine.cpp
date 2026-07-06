#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "AudioEngine.hpp"
#include "VJState.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <iostream>
#include <mutex>
#include <numbers>
#include <vector>

struct AudioEngineImpl {
    ma_device  device{};
    ma_decoder decoder{};
    bool       initialised   = false;
    bool       decoderLoaded = false;
    int        currentMode   = 0;
    float      sampleRate    = 44100.0f;

    ma_context                  context{};
    bool                        contextInit = false;
    std::vector<ma_device_info> captureDeviceInfos;
    int                         selectedCaptureIdx = -1;

    void initContextIfNeeded() {
        if (!contextInit) {
            if (ma_context_init(nullptr, 0, nullptr, &context) == MA_SUCCESS) {
                contextInit = true;
            }
        }
    }

    std::array<float, AudioEngine::kFftSize> sampleBuf{};
    int sampleCount = 0;

    mutable std::mutex historyMutex;
    std::array<float, AudioEngine::kHistorySize> energyHistory{};
    int historyHead = 0;

    mutable std::mutex spectrumMutex;
    std::array<float, 32> spectrum32{};

    std::atomic<bool>  kickDetected{false};
    std::atomic<float> bassEnergy  {0.0f};
    std::atomic<float> subBassEnergy{0.0f};
    std::atomic<float> highsEnergy {0.0f};
    std::atomic<float> threshold   {0.0f};
    std::atomic<float> meanEnergy  {0.0f};
    std::atomic<float> stdDevEnergy{0.0f};

    std::atomic<int>  macroMode{0};
    std::atomic<bool> autoMacroMode{true};

    std::vector<float> offlineMonoPcm;
    float              offlineDuration = 0.0f;

    static void fft(std::array<std::complex<float>, AudioEngine::kFftSize>& x) {
        constexpr int N = AudioEngine::kFftSize;
        for (int i = 1, j = 0; i < N; ++i) {
            int bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(x[i], x[j]);
        }
        for (int len = 2; len <= N; len <<= 1) {
            float ang = -2.0f * std::numbers::pi_v<float> / static_cast<float>(len);
            std::complex<float> wlen(std::cos(ang), std::sin(ang));
            for (int i = 0; i < N; i += len) {
                std::complex<float> w{1.0f, 0.0f};
                for (int j = 0; j < len / 2; ++j) {
                    auto u = x[i + j];
                    auto v = x[i + j + len / 2] * w;
                    x[i + j]           = u + v;
                    x[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
    }

    void processFrame() {
        std::array<std::complex<float>, AudioEngine::kFftSize> fftBuf{};
        for (int i = 0; i < AudioEngine::kFftSize; ++i) {
            float w = 0.5f * (1.0f - std::cos(
                2.0f * std::numbers::pi_v<float> * i / (AudioEngine::kFftSize - 1)));
            fftBuf[i] = {sampleBuf[i] * w, 0.0f};
        }
        fft(fftBuf);

        int binLow  = std::max(1, static_cast<int>(AudioEngine::kKickLowHz  * AudioEngine::kFftSize / sampleRate));
        int binHigh = std::min(AudioEngine::kFftSize / 2 - 1,
                               static_cast<int>(AudioEngine::kKickHighHz * AudioEngine::kFftSize / sampleRate));

        float energy = 0.0f;
        for (int k = binLow; k <= binHigh; ++k)
            energy += std::norm(fftBuf[k]);
        energy /= static_cast<float>(binHigh - binLow + 1);

        int subLow  = std::max(1, static_cast<int>(AudioEngine::kSubBassLowHz  * AudioEngine::kFftSize / sampleRate));
        int subHigh = std::min(AudioEngine::kFftSize / 2 - 1,
                               static_cast<int>(AudioEngine::kSubBassHighHz * AudioEngine::kFftSize / sampleRate));
        float subEnergy = 0.0f;
        for (int k = subLow; k <= subHigh; ++k)
            subEnergy += std::norm(fftBuf[k]);
        subEnergy /= static_cast<float>(subHigh - subLow + 1);

        int hiLow  = std::max(1, static_cast<int>(3000.0f * AudioEngine::kFftSize / sampleRate));
        int hiHigh = std::min(AudioEngine::kFftSize / 2 - 1,
                              static_cast<int>(8000.0f * AudioEngine::kFftSize / sampleRate));
        float hiEnergy = 0.0f;
        for (int k = hiLow; k <= hiHigh; ++k)
            hiEnergy += std::norm(fftBuf[k]);
        hiEnergy /= static_cast<float>(hiHigh - hiLow + 1);

        {
            std::lock_guard lock(historyMutex);
            energyHistory[historyHead] = energy;
            historyHead = (historyHead + 1) % AudioEngine::kHistorySize;
        }

        std::array<float, 32> spec{};
        for (int i = 0; i < 32; ++i) {
            float minBin = 1.0f;
            float maxBin = 511.0f;
            float logMin = std::log10(minBin);
            float logMax = std::log10(maxBin);

            float fLow  = std::pow(10.0f, logMin + (logMax - logMin) * (static_cast<float>(i) / 32.0f));
            float fHigh = std::pow(10.0f, logMin + (logMax - logMin) * (static_cast<float>(i + 1) / 32.0f));

            int bLow  = static_cast<int>(fLow);
            int bHigh = static_cast<int>(fHigh);
            if (bHigh <= bLow) bHigh = bLow + 1;
            if (bHigh > 512) bHigh = 512;

            float bandMag = 0.0f;
            for (int k = bLow; k < bHigh; ++k) {
                bandMag += std::norm(fftBuf[k]);
            }
            bandMag /= static_cast<float>(bHigh - bLow);
            float rms = std::sqrt(bandMag);

            float slopeComp = std::pow(1.0f + static_cast<float>(i) / 32.0f, 1.8f);
            float val = rms * 6.5f * slopeComp;
            spec[i] = std::clamp(val, 0.0f, 1.0f);
        }
        {
            std::lock_guard lock(spectrumMutex);
            spectrum32 = spec;
        }

        float mean = 0.0f;
        for (float e : energyHistory) mean += e;
        mean /= AudioEngine::kHistorySize;

        float variance = 0.0f;
        for (float e : energyHistory) {
            float diff = e - mean;
            variance += diff * diff;
        }
        variance /= AudioEngine::kHistorySize;
        float stdDev = std::sqrt(variance);

        float thr = mean * AudioEngine::kSensitivity;

        bassEnergy   .store(energy,    std::memory_order_relaxed);
        subBassEnergy.store(subEnergy, std::memory_order_relaxed);
        highsEnergy  .store(hiEnergy,  std::memory_order_relaxed);
        threshold    .store(thr,       std::memory_order_relaxed);
        meanEnergy   .store(mean,      std::memory_order_relaxed);
        stdDevEnergy .store(stdDev,    std::memory_order_relaxed);

        if (energy > thr && mean > 1e-10f)
            kickDetected.store(true, std::memory_order_release);

        if (autoMacroMode.load(std::memory_order_relaxed)) {
            if (mean > 0.005f && (energy > mean * 1.05f || subEnergy > mean * 0.8f)) {
                macroMode.store(1, std::memory_order_relaxed);
            } else if (energy < mean * 0.8f) {
                macroMode.store(0, std::memory_order_relaxed);
            }
        }
    }
};

static void audioPlaybackCallback(ma_device*  pDevice,
                                  void*       pOutput,
                                  const void*,
                                  ma_uint32   frameCount)
{
    auto* impl = static_cast<AudioEngineImpl*>(pDevice->pUserData);
    if (impl->decoderLoaded) {
        ma_decoder_read_pcm_frames(&impl->decoder, pOutput, frameCount, nullptr);
    }

    const float* pcm      = static_cast<const float*>(pOutput);
    const int    channels = static_cast<int>(pDevice->playback.channels);
    if (!pcm || channels <= 0) return;

    for (ma_uint32 f = 0; f < frameCount; ++f) {
        float mono = 0.0f;
        for (int c = 0; c < channels; ++c)
            mono += pcm[f * channels + c];
        mono /= static_cast<float>(channels);

        impl->sampleBuf[impl->sampleCount++] = mono;
        if (impl->sampleCount >= AudioEngine::kFftSize) {
            impl->processFrame();
            impl->sampleCount = 0;
        }
    }
}

static void audioCaptureCallback(ma_device*  pDevice,
                                 void*,
                                 const void* pInput,
                                 ma_uint32   frameCount)
{
    auto* impl = static_cast<AudioEngineImpl*>(pDevice->pUserData);
    const float* pcm      = static_cast<const float*>(pInput);
    const int    channels = static_cast<int>(pDevice->capture.channels);
    if (!pcm || channels <= 0) return;

    for (ma_uint32 f = 0; f < frameCount; ++f) {
        float mono = 0.0f;
        for (int c = 0; c < channels; ++c)
            mono += pcm[f * channels + c];
        mono /= static_cast<float>(channels);

        impl->sampleBuf[impl->sampleCount++] = mono * 2.0f;
        if (impl->sampleCount >= AudioEngine::kFftSize) {
            impl->processFrame();
            impl->sampleCount = 0;
        }
    }
}

AudioEngine::AudioEngine()  : m_impl(std::make_unique<AudioEngineImpl>()) {}
AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init(const std::string& filePath, unsigned int sampleRate) {
    m_impl->sampleRate = static_cast<float>(sampleRate);
    m_impl->initContextIfNeeded();

    if (ma_decoder_init_file(filePath.c_str(), nullptr, &m_impl->decoder) == MA_SUCCESS) {
        m_impl->decoderLoaded = true;
    } else {
        std::cerr << "[AudioEngine] Cannot open MP3 file: " << filePath << "\n";
        m_impl->decoderLoaded = false;
    }

    ma_device_config cfg  = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;
    cfg.playback.channels = 0;
    cfg.sampleRate        = sampleRate;
    cfg.dataCallback      = audioPlaybackCallback;
    cfg.pUserData         = m_impl.get();

    ma_context* pContext  = m_impl->contextInit ? &m_impl->context : nullptr;
    if (ma_device_init(pContext, &cfg, &m_impl->device) != MA_SUCCESS) {
        std::cerr << "[AudioEngine] Failed to open audio device.\n";
        if (m_impl->decoderLoaded) ma_decoder_uninit(&m_impl->decoder);
        return false;
    }
    if (ma_device_start(&m_impl->device) != MA_SUCCESS) {
        std::cerr << "[AudioEngine] Failed to start audio device.\n";
        ma_device_uninit(&m_impl->device);
        if (m_impl->decoderLoaded) ma_decoder_uninit(&m_impl->decoder);
        return false;
    }

    m_impl->initialised = true;
    m_impl->currentMode = 0;
    std::cout << "[AudioEngine] Streaming MP3: " << filePath << " @ " << sampleRate << " Hz\n";
    return true;
}

bool AudioEngine::initMicrophone(unsigned int sampleRate) {
    m_impl->sampleRate = static_cast<float>(sampleRate);
    m_impl->initContextIfNeeded();

    ma_device_config cfg  = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format    = ma_format_f32;
    cfg.capture.channels  = 1;
    cfg.sampleRate        = sampleRate;
    cfg.dataCallback      = audioCaptureCallback;
    cfg.pUserData         = m_impl.get();

    if (m_impl->selectedCaptureIdx >= 0 && m_impl->selectedCaptureIdx < static_cast<int>(m_impl->captureDeviceInfos.size())) {
        cfg.capture.pDeviceID = &m_impl->captureDeviceInfos[m_impl->selectedCaptureIdx].id;
    }

    ma_context* pContext  = m_impl->contextInit ? &m_impl->context : nullptr;
    if (ma_device_init(pContext, &cfg, &m_impl->device) != MA_SUCCESS) {
        std::cerr << "[AudioEngine] Failed to open capture device (Microphone).\n";
        return false;
    }
    if (ma_device_start(&m_impl->device) != MA_SUCCESS) {
        std::cerr << "[AudioEngine] Failed to start capture device.\n";
        ma_device_uninit(&m_impl->device);
        return false;
    }

    m_impl->initialised = true;
    m_impl->currentMode = 1;
    std::cout << "[AudioEngine] Capture Microphone started @ " << sampleRate << " Hz\n";
    return true;
}

bool AudioEngine::switchSource(int sourceMode) {
    if (sourceMode == m_impl->currentMode && m_impl->initialised) return true;

    if (m_impl->initialised) {
        ma_device_stop(&m_impl->device);
        ma_device_uninit(&m_impl->device);
        m_impl->initialised = false;
    }

    unsigned int sampleRate = static_cast<unsigned int>(m_impl->sampleRate);
    ma_context* pContext    = m_impl->contextInit ? &m_impl->context : nullptr;

    if (sourceMode == 0) {
        if (!m_impl->decoderLoaded) {
            std::cerr << "[AudioEngine] Cannot switch to MP3 Playback: no MP3 file loaded.\n";
            return false;
        }
        ma_device_config cfg  = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format   = ma_format_f32;
        cfg.playback.channels = 0;
        cfg.sampleRate        = sampleRate;
        cfg.dataCallback      = audioPlaybackCallback;
        cfg.pUserData         = m_impl.get();

        if (ma_device_init(pContext, &cfg, &m_impl->device) == MA_SUCCESS &&
            ma_device_start(&m_impl->device) == MA_SUCCESS) {
            m_impl->initialised = true;
            m_impl->currentMode = 0;
            std::cout << "[AudioEngine] Switched to MP3 Playback mode.\n";
            return true;
        }
        std::cerr << "[AudioEngine] Failed to switch to MP3 Playback.\n";
        return false;
    } else {
        ma_device_config cfg  = ma_device_config_init(ma_device_type_capture);
        cfg.capture.format    = ma_format_f32;
        cfg.capture.channels  = 1;
        cfg.sampleRate        = sampleRate;
        cfg.dataCallback      = audioCaptureCallback;
        cfg.pUserData         = m_impl.get();

        if (m_impl->selectedCaptureIdx >= 0 && m_impl->selectedCaptureIdx < static_cast<int>(m_impl->captureDeviceInfos.size())) {
            cfg.capture.pDeviceID = &m_impl->captureDeviceInfos[m_impl->selectedCaptureIdx].id;
        }

        if (ma_device_init(pContext, &cfg, &m_impl->device) == MA_SUCCESS &&
            ma_device_start(&m_impl->device) == MA_SUCCESS) {
            m_impl->initialised = true;
            m_impl->currentMode = 1;
            std::cout << "[AudioEngine] Switched to Microphone Capture mode.\n";
            return true;
        }
        std::cerr << "[AudioEngine] Failed to switch to Microphone Capture.\n";
        return false;
    }
}

void AudioEngine::refreshCaptureDevices(VJState& state) {
    m_impl->initContextIfNeeded();
    if (!m_impl->contextInit) return;

    ma_device_info* pPlaybackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    ma_device_info* pCaptureInfos = nullptr;
    ma_uint32 captureCount = 0;

    if (ma_context_get_devices(&m_impl->context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) == MA_SUCCESS) {
        m_impl->captureDeviceInfos.clear();
        state.captureDeviceNames.clear();

        for (ma_uint32 i = 0; i < captureCount; ++i) {
            m_impl->captureDeviceInfos.push_back(pCaptureInfos[i]);
            state.captureDeviceNames.push_back(pCaptureInfos[i].name);
        }
    }
}

bool AudioEngine::selectCaptureDevice(int deviceIndex) {
    if (!m_impl->contextInit || deviceIndex < 0 || deviceIndex >= static_cast<int>(m_impl->captureDeviceInfos.size())) {
        return false;
    }
    m_impl->selectedCaptureIdx = deviceIndex;
    if (m_impl->currentMode == 1 && m_impl->initialised) {
        return switchSource(1);
    }
    return true;
}

void AudioEngine::shutdown() {
    if (m_impl->initialised) {
        ma_device_stop(&m_impl->device);
        ma_device_uninit(&m_impl->device);
        m_impl->initialised = false;
    }
    if (m_impl->decoderLoaded) {
        ma_decoder_uninit(&m_impl->decoder);
        m_impl->decoderLoaded = false;
    }
    if (m_impl->contextInit) {
        ma_context_uninit(&m_impl->context);
        m_impl->contextInit = false;
    }
    std::cout << "[AudioEngine] Shutdown.\n";
}

bool  AudioEngine::kickDetected()       const { return m_impl->kickDetected.load(std::memory_order_acquire); }
float AudioEngine::bassEnergy()         const { return m_impl->bassEnergy.load(std::memory_order_relaxed); }
float AudioEngine::subBassEnergy()      const { return m_impl->subBassEnergy.load(std::memory_order_relaxed); }
float AudioEngine::highsEnergy()        const { return m_impl->highsEnergy.load(std::memory_order_relaxed); }
float AudioEngine::detectionThreshold() const { return m_impl->threshold.load(std::memory_order_relaxed); }
int   AudioEngine::macroMode()          const { return m_impl->macroMode.load(std::memory_order_relaxed); }

bool AudioEngine::checkZScoreImpact(float k) {
    float current = m_impl->bassEnergy.load(std::memory_order_relaxed);
    float mean    = m_impl->meanEnergy.load(std::memory_order_relaxed);
    float stdDev  = m_impl->stdDevEnergy.load(std::memory_order_relaxed);

    if (stdDev < 0.001f) stdDev = 0.001f;
    if (mean < 1e-10f) return false;

    return (current - mean) > k * stdDev;
}

void AudioEngine::clearKick() {
    m_impl->kickDetected.store(false, std::memory_order_release);
}

void AudioEngine::setMacroModeOverride(int mode, bool autoMode) {
    m_impl->autoMacroMode.store(autoMode, std::memory_order_relaxed);
    if (!autoMode) {
        m_impl->macroMode.store(mode, std::memory_order_relaxed);
    }
}

void AudioEngine::copyEnergyHistory(std::array<float, kHistorySize>& out) const {
    std::lock_guard lock(m_impl->historyMutex);
    for (int i = 0; i < kHistorySize; ++i)
        out[i] = m_impl->energyHistory[(m_impl->historyHead + i) % kHistorySize];
}

void AudioEngine::copySpectrum32(std::array<float, 32>& out) const {
    std::lock_guard lock(m_impl->spectrumMutex);
    out = m_impl->spectrum32;
}

bool AudioEngine::prepareOfflineAnalysis(const std::string& filePath) {
    finishOfflineAnalysis();
    ma_decoder decoder{};
    if (ma_decoder_init_file(filePath.c_str(), nullptr, &decoder) != MA_SUCCESS) {
        std::cerr << "[AudioEngine] prepareOfflineAnalysis: Cannot open MP3 file: " << filePath << "\n";
        return false;
    }

    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) != MA_SUCCESS || totalFrames == 0) {
        std::cerr << "[AudioEngine] prepareOfflineAnalysis: Cannot get length in PCM frames.\n";
        ma_decoder_uninit(&decoder);
        return false;
    }

    int channels = static_cast<int>(decoder.outputChannels);
    std::vector<float> rawPcm(totalFrames * channels);
    ma_uint64 framesRead = 0;
    ma_decoder_read_pcm_frames(&decoder, rawPcm.data(), totalFrames, &framesRead);
    ma_decoder_uninit(&decoder);

    if (framesRead == 0) {
        std::cerr << "[AudioEngine] prepareOfflineAnalysis: 0 frames read.\n";
        return false;
    }

    m_impl->offlineMonoPcm.resize(framesRead);
    for (ma_uint64 i = 0; i < framesRead; ++i) {
        float mono = 0.0f;
        for (int c = 0; c < channels; ++c) {
            mono += rawPcm[i * channels + c];
        }
        m_impl->offlineMonoPcm[i] = (mono / static_cast<float>(channels)) * 2.0f;
    }

    m_impl->offlineDuration = static_cast<float>(framesRead) / m_impl->sampleRate;
    std::cout << "[AudioEngine] Offline analysis prepared: " << framesRead << " PCM frames (" << m_impl->offlineDuration << "s).\n";
    return true;
}

void AudioEngine::finishOfflineAnalysis() {
    m_impl->offlineMonoPcm.clear();
    m_impl->offlineMonoPcm.shrink_to_fit();
    m_impl->offlineDuration = 0.0f;
}

float AudioEngine::getTotalDurationSeconds() const {
    return m_impl->offlineDuration;
}

bool AudioEngine::analyzeTimestamp(float timestampSec, VJState& outState) {
    if (m_impl->offlineMonoPcm.empty()) return false;

    int centerIdx = static_cast<int>(timestampSec * m_impl->sampleRate);
    int startIdx  = centerIdx - kFftSize;

    for (int i = 0; i < kFftSize; ++i) {
        int idx = startIdx + i;
        if (idx >= 0 && idx < static_cast<int>(m_impl->offlineMonoPcm.size())) {
            m_impl->sampleBuf[i] = m_impl->offlineMonoPcm[idx];
        } else {
            m_impl->sampleBuf[i] = 0.0f;
        }
    }

    m_impl->processFrame();

    outState.bassCurrent    = m_impl->bassEnergy.load(std::memory_order_relaxed);
    outState.subBassCurrent = m_impl->subBassEnergy.load(std::memory_order_relaxed);
    outState.highsCurrent   = m_impl->highsEnergy.load(std::memory_order_relaxed);
    outState.bassThreshold  = m_impl->threshold.load(std::memory_order_relaxed);
    copyEnergyHistory(outState.bassHistory);
    copySpectrum32(outState.audioSpectrum);

    outState.kickDetected = kickDetected();
    outState.zScoreImpact = checkZScoreImpact(outState.zScoreK);
    clearKick();

    bool kickFired = outState.kickDetected || outState.zScoreImpact;
    if (kickFired) {
        outState.flashIntensity = 1.0f;
    }
    outState.macroMode = macroMode();

    return true;
}
