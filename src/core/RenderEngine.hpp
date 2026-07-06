#pragma once

#include <filesystem>
#include <string>

struct GLFWwindow;
struct VJState;

class RenderEngine {
public:
    RenderEngine()  = default;
    ~RenderEngine() = default;

    RenderEngine(const RenderEngine&)            = delete;
    RenderEngine& operator=(const RenderEngine&) = delete;

    bool init();
    void shutdown();

    bool loadShaders(VJState& state);
    void checkHotReload(VJState& state);
    void renderOutput(const VJState& state, int width, int height, unsigned int targetFBO = 0);

    void uploadCameraFrame(VJState& state);

    [[nodiscard]] GLFWwindow* uiWindow()     const { return m_uiWindow; }
    [[nodiscard]] GLFWwindow* outputWindow() const { return m_outputWindow; }
    [[nodiscard]] bool        shouldClose()  const;

private:
    GLFWwindow* m_uiWindow     = nullptr;
    GLFWwindow* m_outputWindow = nullptr;

    unsigned int m_shaderProgram = 0;
    unsigned int m_vao           = 0;
    unsigned int m_cameraTexture = 0;
    int          m_camTexW       = 0;
    int          m_camTexH       = 0;
    int          m_frameCounter  = 0;

    unsigned int m_fbo[2]         = {0, 0};
    unsigned int m_pingPongTex[2] = {0, 0};
    int          m_pingPongIndex  = 0;
    unsigned int m_historyFBO[8]  = {0};
    unsigned int m_historyTex[8]  = {0};
    int          m_historyIndex   = 0;
    int          m_fboW           = 0;
    int          m_fboH           = 0;

    struct UniformLocations {
        int u_time = -1;
        int u_resolution = -1;
        int u_kick = -1;
        int u_energy = -1;
        int u_sub_bass = -1;
        int u_highs = -1;
        int u_mode = -1;
        int u_effect_index = -1;
        int u_bg_source_index = -1;
        int u_is_transitioning = -1;
        int u_target_bg_source_index = -1;
        int u_trans_progress = -1;
        int u_trans_type = -1;
        int u_camera_texture = -1;
        int u_prev_frame = -1;
        int u_sim_pass = -1;
        int u_camera_active = -1;
        int u_zscore_impact = -1;
        int u_col_break1 = -1;
        int u_col_break2 = -1;
        int u_col_drop1 = -1;
        int u_col_drop2 = -1;
        int u_history_frames = -1;
        int u_pad_x = -1;
        int u_pad_y = -1;
        int u_color_break = -1;
        int u_color_drop = -1;
        int u_impact_index = -1;
        int u_deck_texture = -1;
    } m_uniformLocs;
    void cacheUniformLocations();

    bool initPingPongFBOs(int width, int height);
    void deletePingPongFBOs();

    std::filesystem::file_time_type m_lastShaderWriteTime;
    std::string                     m_fragShaderPath;

    static unsigned int compileShader(unsigned int type, const std::string& source, std::string& outError);
    static bool         createProgramFromFiles(const std::string& vertPath, const std::string& fragPath,
                                               unsigned int& outProgram, std::string& outError);
    static void         onError(int code, const char* description);
};
