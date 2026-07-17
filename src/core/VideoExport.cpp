#include "VideoExport.hpp"
#include "VJState.hpp"
#include "RenderEngine.hpp"
#include "AudioEngine.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <algorithm>
#include <cmath>

VideoExport::VideoExport() = default;
VideoExport::~VideoExport() { shutdown(); }

void VideoExport::shutdown() {
    deleteExportFBO();
    if (m_ffmpegPipe) {
        pclose(m_ffmpegPipe);
        m_ffmpegPipe = nullptr;
    }
    m_isExporting.store(false, std::memory_order_release);
}

bool VideoExport::initExportFBO(int width, int height) {
    deleteExportFBO();
    m_width  = width;
    m_height = height;

    glGenFramebuffers(1, &m_exportFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_exportFBO);

    glGenTextures(1, &m_exportTex);
    glBindTexture(GL_TEXTURE_2D, m_exportTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_exportTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[VideoExport] Export FBO is incomplete!\n";
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        deleteExportFBO();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glGenBuffers(3, m_pbo);
    for (int i = 0; i < 3; ++i) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, width * height * 3, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    return true;
}

void VideoExport::deleteExportFBO() {
    if (m_pbo[0] || m_pbo[1] || m_pbo[2]) {
        glDeleteBuffers(3, m_pbo);
        m_pbo[0] = 0;
        m_pbo[1] = 0;
        m_pbo[2] = 0;
    }
    if (m_exportTex) { glDeleteTextures(1, &m_exportTex); m_exportTex = 0; }
    if (m_exportFBO) { glDeleteFramebuffers(1, &m_exportFBO); m_exportFBO = 0; }
}

bool VideoExport::startExport(const std::string& mp3Path,
                              const std::string& outputPath,
                              int width,
                              int height,
                              int fps,
                              VJState& state,
                              RenderEngine&,
                              AudioEngine& audio)
{
    if (m_isExporting.load(std::memory_order_relaxed)) {
        std::cerr << "[VideoExport] Export already in progress.\n";
        return false;
    }

    if (mp3Path.empty()) {
        std::cerr << "[VideoExport] Cannot start export: no MP3 file path provided.\n";
        return false;
    }

    std::cout << "[VideoExport] Starting export for: " << mp3Path << " (" << width << "x" << height << " @ " << fps << " fps)...\n";

    if (!initExportFBO(width, height)) {
        std::cerr << "[VideoExport] Failed to initialize export FBO.\n";
        return false;
    }

    if (audio.getTotalDurationSeconds() <= 0.0f) {
        if (!audio.prepareSilentAnalysis(mp3Path.c_str())) {
            std::cerr << "[VideoExport] Failed to prepare silent audio analysis.\n";
            deleteExportFBO();
            return false;
        }
    }

    float duration = audio.getTotalDurationSeconds();
    if (duration <= 0.0f) {
        std::cerr << "[VideoExport] Audio duration is invalid.\n";
        audio.finishOfflineAnalysis();
        deleteExportFBO();
        return false;
    }

    m_mp3Path      = mp3Path;
    m_outputPath   = outputPath;
    m_fps          = fps;
    m_totalFrames  = static_cast<int>(std::ceil(duration * fps));
    m_currentFrame = 0;
    m_progress.store(0.0f, std::memory_order_relaxed);
    m_startTime    = glfwGetTime();
    m_etaSeconds   = 0.0f;

    std::string cmd = "ffmpeg -y -f rawvideo -vcodec rawvideo -pix_fmt rgb24 -s " +
                      std::to_string(width) + "x" + std::to_string(height) +
                      " -r " + std::to_string(fps) +
                      " -i - -i \"" + mp3Path +
                      "\" -c:v libx264 -preset veryfast -crf 18 -tune grain -maxrate 8000k -bufsize 16000k -pix_fmt yuv420p -vf vflip -threads 0 -c:a aac -b:a 192k -shortest \"" +
                      outputPath + "\" 2>/dev/null";

    std::cout << "[VideoExport] Opening FFmpeg pipe: " << cmd << "\n";
    m_ffmpegPipe = popen(cmd.c_str(), "w");
    if (!m_ffmpegPipe) {
        std::cerr << "[VideoExport] Failed to open popen pipe to ffmpeg!\n";
        audio.finishOfflineAnalysis();
        deleteExportFBO();
        return false;
    }

    state.offlineExport      = true;
    state.exportTime         = 0.0f;
    state.exportProgress     = 0.0f;
    state.exportCurrentFrame = 0;
    state.exportTotalFrames  = m_totalFrames;
    state.exportEtaSeconds   = 0.0f;
    m_isExporting.store(true, std::memory_order_release);
    return true;
}

void VideoExport::finishExport(VJState& state, AudioEngine& audio) {
    if (m_ffmpegPipe) {
        if (m_currentFrame > 1) {
            int secondLastReadIndex = (m_currentFrame - 2) % 3;
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[secondLastReadIndex]);
            auto* ptr = static_cast<uint8_t*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, m_width * m_height * 3, GL_MAP_READ_BIT));
            if (ptr) {
                fwrite(ptr, 1, m_width * m_height * 3, m_ffmpegPipe);
            }
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }
        if (m_currentFrame > 0) {
            int lastReadIndex = (m_currentFrame - 1) % 3;
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[lastReadIndex]);
            auto* ptr = static_cast<uint8_t*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, m_width * m_height * 3, GL_MAP_READ_BIT));
            if (ptr) {
                fwrite(ptr, 1, m_width * m_height * 3, m_ffmpegPipe);
            }
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }
        std::cout << "[VideoExport] Finalizing FFmpeg pipe...\n";
        pclose(m_ffmpegPipe);
        m_ffmpegPipe = nullptr;
    }
    audio.finishOfflineAnalysis();
    deleteExportFBO();
    state.offlineExport      = false;
    state.exportProgress     = 1.0f;
    state.exportCurrentFrame = m_totalFrames;
    m_isExporting.store(false, std::memory_order_release);
    std::cout << "[VideoExport] Export completed: " << m_outputPath << "\n";
}

