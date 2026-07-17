#include "Interface.hpp"

#include "core/AudioEngine.hpp"
#include "core/VJState.hpp"

#define GLFW_INCLUDE_NONE
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <iostream>

static void applyDarkTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 6.0f; s.FrameRounding  = 4.0f;
    s.GrabRounding   = 4.0f; s.TabRounding    = 4.0f;
    s.WindowPadding  = {12.0f, 12.0f};
    s.FramePadding   = {8.0f,  5.0f};
    s.ItemSpacing    = {8.0f,  6.0f};

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]          = {0.10f, 0.10f, 0.12f, 1.00f};
    c[ImGuiCol_TitleBg]           = {0.08f, 0.08f, 0.10f, 1.00f};
    c[ImGuiCol_TitleBgActive]     = {0.14f, 0.10f, 0.25f, 1.00f};
    c[ImGuiCol_FrameBg]           = {0.16f, 0.16f, 0.20f, 1.00f};
    c[ImGuiCol_FrameBgHovered]    = {0.22f, 0.18f, 0.34f, 1.00f};
    c[ImGuiCol_FrameBgActive]     = {0.28f, 0.20f, 0.45f, 1.00f};
    c[ImGuiCol_CheckMark]         = {0.72f, 0.45f, 1.00f, 1.00f};
    c[ImGuiCol_SliderGrab]        = {0.60f, 0.35f, 0.95f, 1.00f};
    c[ImGuiCol_SliderGrabActive]  = {0.80f, 0.55f, 1.00f, 1.00f};
    c[ImGuiCol_Button]            = {0.22f, 0.16f, 0.38f, 1.00f};
    c[ImGuiCol_ButtonHovered]     = {0.36f, 0.24f, 0.60f, 1.00f};
    c[ImGuiCol_ButtonActive]      = {0.50f, 0.32f, 0.80f, 1.00f};
    c[ImGuiCol_Tab]               = {0.14f, 0.10f, 0.24f, 1.00f};
    c[ImGuiCol_TabHovered]        = {0.36f, 0.24f, 0.60f, 1.00f};
    c[ImGuiCol_TabActive]         = {0.28f, 0.18f, 0.50f, 1.00f};
    c[ImGuiCol_ResizeGrip]        = {0.60f, 0.35f, 0.95f, 0.40f};
    c[ImGuiCol_ResizeGripHovered] = {0.60f, 0.35f, 0.95f, 0.80f};
    c[ImGuiCol_ResizeGripActive]  = {0.80f, 0.55f, 1.00f, 1.00f};
    c[ImGuiCol_DockingPreview]    = {0.60f, 0.35f, 0.95f, 0.70f};
}

bool Interface::init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    applyDarkTheme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");
    std::cout << "[Interface] ImGui " << IMGUI_VERSION << " ready.\n";
    return true;
}

