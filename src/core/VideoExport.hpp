#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <cstdint>
#include <cstdio>

struct VJState;
class RenderEngine;
class AudioEngine;

class VideoExport {
public:
    VideoExport();
    ~VideoExport();

    VideoExport(const VideoExport&)            = delete;
    VideoExport& operator=(const VideoExport&) = delete;

    void shutdown();

    bool startExport(const std::string& mp3Path,
                     const std::string& outputPath,
                     int width,
                     int height,
                     int fps,
                     VJState& state,
                     RenderEngine& render,
                     AudioEngine& audio);

    void cancelExport(VJState& state, AudioEngine& audio);
    [[nodiscard]] bool  isExporting() const;
    [[nodiscard]] float getProgress() const;
    [[nodiscard]] int   getCurrentFrame() const;
    [[nodiscard]] int   getTotalFrames() const;
    [[nodiscard]] float getEstimatedTimeRemaining() const;
    [[nodiscard]] int   getFps() const;

    bool processStep(VJState& state, RenderEngine& render, AudioEngine& audio, int screenWidth, int screenHeight);

private:
    unsigned int m_exportFBO     = 0;
    unsigned int m_exportTex     = 0;
    int          m_width         = 1920;
    int          m_height        = 1080;
    int          m_fps           = 60;

    FILE*        m_ffmpegPipe    = nullptr;
    int          m_currentFrame  = 0;
    int          m_totalFrames   = 0;
    double       m_startTime     = 0.0;
    float        m_etaSeconds    = 0.0f;

    std::atomic<bool>  m_isExporting{false};
    std::atomic<float> m_progress{0.0f};
    std::string        m_mp3Path;
    std::string        m_outputPath;
    unsigned int       m_pbo[3] = {0, 0, 0};

    bool initExportFBO(int width, int height);
    void deleteExportFBO();
    void finishExport(VJState& state, AudioEngine& audio);
};
