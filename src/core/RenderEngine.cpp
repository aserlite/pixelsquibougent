#define GLFW_INCLUDE_NONE
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "RenderEngine.hpp"
#include "VJState.hpp"

#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <vector>

void RenderEngine::onError(int code, const char* description) {
    std::cerr << "[GLFW " << code << "] " << description << "\n";
}

static std::string resolveShaderPath(const std::string& filename) {
    for (const auto& prefix : {"shaders/", "../../shaders/",
                                "/home/arthur/Bureau/test/Vjing_armvr/shaders/"}) {
        std::string p = prefix + filename;
        if (std::filesystem::exists(p)) return p;
    }
    return "shaders/" + filename;
}

unsigned int RenderEngine::compileShader(unsigned int type,
                                          const std::string& source,
                                          std::string& outError) {
    unsigned int s = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        outError = log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool RenderEngine::createProgramFromFiles(const std::string& vertPath,
                                           const std::string& fragPath,
                                           unsigned int& outProgram,
                                           std::string& outError) {
    std::ifstream vf(vertPath), ff(fragPath);
    if (!vf.is_open()) { outError = "Cannot open vertex: " + vertPath;   return false; }
    if (!ff.is_open()) { outError = "Cannot open fragment: " + fragPath; return false; }

    std::stringstream vs, fs;
    vs << vf.rdbuf();
    fs << ff.rdbuf();

    unsigned int vert = compileShader(GL_VERTEX_SHADER,   vs.str(), outError);
    if (!vert) { outError = "[Vertex] " + outError; return false; }

    unsigned int frag = compileShader(GL_FRAGMENT_SHADER, fs.str(), outError);
    if (!frag) { outError = "[Fragment] " + outError; glDeleteShader(vert); return false; }

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    glDeleteShader(vert);
    glDeleteShader(frag);

    int linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        outError = std::string("[Link] ") + log;
        glDeleteProgram(prog);
        return false;
    }
    outProgram = prog;
    return true;
}

