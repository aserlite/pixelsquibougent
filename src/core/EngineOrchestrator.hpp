#pragma once

#include <memory>

struct VJState;
class AudioEngine;
class RenderEngine;
class Interface;
class WebServer;
class VideoExport;

class EngineOrchestrator {
public:
    EngineOrchestrator();
    ~EngineOrchestrator();

    EngineOrchestrator(const EngineOrchestrator&)            = delete;
    EngineOrchestrator& operator=(const EngineOrchestrator&) = delete;

    bool init(int argc, char* argv[]);
    void run();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
