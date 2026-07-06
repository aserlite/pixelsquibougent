#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>

struct AudioEngineImpl;
struct VJState;

class AudioEngine {
public:
    static constexpr int   kFftSize       = 1024;
    static constexpr int   kHistorySize   = 43;
    static constexpr float kKickLowHz     = 40.0f;
    static constexpr float kKickHighHz    = 120.0f;
    static constexpr float kSubBassLowHz  = 20.0f;
    static constexpr float kSubBassHighHz = 60.0f;
    static constexpr float kSensitivity   = 1.4f;

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&)            = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool init(const std::string& filePath, unsigned int sampleRate = 44100);
    bool initMicrophone(unsigned int sampleRate = 44100);
    bool switchSource(int sourceMode);
    void refreshCaptureDevices(VJState& state);
    bool selectCaptureDevice(int deviceIndex);
    void shutdown();

    bool  prepareOfflineAnalysis(const std::string& filePath);
    bool  analyzeTimestamp(float timestampSec, VJState& outState);
    void  finishOfflineAnalysis();
    [[nodiscard]] float getTotalDurationSeconds() const;

    [[nodiscard]] bool  kickDetected()       const;
    [[nodiscard]] float bassEnergy()         const;
    [[nodiscard]] float subBassEnergy()      const;
    [[nodiscard]] float highsEnergy()        const;
    [[nodiscard]] float detectionThreshold() const;

    [[nodiscard]] int   macroMode()          const;
    [[nodiscard]] bool  checkZScoreImpact(float k);

    void clearKick();
    void setMacroModeOverride(int mode, bool autoMode);
    void copyEnergyHistory(std::array<float, kHistorySize>& out) const;
    void copySpectrum32(std::array<float, 32>& out) const;

private:
    std::unique_ptr<AudioEngineImpl> m_impl;
};
