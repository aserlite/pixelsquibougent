#define GLFW_INCLUDE_NONE
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "EngineOrchestrator.hpp"

#include "AudioEngine.hpp"
#include "DeckLoader.hpp"
#include "RenderEngine.hpp"
#include "VJState.hpp"
#include "VideoExport.hpp"
#include "gui/Interface.hpp"
#include "network/WebServer.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <iostream>
#include <mutex>

using json = nlohmann::json;

struct EngineOrchestrator::Impl {
    VJState       state;
    AudioEngine   audio;
    RenderEngine  renderer;
    Interface     ui;
    WebServer     server;
    VideoExport   videoExport;

    bool audioOk = false;

    int   lastSyncBg         = 0;
    int   lastSyncEffect     = 0;
    int   lastSyncImpact     = 0;
    int   lastSyncTransType  = 0;
    float lastSyncTransDur   = 0.0f;
    bool  lastSyncAutoSwitch = false;
    bool  lastSyncAutoMacro  = false;
    int   lastSyncMode       = 0;

    void syncSnapshot() {
        lastSyncBg         = state.isTransitioning ? state.targetBgSourceIndex : state.bgSourceIndex;
        lastSyncEffect     = state.effectIndex;
        lastSyncImpact     = state.impactIndex;
        lastSyncTransType  = state.transitionType;
        lastSyncTransDur   = state.transitionDuration;
        lastSyncAutoSwitch = state.autoSwitchEnabled;
        lastSyncAutoMacro  = state.autoMacroMode;
        lastSyncMode       = state.macroMode;
    }

    void sendSyncState() {
        json j;
        j["type"]       = "sync_state";
        j["bgSource"]   = state.isTransitioning ? state.targetBgSourceIndex : state.bgSourceIndex;
        j["effect"]     = state.effectIndex;
        j["impact"]     = state.impactIndex;
        j["transType"]  = state.transitionType;
        j["transDur"]   = state.transitionDuration;
        j["autoSwitch"] = state.autoSwitchEnabled;
        j["autoMacro"]  = state.autoMacroMode;
        j["mode"]       = state.macroMode;
        j["hasDeck"]    = state.hasDeckImages;
        j["deckIdx"]    = state.activeDeckIndex;
        server.broadcast(j.dump());
        syncSnapshot();
    }

    bool stateDirty() const {
        int currentBg = state.isTransitioning ? state.targetBgSourceIndex : state.bgSourceIndex;
        return currentBg != lastSyncBg
            || state.effectIndex    != lastSyncEffect
            || state.impactIndex    != lastSyncImpact
            || state.transitionType != lastSyncTransType
            || std::abs(state.transitionDuration - lastSyncTransDur) > 0.01f
            || state.autoSwitchEnabled != lastSyncAutoSwitch
            || state.autoMacroMode     != lastSyncAutoMacro
            || state.macroMode         != lastSyncMode;
    }

    void consumeNetworkCommands() {
        if (state.netFlashTrigger.exchange(false, std::memory_order_acq_rel))
            state.flashIntensity = 1.0f;

        int netMode = state.netMacroMode.load(std::memory_order_relaxed);
        if (netMode >= 0) {
            state.macroMode     = netMode;
            state.autoMacroMode = false;
            state.netMacroMode.store(-1, std::memory_order_relaxed);
        }

        float remoteK = state.netZScoreK.load(std::memory_order_relaxed);
        if (remoteK != state.zScoreK) state.zScoreK = remoteK;

        int netEff = state.netEffectIndex.exchange(-1, std::memory_order_acq_rel);
        if (netEff >= 0 && netEff <= 5) { state.effectIndex = netEff; sendSyncState(); }

        int netImp = state.netImpactIndex.exchange(-1, std::memory_order_acq_rel);
        if (netImp >= 0 && netImp <= 3) { state.impactIndex = netImp; sendSyncState(); }

        float netDur = state.netTransDuration.load(std::memory_order_relaxed);
        if (netDur != state.transitionDuration) state.transitionDuration = netDur;

        int netType = state.netTransType.load(std::memory_order_relaxed);
        if (netType != state.transitionType) state.transitionType = netType;

        int netAS = state.netAutoSwitch.exchange(-1, std::memory_order_acq_rel);
        if (netAS >= 0) state.autoSwitchEnabled = (netAS == 1);

        int netAM = state.netAutoMacro.exchange(-1, std::memory_order_acq_rel);
        if (netAM >= 0) state.autoMacroMode = (netAM == 1);

        state.padX = state.netPadX.load(std::memory_order_relaxed);
        state.padY = state.netPadY.load(std::memory_order_relaxed);

        if (state.netColorsChanged) {
            std::lock_guard<std::mutex> lock(state.netMutex);
            for (int i = 0; i < 3; ++i) {
                state.colorBreak[i] = state.netColorBreak[i];
                state.colorDrop[i]  = state.netColorDrop[i];
                state.colBreak1[i]  = state.netColBreak1[i];
                state.colBreak2[i]  = state.netColBreak2[i];
                state.colDrop1[i]   = state.netColDrop1[i];
                state.colDrop2[i]   = state.netColDrop2[i];
            }
            state.netColorsChanged = false;
        }

        int netBg = state.netBgSource.exchange(-1, std::memory_order_acq_rel);
        if (netBg >= 0 && netBg <= 9) {
            if (!state.isTransitioning && netBg != state.bgSourceIndex) {
                state.targetBgSourceIndex = netBg;
                state.isTransitioning     = true;
                state.transitionProgress  = 0.0f;
            } else if (!state.isTransitioning) {
                state.bgSourceIndex   = netBg;
                state.isTransitioning = false;
            }
            sendSyncState();
        }

        if ((state.bgSourceIndex == 8 || state.targetBgSourceIndex == 8) && !state.cameraActive.load()) {
            state.bgSourceIndex   = 0;
            state.isTransitioning = false;
        }
    }