void VideoExport::cancelExport(VJState& state, AudioEngine& audio) {
    if (!m_isExporting.load(std::memory_order_relaxed)) return;
    std::cout << "[VideoExport] Export cancelled by user.\n";
    if (m_ffmpegPipe) {
        pclose(m_ffmpegPipe);
        m_ffmpegPipe = nullptr;
    }
    audio.finishOfflineAnalysis();
    deleteExportFBO();
    state.offlineExport      = false;
    state.exportProgress     = 0.0f;
    state.exportCurrentFrame = 0;
    m_isExporting.store(false, std::memory_order_release);
}

bool VideoExport::isExporting() const {
    return m_isExporting.load(std::memory_order_relaxed);
}

float VideoExport::getProgress() const {
    return m_progress.load(std::memory_order_relaxed);
}

int VideoExport::getCurrentFrame() const { return m_currentFrame; }
int VideoExport::getTotalFrames() const  { return m_totalFrames; }
float VideoExport::getEstimatedTimeRemaining() const { return m_etaSeconds; }
int VideoExport::getFps() const { return m_fps; }

bool VideoExport::processStep(VJState& state, RenderEngine& render, AudioEngine& audio, int screenWidth, int screenHeight) {
    (void)screenWidth; (void)screenHeight;
    if (!m_isExporting.load(std::memory_order_relaxed)) return false;

    if (m_currentFrame >= m_totalFrames || state.exportCancelRequested) {
        state.exportCancelRequested = false;
        if (m_currentFrame >= m_totalFrames) {
            finishExport(state, audio);
        } else {
            cancelExport(state, audio);
        }
        return false;
    }

    float t = static_cast<float>(m_currentFrame) / static_cast<float>(m_fps);
    state.exportTime = t;

    audio.analyzeTimestamp(t, state);

    render.renderOutput(state, m_width, m_height, m_exportFBO);

    int writeIndex = m_currentFrame % 3;
    int readIndex  = (m_currentFrame + 1) % 3;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_exportFBO);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[writeIndex]);
    glReadPixels(0, 0, m_width, m_height, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (m_currentFrame > 1) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[readIndex]);
        auto* ptr = static_cast<uint8_t*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, m_width * m_height * 3, GL_MAP_READ_BIT));
        if (ptr && m_ffmpegPipe) {
            size_t written = fwrite(ptr, 1, m_width * m_height * 3, m_ffmpegPipe);
            if (written != static_cast<size_t>(m_width * m_height * 3)) {
                std::cerr << "[VideoExport] Error writing to FFmpeg pipe! Aborting.\n";
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                cancelExport(state, audio);
                return false;
            }
        }
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    m_currentFrame++;
    float prog = static_cast<float>(m_currentFrame) / static_cast<float>(std::max(1, m_totalFrames));
    m_progress.store(prog, std::memory_order_relaxed);

    double elapsed = glfwGetTime() - m_startTime;
    if (prog > 0.001f && elapsed > 0.5) {
        double totalEst = elapsed / static_cast<double>(prog);
        m_etaSeconds = static_cast<float>(totalEst - elapsed);
    }

    state.exportProgress     = prog;
    state.exportCurrentFrame = m_currentFrame;
    state.exportEtaSeconds   = m_etaSeconds;

    if (m_currentFrame >= m_totalFrames) {
        finishExport(state, audio);
        return false;
    }

    return true;
}