bool RenderEngine::init() {
    glfwSetErrorCallback(onError);
    if (!glfwInit()) { std::cerr << "[RenderEngine] glfwInit failed.\n"; return false; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE,        GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    m_uiWindow = glfwCreateWindow(1280, 720, "VJ — Dashboard", nullptr, nullptr);
    if (!m_uiWindow) { glfwTerminate(); return false; }

    glfwMakeContextCurrent(m_uiWindow);
    if (!gladLoadGL(glfwGetProcAddress)) {
        std::cerr << "[RenderEngine] GLAD failed.\n";
        glfwDestroyWindow(m_uiWindow);
        glfwTerminate();
        return false;
    }
    glfwSwapInterval(1);
    std::cout << "[RenderEngine] OpenGL " << glGetString(GL_VERSION)
              << " | " << glGetString(GL_RENDERER) << "\n";

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE,        GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    int monCount = 0;
    GLFWmonitor** mons = glfwGetMonitors(&monCount);

    if (monCount >= 2) {
        GLFWmonitor*       m    = mons[1];
        const GLFWvidmode* mode = glfwGetVideoMode(m);
        glfwWindowHint(GLFW_RED_BITS,     mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS,   mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS,    mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        m_outputWindow = glfwCreateWindow(mode->width, mode->height,
                                          "VJ — Output", m, m_uiWindow);
        std::cout << "[RenderEngine] Dual-monitor: "
                  << mode->width << "×" << mode->height << "\n";
    } else {
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        m_outputWindow = glfwCreateWindow(640, 480, "VJ — Output [dev]",
                                          nullptr, m_uiWindow);
        int ux, uy;
        glfwGetWindowPos(m_uiWindow, &ux, &uy);
        glfwSetWindowPos(m_outputWindow, ux + 1290, uy);
        std::cout << "[RenderEngine] Dev mode 640×480.\n";
    }
    if (!m_outputWindow) {
        glfwDestroyWindow(m_uiWindow);
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_outputWindow);
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenTextures(1, &m_cameraTexture);
    glBindTexture(GL_TEXTURE_2D, m_cameraTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 640, 480, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    m_camTexW = 640;
    m_camTexH = 480;

    initPingPongFBOs(640, 480);

    return true;
}

bool RenderEngine::initPingPongFBOs(int width, int height) {
    deletePingPongFBOs();

    m_fboW = width;
    m_fboH = height;
    m_pingPongIndex = 0;
    m_historyIndex  = 0;

    glGenFramebuffers(2, m_fbo);
    glGenTextures(2, m_pingPongTex);
    glGenFramebuffers(8, m_historyFBO);
    glGenTextures(8, m_historyTex);

    for (int i = 0; i < 2; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[i]);
        glBindTexture(GL_TEXTURE_2D, m_pingPongTex[i]);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_pingPongTex[i], 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "[RenderEngine] FBO " << i << " incomplete!\n";
            deletePingPongFBOs();
            return false;
        }
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    for (int i = 0; i < 8; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_historyFBO[i]);
        glBindTexture(GL_TEXTURE_2D, m_historyTex[i]);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_historyTex[i], 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "[RenderEngine] History FBO " << i << " incomplete!\n";
            deletePingPongFBOs();
            return false;
        }
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::cout << "[RenderEngine] Ping-Pong and History FBOs initialized (" << width << "×" << height << ", RGBA16F).\n";
    return true;
}

void RenderEngine::deletePingPongFBOs() {
    if (m_fbo[0] || m_fbo[1]) {
        glDeleteFramebuffers(2, m_fbo);
        m_fbo[0] = m_fbo[1] = 0;
    }
    if (m_pingPongTex[0] || m_pingPongTex[1]) {
        glDeleteTextures(2, m_pingPongTex);
        m_pingPongTex[0] = m_pingPongTex[1] = 0;
    }
    if (m_historyFBO[0]) {
        glDeleteFramebuffers(8, m_historyFBO);
        for (int i = 0; i < 8; ++i) m_historyFBO[i] = 0;
    }
    if (m_historyTex[0]) {
        glDeleteTextures(8, m_historyTex);
        for (int i = 0; i < 8; ++i) m_historyTex[i] = 0;
    }
    m_fboW = m_fboH = 0;
}

void RenderEngine::cacheUniformLocations() {
    if (!m_shaderProgram) return;
    m_uniformLocs.u_time = glGetUniformLocation(m_shaderProgram, "u_time");
    m_uniformLocs.u_resolution = glGetUniformLocation(m_shaderProgram, "u_resolution");
    m_uniformLocs.u_kick = glGetUniformLocation(m_shaderProgram, "u_kick");
    m_uniformLocs.u_energy = glGetUniformLocation(m_shaderProgram, "u_energy");
    m_uniformLocs.u_sub_bass = glGetUniformLocation(m_shaderProgram, "u_sub_bass");
    m_uniformLocs.u_highs = glGetUniformLocation(m_shaderProgram, "u_highs");
    m_uniformLocs.u_mode = glGetUniformLocation(m_shaderProgram, "u_mode");
    m_uniformLocs.u_effect_index = glGetUniformLocation(m_shaderProgram, "u_effect_index");
    m_uniformLocs.u_bg_source_index = glGetUniformLocation(m_shaderProgram, "u_bg_source_index");
    m_uniformLocs.u_is_transitioning = glGetUniformLocation(m_shaderProgram, "u_is_transitioning");
    m_uniformLocs.u_target_bg_source_index = glGetUniformLocation(m_shaderProgram, "u_target_bg_source_index");
    m_uniformLocs.u_trans_progress = glGetUniformLocation(m_shaderProgram, "u_trans_progress");
    m_uniformLocs.u_trans_type = glGetUniformLocation(m_shaderProgram, "u_trans_type");
    m_uniformLocs.u_camera_texture = glGetUniformLocation(m_shaderProgram, "u_camera_texture");
    m_uniformLocs.u_prev_frame = glGetUniformLocation(m_shaderProgram, "u_prev_frame");
    m_uniformLocs.u_sim_pass = glGetUniformLocation(m_shaderProgram, "u_sim_pass");
    m_uniformLocs.u_camera_active = glGetUniformLocation(m_shaderProgram, "u_camera_active");
    m_uniformLocs.u_zscore_impact = glGetUniformLocation(m_shaderProgram, "u_zscore_impact");
    m_uniformLocs.u_col_break1 = glGetUniformLocation(m_shaderProgram, "u_col_break1");
    m_uniformLocs.u_col_break2 = glGetUniformLocation(m_shaderProgram, "u_col_break2");
    m_uniformLocs.u_col_drop1 = glGetUniformLocation(m_shaderProgram, "u_col_drop1");
    m_uniformLocs.u_col_drop2 = glGetUniformLocation(m_shaderProgram, "u_col_drop2");
    m_uniformLocs.u_history_frames = glGetUniformLocation(m_shaderProgram, "u_history_frames");
    m_uniformLocs.u_pad_x = glGetUniformLocation(m_shaderProgram, "u_pad_x");
    m_uniformLocs.u_pad_y = glGetUniformLocation(m_shaderProgram, "u_pad_y");
    m_uniformLocs.u_color_break = glGetUniformLocation(m_shaderProgram, "u_color_break");
    m_uniformLocs.u_color_drop = glGetUniformLocation(m_shaderProgram, "u_color_drop");
    m_uniformLocs.u_impact_index = glGetUniformLocation(m_shaderProgram, "u_impact_index");
    m_uniformLocs.u_deck_texture = glGetUniformLocation(m_shaderProgram, "u_deck_texture");
}

bool RenderEngine::loadShaders(VJState& state) {
    glfwMakeContextCurrent(m_outputWindow);
    std::string vert = resolveShaderPath("base_vertex.glsl");
    m_fragShaderPath = resolveShaderPath("template_fragment.glsl");
    if (std::filesystem::exists(m_fragShaderPath)) {
        std::error_code ec;
        m_lastShaderWriteTime = std::filesystem::last_write_time(m_fragShaderPath, ec);
    }
    unsigned int prog = 0;
    std::string  err;
    if (createProgramFromFiles(vert, m_fragShaderPath, prog, err)) {
        if (m_shaderProgram) glDeleteProgram(m_shaderProgram);
        m_shaderProgram      = prog;
        cacheUniformLocations();
        state.shaderCompiled = true;
        state.shaderError.clear();
        std::cout << "[RenderEngine] Shaders loaded.\n";
        return true;
    }
    state.shaderCompiled = false;
    state.shaderError    = err;
    std::cerr << "[RenderEngine] Shader error: " << err << "\n";
    return false;
}

void RenderEngine::checkHotReload(VJState& state) {
    if (++m_frameCounter % 30 != 0) return;
    if (m_fragShaderPath.empty() || !std::filesystem::exists(m_fragShaderPath)) return;
    std::error_code ec;
    auto t = std::filesystem::last_write_time(m_fragShaderPath, ec);
    if (ec || t == m_lastShaderWriteTime) return;
    m_lastShaderWriteTime = t;
    std::cout << "[RenderEngine] Hot-reloading shader...\n";
    unsigned int prog = 0;
    std::string  err;
    glfwMakeContextCurrent(m_outputWindow);
    if (createProgramFromFiles(resolveShaderPath("base_vertex.glsl"),
                               m_fragShaderPath, prog, err)) {
        if (m_shaderProgram) glDeleteProgram(m_shaderProgram);
        m_shaderProgram      = prog;
        cacheUniformLocations();
        state.shaderCompiled = true;
        state.shaderError.clear();
        std::cout << "[RenderEngine] Hot-reload OK.\n";
    } else {
        state.shaderCompiled = false;
        state.shaderError    = err;
        std::cerr << "[RenderEngine] Hot-reload failed (keeping old shader): " << err << "\n";
    }
}

void RenderEngine::uploadCameraFrame(VJState& state) {
    if (!state.newFrameAvailable.load(std::memory_order_acquire)) return;

    std::lock_guard lock(state.netMutex);
    if (state.cameraPixels.empty()) { state.newFrameAvailable.store(false); return; }

    int w = state.cameraWidth;
    int h = state.cameraHeight;

    glBindTexture(GL_TEXTURE_2D, m_cameraTexture);
    if (w != m_camTexW || h != m_camTexH) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, state.cameraPixels.data());
        m_camTexW = w;
        m_camTexH = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                        GL_RGB, GL_UNSIGNED_BYTE, state.cameraPixels.data());
    }

    state.newFrameAvailable.store(false, std::memory_order_release);
}