    void advanceTransition(double dt) {
        if (!state.isTransitioning) return;
        state.transitionProgress += static_cast<float>(dt) / state.transitionDuration;
        if (state.transitionProgress >= 1.0f) {
            state.bgSourceIndex      = state.targetBgSourceIndex;
            state.isTransitioning    = false;
            state.transitionProgress = 0.0f;
            sendSyncState();
        }
    }

    void handleAudioSourceSwitch() {
        if (state.audioSourceChanged) {
            state.audioSourceChanged = false;
            int targetMode = (state.audioSource == AudioSourceType::MICROPHONE) ? 1 : 0;
            audioOk = audio.switchSource(targetMode);
            state.audioActive = audioOk;
        }
        if (state.captureDeviceChanged) {
            state.captureDeviceChanged = false;
            if (state.audioSource == AudioSourceType::MICROPHONE) {
                if (audio.selectCaptureDevice(state.selectedCaptureDevice)) {
                    audioOk           = true;
                    state.audioActive = true;
                }
            }
        }
    }

    bool tickAudio() {
        if (!audioOk) return false;

        state.bassCurrent    = audio.bassEnergy();
        state.subBassCurrent = audio.subBassEnergy();
        state.highsCurrent   = audio.highsEnergy();
        state.bassThreshold  = audio.detectionThreshold();
        audio.copyEnergyHistory(state.bassHistory);

        state.kickDetected = audio.kickDetected();
        state.zScoreImpact = audio.checkZScoreImpact(state.zScoreK);
        audio.clearKick();

        const bool kickFired = state.kickDetected || state.zScoreImpact;
        if (kickFired) state.flashIntensity = 1.0f;

        state.kickDetected = false;
        state.zScoreImpact = false;

        audio.setMacroModeOverride(state.macroMode, state.autoMacroMode);
        if (state.autoMacroMode) state.macroMode = audio.macroMode();

        return kickFired;
    }

    void handleMp3HotReload() {
        if (!state.mp3ReloadRequested) return;
        state.mp3ReloadRequested = false;
        audio.shutdown();
        audioOk = audio.init(state.pendingMp3Path);
        state.audioActive = audioOk;
        if (audioOk) {
            state.activeMp3Path = state.pendingMp3Path;
            std::cout << "[Engine] MP3 hot-reloaded: " << state.pendingMp3Path << "\n";
        } else {
            std::cerr << "[Engine] Failed to load: " << state.pendingMp3Path << "\n";
        }
    }

    void tickAutoSwitch(double dt, bool kickFired) {
        if (!state.autoSwitchEnabled || !audioOk) return;

        state.autoSwitchTimer += static_cast<float>(dt);

        if (!state.autoSwitchArmed && state.autoSwitchTimer >= state.autoSwitchInterval) {
            state.autoSwitchArmed = true;
            sendSyncState();
        }

        if (!state.autoSwitchArmed || !kickFired) return;

        constexpr int kMaxBg = 10;
        int nextBg = (state.bgSourceIndex + 1) % kMaxBg;
        if (nextBg == 8 && !state.cameraActive.load()) nextBg = 9;
        if (nextBg == 9 && !state.hasDeckImages) nextBg = 0;

        if (!state.isTransitioning && nextBg != state.bgSourceIndex) {
            state.targetBgSourceIndex = nextBg;
            state.isTransitioning     = true;
            state.transitionProgress  = 0.0f;
        } else if (!state.isTransitioning) {
            state.bgSourceIndex = nextBg;
        }

        state.effectIndex     = (state.effectIndex + 1) % 6;
        state.autoSwitchTimer = 0.0f;
        state.autoSwitchArmed = false;
        sendSyncState();
    }

    void tickDeck(double now, bool kickFired) {
        if (!state.hasDeckImages || state.deckItems.empty()) return;

        bool trigger = state.netTriggerRandomImg.exchange(false, std::memory_order_acq_rel);
        if (!trigger && state.autoSwitchEnabled && state.bgSourceIndex == 9
            && kickFired && state.flashIntensity > 0.15f)
            trigger = true;

        if (trigger) {
            state.activeDeckIndex = static_cast<int>(rand() % state.deckItems.size());
            sendSyncState();
        }

        const int idx = state.activeDeckIndex % static_cast<int>(state.deckItems.size());
        auto& item = state.deckItems[idx];
        if (item.textureIDs.size() > 1 && (now - item.lastFrameTime) >= 0.05) {
            item.lastFrameTime = now;
            item.currentFrame  = (item.currentFrame + 1) % static_cast<int>(item.textureIDs.size());
        }
    }