void Interface::shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Interface::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Interface::render(VJState& state) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize      | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::Begin("##DockHost", nullptr, hostFlags);
    ImGui::PopStyleVar();
    ImGui::DockSpace(ImGui::GetID("MainDock"), {0.0f, 0.0f},
                     ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    auto countAllowed = [](const bool* arr, int n) {
        int c = 0; for (int i = 0; i < n; i++) if (arr[i]) c++; return c;
    };

    ImGui::SetNextWindowSize({400.0f, 600.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos ({20.0f,  20.0f},  ImGuiCond_FirstUseEver);
    ImGui::Begin("VJ Dashboard");

    ImGui::SeparatorText("Performance");
    ImGui::Text("FPS  %.1f  (%.2f ms)", state.uiFps, 1000.0f / (state.uiFps + 1e-6f));
    if (state.shaderCompiled)
        ImGui::TextColored({0.3f, 0.9f, 0.4f, 1.0f}, "Shader OK — Hot-Reload ON");
    else {
        ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Shader Error!");
        if (!state.shaderError.empty()) ImGui::TextWrapped("%s", state.shaderError.c_str());
    }

    ImGui::SeparatorText("Source — Etape 1");
    auto selectBgSource = [&](int idx) {
        if (idx == state.bgSourceIndex && !state.isTransitioning) return;
        if (state.transitionDuration > 0.0f && state.transitionType > 0 && !state.isTransitioning && idx != state.bgSourceIndex) {
            state.targetBgSourceIndex = idx;
            state.isTransitioning     = true;
            state.transitionProgress  = 0.0f;
        } else {
            state.bgSourceIndex   = idx;
            state.isTransitioning = false;
        }
    };
    const char* bgNames[]      = { "0: Noise Field", "1: Curl Noise (Fluid)", "2: Raymarching Tunnel", "3: Metaballs 3D", "4: Fluid Morphing Blob", "5: Reaction-Diffusion", "6: Clifford Attractor", "7: Voronoi Tessellation", "8: Camera Stream (Live)", "9: Image Deck" };
    const char* bgShortNames[] = { "Noise", "Curl", "Tunnel", "Metaballs", "Fluid", "React-Diff", "Clifford", "Voronoi", "Camera", "Img Deck" };
    ImGui::SetNextItemWidth(280.0f);
    if (ImGui::BeginCombo("##srccombo", bgNames[state.bgSourceIndex])) {
        for (int i = 0; i < 10; ++i) {
            if (!state.allowedBgSources[i]) continue;
            if (i == 8 && !state.cameraActive.load()) continue;
            bool sel = (state.bgSourceIndex == i);
            if (ImGui::Selectable(bgNames[i], sel)) selectBgSource(i);
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    { int col = 0;
      for (int i = 0; i < 10; ++i) {
        if (!state.allowedBgSources[i]) continue;
        if (i == 8 && !state.cameraActive.load()) continue;
        if (col > 0 && col % 4 != 0) ImGui::SameLine();
        char lbl[32]; snprintf(lbl, sizeof(lbl), "%s##bg%d", bgShortNames[i], i);
        if (ImGui::RadioButton(lbl, state.bgSourceIndex == i)) selectBgSource(i);
        ++col;
      }
    }
    if (state.bgSourceIndex == 9) { ImGui::SameLine(); if (ImGui::Button("Next##deck")) state.netTriggerRandomImg.store(true, std::memory_order_release); }

    ImGui::SeparatorText("Filtre — Etape 2");
    const char* filterNames[]   = { "0: Raw / Through", "1: Sobel Neon", "2: ASCII Art", "3: Ordered Dither", "4: Halftone Pop-Art", "5: Cross-Hatch" };
    const char* fltShortNames[] = { "Raw", "Sobel", "ASCII", "Dither", "Halftone", "X-Hatch" };
    ImGui::SetNextItemWidth(280.0f);
    if (ImGui::BeginCombo("##fltcombo", filterNames[state.effectIndex])) {
        for (int i = 0; i < 6; ++i) {
            if (!state.allowedEffects[i]) continue;
            bool sel = (state.effectIndex == i);
            if (ImGui::Selectable(filterNames[i], sel)) state.effectIndex = i;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    { int col = 0;
      for (int i = 0; i < 6; ++i) {
        if (!state.allowedEffects[i]) continue;
        if (col > 0 && col % 3 != 0) ImGui::SameLine();
        char lbl[32]; snprintf(lbl, sizeof(lbl), "%s##f%d", fltShortNames[i], i);
        if (ImGui::RadioButton(lbl, state.effectIndex == i)) state.effectIndex = i;
        ++col;
      }
    }

    ImGui::SeparatorText("Impact — Etape 3 (Flash/Kick)");
    const char* impNames[] = { "None", "CRT Glitch", "Pixel Sort", "Slit-Scan" };
    { int col = 0;
      for (int i = 0; i < 4; ++i) {
        if (!state.allowedImpacts[i]) continue;
        if (col > 0) ImGui::SameLine();
        char lbl[32]; snprintf(lbl, sizeof(lbl), "%s##imp%d", impNames[i], i);
        if (ImGui::RadioButton(lbl, state.impactIndex == i)) { state.impactIndex = i; state.netImpactIndex.store(i, std::memory_order_relaxed); }
        ++col;
      }
    }

    ImGui::SeparatorText("Transitions");
    const char* transNames[] = { "0: Cut", "1: Fade", "2: Wipe", "3: Glitch", "4: Luma Melt", "5: Radial", "6: Zoom Cross" };
    const char* trShort[]    = { "Cut", "Fade", "Wipe", "Glitch", "Luma", "Radial", "Zoom" };
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::BeginCombo("##trcombo", transNames[state.transitionType])) {
        for (int i = 0; i < 7; ++i) {
            bool sel = (state.transitionType == i);
            if (ImGui::Selectable(transNames[i], sel)) { state.transitionType = i; state.netTransType.store(i, std::memory_order_relaxed); }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    for (int i = 0; i < 7; ++i) {
        if (i > 0 && i % 4 != 0) ImGui::SameLine();
        char lbl[32]; snprintf(lbl, sizeof(lbl), "%s##tr%d", trShort[i], i);
        if (ImGui::RadioButton(lbl, state.transitionType == i)) { state.transitionType = i; state.netTransType.store(i, std::memory_order_relaxed); }
    }
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::SliderFloat("Duration (s)", &state.transitionDuration, 0.0f, 1.0f, "%.1f s"))
        state.netTransDuration.store(state.transitionDuration, std::memory_order_relaxed);
    if (state.isTransitioning) { ImGui::SameLine(); ImGui::TextColored({0.7f, 0.4f, 1.0f, 1.0f}, "TRANSITION: %.0f%%", state.transitionProgress * 100.0f); }

    ImGui::SeparatorText("Auto-Switch Visuals & Filtres");
    if (ImGui::Checkbox("Auto-Visuals Enabled", &state.autoVisualSwitchEnabled))
        state.netAutoVisualSwitchEnabled.store(state.autoVisualSwitchEnabled, std::memory_order_relaxed);
    if (state.autoVisualSwitchEnabled) {
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderFloat("Interval (s)", &state.autoVisualSwitchInterval, 2.0f, 30.0f, "%.1f s");
        ImGui::SameLine();
        if (state.autoVisualSwitchArmed) ImGui::TextColored({1.0f, 0.85f, 0.1f, 1.0f}, "ARM");
        else ImGui::Text("%.1f / %.1fs", state.autoVisualSwitchTimer, state.autoVisualSwitchInterval);
    }

    ImGui::SeparatorText("Output & Flash Trigger");
    if (ImGui::Button("Trigger Kick Flash", {-1.0f, 28.0f})) state.flashIntensity = 1.0f;
    if (ImGui::Checkbox("Auto-Flash Enabled", &state.autoFlashEnabled))
        state.netAutoFlashEnabled.store(state.autoFlashEnabled, std::memory_order_relaxed);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, {0.72f, 0.45f, 1.00f, 1.0f});
    ImGui::ProgressBar(state.flashIntensity, {-1.0f, 0.0f}, "");
    ImGui::PopStyleColor();
    ImGui::End();

    // ── Visual Allowlist window ────────────────────────────────────────────────
    ImGui::SetNextWindowSize({320.0f, 420.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos ({440.0f,  20.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Visual Allowlist");
    ImGui::TextDisabled("Min. 1 element per category.");
    ImGui::Spacing();
    ImGui::SeparatorText("Sources BG (Etape 1)");
    const char* bgAllow[] = { "Noise Field", "Curl Noise", "Tunnel 3D", "Metaballs", "Fluid Blob", "React-Diffusion", "Clifford", "Voronoi", "Camera", "Image Deck" };
    for (int i = 0; i < 10; ++i) {
        bool locked = (countAllowed(state.allowedBgSources, 10) == 1 && state.allowedBgSources[i]);
        bool na     = (i == 8 && !state.cameraActive.load()) || (i == 9 && !state.hasDeckImages);
        if (i % 2 == 1) ImGui::SameLine(160.0f);
        if (locked || na) ImGui::BeginDisabled();
        bool v = state.allowedBgSources[i];
        char lbl[64]; snprintf(lbl, sizeof(lbl), "%d: %s##albg%d", i, bgAllow[i], i);
        if (ImGui::Checkbox(lbl, &v)) {
            state.allowedBgSources[i] = v; state.allowedListsDirty = true;
            if (!v && state.bgSourceIndex == i) for (int j=0;j<10;++j) if(state.allowedBgSources[j]){state.bgSourceIndex=j;break;}
        }
        if (locked || na) { ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(locked ? "At least 1 source required" : (i==8?"Camera inactive":"No deck images")); }
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Filtres (Etape 2)");
    const char* fxAllow[] = { "Raw / Through", "Sobel Neon", "ASCII Art", "Ordered Dither", "Halftone Pop-Art", "Cross-Hatch" };
    for (int i = 0; i < 6; ++i) {
        bool locked = (countAllowed(state.allowedEffects, 6) == 1 && state.allowedEffects[i]);
        if (locked) ImGui::BeginDisabled();
        bool v = state.allowedEffects[i];
        char lbl[64]; snprintf(lbl, sizeof(lbl), "%d: %s##alfx%d", i, fxAllow[i], i);
        if (ImGui::Checkbox(lbl, &v)) {
            state.allowedEffects[i] = v; state.allowedListsDirty = true;
            if (!v && state.effectIndex == i) for (int j=0;j<6;++j) if(state.allowedEffects[j]){state.effectIndex=j;break;}
        }
        if (locked) { ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("At least 1 filter required"); }
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Impacts (Etape 3)");
    const char* impAllow[] = { "Aucun (None)", "CRT Glitch", "Pixel Sort", "Slit-Scan" };
    for (int i = 0; i < 4; ++i) {
        bool locked = (countAllowed(state.allowedImpacts, 4) == 1 && state.allowedImpacts[i]);
        if (locked) ImGui::BeginDisabled();
        bool v = state.allowedImpacts[i];
        char lbl[64]; snprintf(lbl, sizeof(lbl), "%d: %s##alimp%d", i, impAllow[i], i);
        if (ImGui::Checkbox(lbl, &v)) {
            state.allowedImpacts[i] = v; state.allowedListsDirty = true;
            if (!v && state.impactIndex == i) for (int j=0;j<4;++j) if(state.allowedImpacts[j]){state.impactIndex=j;state.netImpactIndex.store(j,std::memory_order_relaxed);break;}
        }
        if (locked) { ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("At least 1 impact required"); }
    }
    ImGui::End();

    // ── (legacy placeholder removed) ───────────────────────────────────────────
    ImGui::SetNextWindowSize({380.0f, 220.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos ({420.0f, 300.0f}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Video Export Studio", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (state.offlineExport) {
            ImGui::TextColored({0.2f, 1.0f, 0.2f, 1.0f}, "EXPORTING... (Deterministic 60 FPS)");
            char buf[128];
            snprintf(buf, sizeof(buf), "Frame %d / %d (%.1f%%) - ETA: %.1fs",
                     state.exportCurrentFrame, state.exportTotalFrames,
                     state.exportProgress * 100.0f, state.exportEtaSeconds);
            ImGui::ProgressBar(state.exportProgress, ImVec2(-1.0f, 0.0f), buf);

            if (ImGui::Button("Cancel Export##exp", ImVec2(-1.0f, 0.0f))) {
                state.exportCancelRequested = true;
            }
        } else {
            ImGui::Text("Resolution:");
            ImGui::RadioButton("720p (1280x720)##exp",  &state.exportResIndex, 0); ImGui::SameLine();
            ImGui::RadioButton("1080p (1920x1080)##exp", &state.exportResIndex, 1);
            ImGui::Separator();

            if (ImGui::Button("Configure & Render (MP4)##exp", ImVec2(-1.0f, 0.0f))) {
                if (state.exportAudioPath[0] == '\0' && !state.activeMp3Path.empty()) {
                    strncpy(state.exportAudioPath, state.activeMp3Path.c_str(), sizeof(state.exportAudioPath) - 1);
                    state.exportAudioPath[sizeof(state.exportAudioPath) - 1] = '\0';
                }
                state.showExportConfigModal = true;
            }
        }

        if (state.showExportConfigModal) {
            ImGui::OpenPopup("Export Configuration");
        }

        if (ImGui::BeginPopupModal("Export Configuration", &state.showExportConfigModal, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Configure Automation Rules for Video Render:");
            ImGui::Separator();

            ImGui::InputText("Audio Path (.mp3)", state.exportAudioPath, sizeof(state.exportAudioPath));
            ImGui::InputText("Output File (.mp4)", state.exportOutputPath, sizeof(state.exportOutputPath));
            ImGui::Separator();

            if (ImGui::Checkbox("Enable Auto-Structure Loop (Grid-Sync)", &state.autoSwitchEnabled)) {
                state.netAutoSwitchEnabled.store(state.autoSwitchEnabled, std::memory_order_release);
            }
            if (ImGui::Checkbox("Enable Auto-Kick Flash (Strobe)", &state.autoFlashEnabled)) {
                state.netAutoFlashEnabled.store(state.autoFlashEnabled, std::memory_order_release);
            }
            if (ImGui::Checkbox("Enable Auto-Visuals Switch", &state.autoVisualSwitchEnabled)) {
                state.netAutoVisualSwitchEnabled.store(state.autoVisualSwitchEnabled, std::memory_order_release);
            }
            ImGui::Checkbox("Enable Auto-Switch via Audio (Drop/Break)", &state.autoMacroMode);

            ImGui::Text("Initial Macro Mode:");
            if (ImGui::RadioButton("BREAK", &state.macroMode, 0)) {
                state.netAutoMacro.store(0, std::memory_order_release);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("DROP", &state.macroMode, 1)) {
                state.netAutoMacro.store(1, std::memory_order_release);
            }

            ImGui::Separator();

            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                state.showExportConfigModal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Launch Render", ImVec2(120, 0))) {
                state.showExportConfigModal = false;
                state.kickCounter = 0;
                state.netAutoSwitchEnabled.store(state.autoSwitchEnabled, std::memory_order_release);
                state.netAutoFlashEnabled.store(state.autoFlashEnabled, std::memory_order_release);
                state.netAutoVisualSwitchEnabled.store(state.autoVisualSwitchEnabled, std::memory_order_release);
                state.netAutoMacro.store(state.macroMode, std::memory_order_release);
                state.exportRequested = true;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
    ImGui::End();

    ImGui::SetNextWindowSize({400.0f, 200.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos ({820.0f,  20.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Colour Palettes");

    ImGui::SeparatorText("BREAK mode");
    ImGui::ColorEdit3("Primary##b",   state.colBreak1.data());
    ImGui::ColorEdit3("Secondary##b", state.colBreak2.data());

    ImGui::SeparatorText("DROP mode");
    ImGui::ColorEdit3("Primary##d",   state.colDrop1.data());
    ImGui::ColorEdit3("Secondary##d", state.colDrop2.data());

    ImGui::End();

    ImGui::SetNextWindowSize({380.0f, 200.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos ({420.0f, 20.0f},  ImGuiCond_FirstUseEver);
    ImGui::Begin("Macro State Machine");

    ImGui::SeparatorText("Current Visual Mode");
    if (state.macroMode == 1) {
        ImGui::PushStyleColor(ImGuiCol_Button, {0.85f, 0.15f, 0.35f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.95f, 0.25f, 0.45f, 1.0f});
        if (ImGui::Button("   DROP MODE [AGRESSIF]   ", {-1.0f, 32.0f})) {
            state.macroMode = 0;
            state.kickCounter = 0;
            state.autoMacroMode = false;
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, {0.15f, 0.45f, 0.85f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.25f, 0.55f, 0.95f, 1.0f});
        if (ImGui::Button("   BREAK MODE [CALME]   ", {-1.0f, 32.0f})) {
            state.macroMode = 1;
            state.kickCounter = 0;
            state.autoMacroMode = false;
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::Checkbox("Auto-switch via Audio", &state.autoMacroMode);
    if (!state.autoMacroMode) {
        state.autoSwitchEnabled = false;
        state.netAutoSwitchEnabled.store(false, std::memory_order_relaxed);
        ImGui::SameLine();
        if (ImGui::RadioButton("BREAK", state.macroMode == 0)) { state.macroMode = 0; state.kickCounter = 0; }
        ImGui::SameLine();
        if (ImGui::RadioButton("DROP",  state.macroMode == 1)) { state.macroMode = 1; state.kickCounter = 0; }
    }
    if (!state.autoMacroMode) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Auto-Structure Loop (Grid-Sync)", &state.autoSwitchEnabled)) {
        state.netAutoSwitchEnabled.store(state.autoSwitchEnabled, std::memory_order_relaxed);
    }
    if (state.autoSwitchEnabled) {
        ImGui::SetNextItemWidth(180.0f);
        int stepTarget = state.kickTarget;
        if (ImGui::SliderInt("Kick Target", &stepTarget, 8, 64)) {
            stepTarget = std::clamp(((stepTarget + 4) / 8) * 8, 8, 64);
            state.kickTarget = stepTarget;
            state.netKickTarget.store(stepTarget, std::memory_order_relaxed);
        }
        char progressBuf[64];
        snprintf(progressBuf, sizeof(progressBuf), "%d / %d Kicks", state.kickCounter, state.kickTarget);
        float progressFraction = (state.kickTarget > 0) ? static_cast<float>(state.kickCounter) / static_cast<float>(state.kickTarget) : 0.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, {0.72f, 0.45f, 1.00f, 1.0f});
        ImGui::ProgressBar(progressFraction, {-1.0f, 0.0f}, progressBuf);
        ImGui::PopStyleColor();
    }
    if (!state.autoMacroMode) ImGui::EndDisabled();

    ImGui::SeparatorText("Z-Score Impact Detection");
    if (ImGui::SliderFloat("Threshold k (sigma)", &state.zScoreK, 1.0f, 5.0f)) {
        state.netZScoreK.store(state.zScoreK, std::memory_order_relaxed);
    }
    if (state.zScoreImpact) {
        ImGui::TextColored({1.0f, 0.8f, 0.1f, 1.0f}, "★ Z-SCORE IMPACT DETECTED!");
    } else {
        ImGui::TextDisabled("★ Standing by...");
    }

    ImGui::End();

    ImGui::SetNextWindowSize({400.0f, 240.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos ({20.0f,  280.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Audio Analysis");

    ImGui::Text("Input Source:");
    ImGui::SameLine();
    if (ImGui::RadioButton("MP3 File", state.audioSource == AudioSourceType::MP3_FILE)) {
        if (state.audioSource != AudioSourceType::MP3_FILE) {
            state.audioSource = AudioSourceType::MP3_FILE;
            state.audioSourceChanged = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Microphone", state.audioSource == AudioSourceType::MICROPHONE)) {
        if (state.audioSource != AudioSourceType::MICROPHONE) {
            state.audioSource = AudioSourceType::MICROPHONE;
            state.audioSourceChanged = true;
        }
    }
    if (state.audioSource == AudioSourceType::MICROPHONE) {
        const char* currentName = "Default / Internal";
        if (state.selectedCaptureDevice >= 0 && state.selectedCaptureDevice < static_cast<int>(state.captureDeviceNames.size())) {
            currentName = state.captureDeviceNames[state.selectedCaptureDevice].c_str();
        }
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::BeginCombo("Device / Monitor", currentName)) {
            for (int i = 0; i < static_cast<int>(state.captureDeviceNames.size()); ++i) {
                bool isSelected = (state.selectedCaptureDevice == i);
                if (ImGui::Selectable(state.captureDeviceNames[i].c_str(), isSelected)) {
                    state.selectedCaptureDevice = i;
                    state.captureDeviceChanged = true;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else if (state.audioSource == AudioSourceType::MP3_FILE) {
        ImGui::SeparatorText("MP3 Hot-Reload");
        ImGui::SetNextItemWidth(-80.0f);
        ImGui::InputText("##mp3path", state.pendingMp3Path, sizeof(state.pendingMp3Path));
        ImGui::SameLine();
        if (ImGui::Button("Load##mp3")) {
            if (state.pendingMp3Path[0] != '\0') {
                state.mp3ReloadRequested = true;
            }
        }
    }
    ImGui::Separator();

    if (!state.audioActive) {
        ImGui::TextDisabled("Audio engine inactive or no source loaded.");
        ImGui::End();
        return;
    }

    static float displayMax = 1e-10f;
    displayMax = std::max(displayMax * 0.995f, state.bassCurrent);

    std::array<float, VJState::kHistorySize> normHistory;
    for (int i = 0; i < VJState::kHistorySize; ++i)
        normHistory[i] = (displayMax > 1e-10f)
                         ? state.bassHistory[i] / displayMax : 0.0f;
    float normThreshold = (displayMax > 1e-10f)
                          ? state.bassThreshold / displayMax : 0.0f;

    if (state.kickDetected)
        ImGui::TextColored({1.0f, 0.4f, 0.1f, 1.0f}, "  KICK!");
    else
        ImGui::TextDisabled("  -----");

    ImGui::SameLine(100.0f);
    ImGui::Text("Kick %.4f | Sub %.4f", state.bassCurrent, state.subBassCurrent);

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_PlotLines, {0.60f, 0.35f, 0.95f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_FrameBg,   {0.12f, 0.12f, 0.16f, 1.0f});
    ImGui::PlotLines("##bass", normHistory.data(), VJState::kHistorySize,
                     0, nullptr, 0.0f, 1.2f, {-1.0f, 80.0f});
    ImGui::PopStyleColor(2);

    ImVec2 pMin = ImGui::GetItemRectMin();
    ImVec2 pMax = ImGui::GetItemRectMax();
    float  thrY = pMax.y - std::clamp(normThreshold, 0.0f, 1.0f) * (pMax.y - pMin.y);
    ImGui::GetWindowDrawList()->AddLine(
        {pMin.x, thrY}, {pMax.x, thrY}, IM_COL32(255, 80, 40, 200), 1.5f);

    ImGui::Spacing();
    ImGui::TextDisabled("Purple: kick energy  |  Orange: threshold (x%.1f)",
                        AudioEngine::kSensitivity);
    ImGui::End();
}

void Interface::endFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