void RenderEngine::renderOutput(const VJState& state, int width, int height, unsigned int targetFBO) {
    if (!m_shaderProgram) {
        float f = state.flashIntensity;
        glClearColor(f * 0.95f, f * 0.90f, f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    if (width != m_fboW || height != m_fboH) {
        initPingPongFBOs(width, height);
    }

    glUseProgram(m_shaderProgram);
    glBindVertexArray(m_vao);

    auto setCommonUniforms = [&](int simPass, int prevTexUnit) {
        if (m_uniformLocs.u_time >= 0) glUniform1f(m_uniformLocs.u_time, state.offlineExport ? state.exportTime : static_cast<float>(glfwGetTime()));
        if (m_uniformLocs.u_resolution >= 0) glUniform2f(m_uniformLocs.u_resolution, static_cast<float>(width), static_cast<float>(height));
        if (m_uniformLocs.u_kick >= 0) glUniform1f(m_uniformLocs.u_kick, state.flashIntensity);
        if (m_uniformLocs.u_energy >= 0) glUniform1f(m_uniformLocs.u_energy, state.bassCurrent);
        if (m_uniformLocs.u_sub_bass >= 0) glUniform1f(m_uniformLocs.u_sub_bass, state.subBassCurrent);
        if (m_uniformLocs.u_highs >= 0) glUniform1f(m_uniformLocs.u_highs, state.highsCurrent);
        if (m_uniformLocs.u_mode >= 0) glUniform1i(m_uniformLocs.u_mode, state.macroMode);
        if (m_uniformLocs.u_effect_index >= 0) glUniform1i(m_uniformLocs.u_effect_index, state.effectIndex);
        if (m_uniformLocs.u_bg_source_index >= 0) glUniform1i(m_uniformLocs.u_bg_source_index, state.bgSourceIndex);
        if (m_uniformLocs.u_is_transitioning >= 0) glUniform1i(m_uniformLocs.u_is_transitioning, state.isTransitioning ? 1 : 0);
        if (m_uniformLocs.u_target_bg_source_index >= 0) glUniform1i(m_uniformLocs.u_target_bg_source_index, state.targetBgSourceIndex);
        if (m_uniformLocs.u_trans_progress >= 0) glUniform1f(m_uniformLocs.u_trans_progress, state.transitionProgress);
        if (m_uniformLocs.u_trans_type >= 0) glUniform1i(m_uniformLocs.u_trans_type, state.transitionType);
        if (m_uniformLocs.u_camera_texture >= 0) glUniform1i(m_uniformLocs.u_camera_texture, 0);
        if (m_uniformLocs.u_prev_frame >= 0) glUniform1i(m_uniformLocs.u_prev_frame, prevTexUnit);
        if (m_uniformLocs.u_sim_pass >= 0) glUniform1i(m_uniformLocs.u_sim_pass, simPass);
        if (m_uniformLocs.u_camera_active >= 0) glUniform1i(m_uniformLocs.u_camera_active, state.cameraActive.load() ? 1 : 0);
        if (m_uniformLocs.u_zscore_impact >= 0) glUniform1i(m_uniformLocs.u_zscore_impact, state.zScoreImpact ? 1 : 0);

        if (m_uniformLocs.u_col_break1 >= 0) glUniform3fv(m_uniformLocs.u_col_break1, 1, state.colBreak1.data());
        if (m_uniformLocs.u_col_break2 >= 0) glUniform3fv(m_uniformLocs.u_col_break2, 1, state.colBreak2.data());
        if (m_uniformLocs.u_col_drop1 >= 0) glUniform3fv(m_uniformLocs.u_col_drop1, 1, state.colDrop1.data());
        if (m_uniformLocs.u_col_drop2 >= 0) glUniform3fv(m_uniformLocs.u_col_drop2, 1, state.colDrop2.data());

        if (m_uniformLocs.u_pad_x >= 0) glUniform1f(m_uniformLocs.u_pad_x, state.padX);
        if (m_uniformLocs.u_pad_y >= 0) glUniform1f(m_uniformLocs.u_pad_y, state.padY);
        if (m_uniformLocs.u_color_break >= 0) glUniform3fv(m_uniformLocs.u_color_break, 1, state.colorBreak);
        if (m_uniformLocs.u_color_drop >= 0) glUniform3fv(m_uniformLocs.u_color_drop, 1, state.colorDrop);
        if (m_uniformLocs.u_impact_index >= 0) glUniform1i(m_uniformLocs.u_impact_index, state.impactIndex);

        if (m_uniformLocs.u_history_frames >= 0) {
            int historyUnits[8] = {2, 3, 4, 5, 6, 7, 8, 9};
            glUniform1iv(m_uniformLocs.u_history_frames, 8, historyUnits);
        }

        if (m_uniformLocs.u_deck_texture >= 0 && state.hasDeckImages && !state.deckItems.empty()) {
            int idx = state.activeDeckIndex % state.deckItems.size();
            const auto& item = state.deckItems[idx];
            if (!item.textureIDs.empty()) {
                int frameIdx = item.currentFrame % item.textureIDs.size();
                glActiveTexture(GL_TEXTURE10);
                glBindTexture(GL_TEXTURE_2D, item.textureIDs[frameIdx]);
                glUniform1i(m_uniformLocs.u_deck_texture, 10);
                glActiveTexture(GL_TEXTURE0);
            }
        }
    };

    bool needsRdSim = (state.effectIndex == 6);

    if (needsRdSim) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[m_pingPongIndex]);
        glViewport(0, 0, width, height);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_cameraTexture);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_pingPongTex[1 - m_pingPongIndex]);

        setCommonUniforms(1, 1);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    m_historyIndex = (m_historyIndex + 1) % 8;
    glBindFramebuffer(GL_FRAMEBUFFER, m_historyFBO[m_historyIndex]);
    glViewport(0, 0, width, height);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_cameraTexture);

    if (needsRdSim) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_pingPongTex[m_pingPongIndex]);
    } else {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    setCommonUniforms(2, 1);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glViewport(0, 0, width, height);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_cameraTexture);

    if (needsRdSim) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_pingPongTex[m_pingPongIndex]);
    } else {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_historyTex[m_historyIndex]);

    if (state.impactIndex == 3) {
        for (int i = 1; i < 8; ++i) {
            glActiveTexture(GL_TEXTURE2 + i);
            int idx = (m_historyIndex - i + 8) % 8;
            glBindTexture(GL_TEXTURE_2D, m_historyTex[idx]);
        }
    }
    glActiveTexture(GL_TEXTURE0);

    setCommonUniforms(0, 1);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    if (needsRdSim) {
        m_pingPongIndex = 1 - m_pingPongIndex;
    }
}

void RenderEngine::shutdown() {
    if (m_outputWindow) {
        glfwMakeContextCurrent(m_outputWindow);
        deletePingPongFBOs();
        if (m_shaderProgram)  glDeleteProgram(m_shaderProgram);
        if (m_vao)            glDeleteVertexArrays(1, &m_vao);
        if (m_cameraTexture)  glDeleteTextures(1, &m_cameraTexture);
    }
    if (m_outputWindow) { glfwDestroyWindow(m_outputWindow); m_outputWindow = nullptr; }
    if (m_uiWindow)     { glfwDestroyWindow(m_uiWindow);     m_uiWindow     = nullptr; }
    glfwTerminate();
    std::cout << "[RenderEngine] Shutdown.\n";
}

bool RenderEngine::shouldClose() const {
    return glfwWindowShouldClose(m_uiWindow) ||
           glfwWindowShouldClose(m_outputWindow);
}
