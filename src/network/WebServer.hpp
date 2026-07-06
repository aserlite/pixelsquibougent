#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

struct VJState;

class WebServer {
public:
    WebServer();
    ~WebServer();

    WebServer(const WebServer&)            = delete;
    WebServer& operator=(const WebServer&) = delete;

    bool start(VJState& state, uint16_t port = 18080);
    void stop();
    void broadcast(const std::string& msg);

    [[nodiscard]] bool     running() const { return m_running; }
    [[nodiscard]] uint16_t port()    const { return m_port; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::thread           m_thread;
    bool                  m_running = false;
    uint16_t              m_port    = 18080;
};