    void broadcastSpectrum(double now) {
        static double lastBroadcast = 0.0;
        if (!audioOk || (now - lastBroadcast) < 0.05) return;
        lastBroadcast = now;
        audio.copySpectrum32(state.audioSpectrum);
        json j;
        j["type"] = "audio_peaks";
        j["data"] = state.audioSpectrum;
        server.broadcast(j.dump());
    }

    void renderUIWindow() {
        glfwMakeContextCurrent(renderer.uiWindow());
        int uiW, uiH;
        glfwGetFramebufferSize(renderer.uiWindow(), &uiW, &uiH);
        glViewport(0, 0, uiW, uiH);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ui.beginFrame();
        ui.render(state);
        ui.endFrame();
        glfwSwapBuffers(renderer.uiWindow());
    }

    void renderOutputWindow(int outW, int outH) {
        if (state.exportRequested) {
            state.exportRequested = false;
            const int expW = (state.exportResIndex == 0) ? 1280 : 1920;
            const int expH = (state.exportResIndex == 0) ? 720  : 1080;
            videoExport.startExport(state.activeMp3Path, "output.mp4", expW, expH, 60, state, renderer, audio);
        }

        if (videoExport.isExporting())
            videoExport.processStep(state, renderer, audio, outW, outH);
        else
            renderer.renderOutput(state, outW, outH);

        glfwSwapBuffers(renderer.outputWindow());
    }

    void cleanup() {
        for (auto& item : state.deckItems) {
            if (!item.textureIDs.empty())
                glDeleteTextures(static_cast<GLsizei>(item.textureIDs.size()), item.textureIDs.data());
        }
        state.deckItems.clear();

        videoExport.shutdown();
        server.stop();
        ui.shutdown();
        audio.shutdown();
        renderer.shutdown();
    }
};

EngineOrchestrator::EngineOrchestrator()  : m_impl(std::make_unique<Impl>()) {}
EngineOrchestrator::~EngineOrchestrator() = default;

bool EngineOrchestrator::init(int argc, char* argv[]) {
    auto& d = *m_impl;

    std::cout << "VJ Engine v0.7.0\n";

    if (argc >= 2) {
        d.audioOk = d.audio.init(argv[1]);
        if (!d.audioOk)
            std::cerr << "[Engine] Failed to init audio from: " << argv[1] << "\n";
    } else {
        std::cout << "[Engine] No audio file. Run: ./VJ <file.mp3>\n";
    }

    if (!d.renderer.init()) return false;

    glfwMakeContextCurrent(d.renderer.uiWindow());
    if (!d.ui.init(d.renderer.uiWindow())) {
        d.renderer.shutdown();
        return false;
    }

    if (d.audioOk && argc >= 2) {
        d.state.activeMp3Path = argv[1];
        d.state.zScoreK       = d.audio.detectionThreshold();
    }
    d.state.audioActive = d.audioOk;

    glfwMakeContextCurrent(d.renderer.outputWindow());
    d.renderer.loadShaders(d.state);

    DeckLoader::loadDeck(d.state);

    glfwMakeContextCurrent(d.renderer.uiWindow());

    d.server.start(d.state, 18080);

    d.syncSnapshot();

    return true;
}

void EngineOrchestrator::run() {
    auto& d = *m_impl;

    double prevTime = glfwGetTime();

    while (!d.renderer.shouldClose()) {
        const double now = glfwGetTime();
        const double dt  = now - prevTime;
        prevTime = now;
        d.state.uiFps = (dt > 0.0) ? static_cast<float>(1.0 / dt) : 0.0f;

        glfwPollEvents();

        d.consumeNetworkCommands();
        d.advanceTransition(dt);
        d.handleAudioSourceSwitch();
        d.handleMp3HotReload();

        const bool kickFired = d.tickAudio();

        d.tickAutoSwitch(dt, kickFired);
        d.tickDeck(now, kickFired);

        constexpr float kFlashDecay = 5.5f;
        d.state.flashIntensity *= std::exp(-kFlashDecay * static_cast<float>(dt));
        if (d.state.flashIntensity < 0.005f) d.state.flashIntensity = 0.0f;

        d.broadcastSpectrum(now);

        glfwMakeContextCurrent(d.renderer.outputWindow());
        d.renderer.checkHotReload(d.state);
        d.renderer.uploadCameraFrame(d.state);

        d.renderUIWindow();

        if (d.stateDirty()) d.sendSyncState();

        glfwMakeContextCurrent(d.renderer.outputWindow());
        int outW, outH;
        glfwGetFramebufferSize(d.renderer.outputWindow(), &outW, &outH);
        glViewport(0, 0, outW, outH);
        d.renderOutputWindow(outW, outH);
    }

    d.cleanup();
}
