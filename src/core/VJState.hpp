#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

enum class AudioSourceType {
    MP3_FILE   = 0,
    MICROPHONE = 1
};

struct VJState {
    static constexpr int kHistorySize = 43;

    bool  kickEnabled    = false;
    float flashIntensity = 0.0f;
    float uiFps          = 0.0f;

    bool        shaderCompiled = false;
    std::string shaderError;

    int effectIndex   = 1;
    int impactIndex   = 0;
    int bgSourceIndex = 0;

    struct DeckItem {
        std::vector<unsigned int> textureIDs;
        int    currentFrame  = 0;
        double lastFrameTime = 0.0;
    };
    std::vector<DeckItem> deckItems;
    int  activeDeckIndex = 0;
    bool hasDeckImages   = false;

    int   targetBgSourceIndex = -1;
    bool  isTransitioning     = false;
    float transitionProgress  = 0.0f;
    float transitionDuration  = 0.3f;
    int   transitionType      = 1;

    std::array<float, 3> colBreak1 = {0.05f, 0.15f, 0.75f};
    std::array<float, 3> colBreak2 = {0.75f, 0.05f, 0.55f};
    std::array<float, 3> colDrop1  = {0.85f, 0.15f, 0.05f};
    std::array<float, 3> colDrop2  = {1.00f, 0.75f, 0.00f};

    float colorBreak[3]    = {0.486f, 0.227f, 0.933f};
    float colorDrop[3]     = {0.863f, 0.149f, 0.149f};
    float netColorBreak[3] = {0.486f, 0.227f, 0.933f};
    float netColorDrop[3]  = {0.863f, 0.149f, 0.149f};
    std::array<float, 3> netColBreak1 = {0.05f, 0.15f, 0.75f};
    std::array<float, 3> netColBreak2 = {0.75f, 0.05f, 0.55f};
    std::array<float, 3> netColDrop1  = {0.85f, 0.15f, 0.05f};
    std::array<float, 3> netColDrop2  = {1.00f, 0.75f, 0.00f};
    bool netColorsChanged = false;

    float padX = 0.5f;
    float padY = 0.5f;

    bool  autoSwitchEnabled  = false;
    float autoSwitchInterval = 8.0f;
    float autoSwitchTimer    = 0.0f;
    bool  autoSwitchArmed    = false;

    char        pendingMp3Path[512]   = {};
    bool        mp3ReloadRequested    = false;
    std::string activeMp3Path         = "";
    bool        offlineExport         = false;
    float       exportTime            = 0.0f;
    bool        exportRequested       = false;
    bool        exportCancelRequested = false;
    int         exportResIndex        = 1;
    float       exportProgress        = 0.0f;
    int         exportCurrentFrame    = 0;
    int         exportTotalFrames     = 0;
    float       exportEtaSeconds      = 0.0f;

    bool            audioActive        = false;
    AudioSourceType audioSource        = AudioSourceType::MP3_FILE;
    bool            audioSourceChanged = false;

    std::vector<std::string> captureDeviceNames;
    int                      selectedCaptureDevice = 0;
    bool                     captureDeviceChanged  = false;
    bool  kickDetected   = false;
    float bassCurrent    = 0.0f;
    float subBassCurrent = 0.0f;
    float highsCurrent   = 0.0f;
    float bassThreshold  = 0.0f;
    std::array<float, kHistorySize> bassHistory{};
    std::array<float, 32>           audioSpectrum{};

    int   macroMode     = 0;
    bool  autoMacroMode = true;
    float zScoreK       = 2.5f;
    bool  zScoreImpact  = false;

    alignas(64) mutable std::mutex netMutex;
    std::vector<uint8_t> cameraPixels;
    int                  cameraWidth  = 640;
    int                  cameraHeight = 480;

    alignas(64) std::atomic<bool>  newFrameAvailable{false};
    std::atomic<bool>              cameraActive       {false};
    std::atomic<bool>              netFlashTrigger    {false};
    std::atomic<float>             netZScoreK         {2.5f};
    std::atomic<int>               netMacroMode       {-1};
    std::atomic<int>               netEffectIndex     {-1};
    std::atomic<int>               netImpactIndex     {-1};
    std::atomic<int>               netBgSource        {-1};
    std::atomic<float>             netTransDuration   {0.3f};
    std::atomic<int>               netTransType       {1};
    std::atomic<int>               netAutoSwitch      {-1};
    std::atomic<int>               netAutoMacro       {-1};
    std::atomic<float>             netPadX            {0.5f};
    std::atomic<float>             netPadY            {0.5f};
    std::atomic<bool>              netTriggerRandomImg{false};
};
