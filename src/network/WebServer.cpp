#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <crow.h>

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "WebServer.hpp"
#include "core/VJState.hpp"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

using json = nlohmann::json;

static std::vector<uint8_t> base64Decode(const std::string& in) {
    static constexpr unsigned char kTable[256] = {
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,62,64,64,64,63,
        52,53,54,55,56,57,58,59,60,61,64,64,64,64,64,64,
        64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,64,64,64,64,64,
        64,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64
    };

    std::vector<uint8_t> out;
    out.reserve(in.size() * 3 / 4);
    uint32_t val = 0;
    int      bits = -8;
    for (unsigned char c : in) {
        if (kTable[c] == 64) continue;
        val   = (val << 6) | kTable[c];
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

static std::string loadHtmlPage() {
    static const std::vector<std::string> kPaths = {
        "web_client/index.html",
        "../../web_client/index.html",
        "/home/arthur/Bureau/test/Vjing_armvr/web_client/index.html"
    };
    for (const auto& p : kPaths) {
        if (std::filesystem::exists(p)) {
            std::ifstream f(p);
            return {std::istreambuf_iterator<char>(f), {}};
        }
    }
    return "<h1>VJ Engine — place web_client/index.html beside the binary</h1>";
}

struct WebServer::Impl {
    crow::SimpleApp app;
    std::mutex wsMutex;
    std::unordered_set<crow::websocket::connection*> connections;
};

WebServer::WebServer()  = default;
WebServer::~WebServer() { stop(); }

bool WebServer::start(VJState& state, uint16_t port) {
    m_port  = port;
    m_impl  = std::make_unique<Impl>();
    auto& app = m_impl->app;

    CROW_ROUTE(app, "/")([](){ 
        crow::response res(loadHtmlPage());
        res.set_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });

    CROW_WEBSOCKET_ROUTE(app, "/vj-control")
        .onopen([impl = m_impl.get()](crow::websocket::connection& conn) {
            std::cout << "[WebServer] WS client connected: " << conn.get_remote_ip() << "\n";
            std::lock_guard<std::mutex> lock(impl->wsMutex);
            impl->connections.insert(&conn);
        })
        .onclose([impl = m_impl.get()](crow::websocket::connection& conn, const std::string&) {
            std::cout << "[WebServer] WS client disconnected: " << conn.get_remote_ip() << "\n";
            std::lock_guard<std::mutex> lock(impl->wsMutex);
            impl->connections.erase(&conn);
        })
        .onmessage([&state](crow::websocket::connection&,
                             const std::string& data, bool is_binary)
        {
            if (is_binary) return;

            try {
                auto msg = json::parse(data);
                const std::string cmd = msg.value("cmd", "");

                if (msg.contains("set_effect")) {
                    int eff = msg.value("set_effect", -1);
                    if (eff >= 0 && eff <= 5) {
                        state.netEffectIndex.store(eff, std::memory_order_relaxed);
                    }
                } else if (cmd == "set_effect") {
                    int eff = msg.value("value", -1);
                    if (eff >= 0 && eff <= 5) {
                        state.netEffectIndex.store(eff, std::memory_order_relaxed);
                    }
                } else if (cmd == "set_impact") {
                    int imp = msg.value("value", -1);
                    if (imp >= 0 && imp <= 3) {
                        state.netImpactIndex.store(imp, std::memory_order_relaxed);
                    }
                } else if (cmd == "set_bg_source") {
                    int bg = msg.value("value", -1);
                    if (bg >= 0 && bg <= 9) {
                        state.netBgSource.store(bg, std::memory_order_relaxed);
                    }
                } else if (cmd == "next_deck_img") {
                    state.netTriggerRandomImg.store(true, std::memory_order_release);
                } else if (cmd == "set_trans_duration") {
                    float dur = msg.value("value", 0.3f);
                    dur = std::clamp(dur, 0.0f, 1.0f);
                    state.netTransDuration.store(dur, std::memory_order_relaxed);
                } else if (cmd == "pad_movement") {
                    float x = msg.value("x", 0.5f);
                    float y = msg.value("y", 0.5f);
                    state.netPadX.store(std::clamp(x, 0.0f, 1.0f), std::memory_order_relaxed);
                    state.netPadY.store(std::clamp(y, 0.0f, 1.0f), std::memory_order_relaxed);
                } else if (cmd == "set_color_break1" || cmd == "set_color_break") {
                    if (msg.contains("value") && msg["value"].is_array() && msg["value"].size() == 3) {
                        std::lock_guard<std::mutex> lock(state.netMutex);
                        for (int i = 0; i < 3; ++i) {
                            state.netColBreak1[i] = msg["value"][i].get<float>();
                            state.netColorBreak[i] = state.netColBreak1[i];
                        }
                        state.netColorsChanged = true;
                    }
                } else if (cmd == "set_color_break2") {
                    if (msg.contains("value") && msg["value"].is_array() && msg["value"].size() == 3) {
                        std::lock_guard<std::mutex> lock(state.netMutex);
                        for (int i = 0; i < 3; ++i) state.netColBreak2[i] = msg["value"][i].get<float>();
                        state.netColorsChanged = true;
                    }
                } else if (cmd == "set_color_drop1" || cmd == "set_color_drop") {
                    if (msg.contains("value") && msg["value"].is_array() && msg["value"].size() == 3) {
                        std::lock_guard<std::mutex> lock(state.netMutex);
                        for (int i = 0; i < 3; ++i) {
                            state.netColDrop1[i] = msg["value"][i].get<float>();
                            state.netColorDrop[i] = state.netColDrop1[i];
                        }
                        state.netColorsChanged = true;
                    }
                } else if (cmd == "set_color_drop2") {
                    if (msg.contains("value") && msg["value"].is_array() && msg["value"].size() == 3) {
                        std::lock_guard<std::mutex> lock(state.netMutex);
                        for (int i = 0; i < 3; ++i) state.netColDrop2[i] = msg["value"][i].get<float>();
                        state.netColorsChanged = true;
                    }
                } else if (cmd == "set_trans_type") {
                    int t = msg.value("value", 1);
                    if (t >= 0 && t <= 6) {
                        state.netTransType.store(t, std::memory_order_relaxed);
                    }
                } else if (cmd == "set_autoswitch") {
                    bool as = msg.value("value", false);
                    state.netAutoSwitchEnabled.store(as, std::memory_order_relaxed);
                } else if (cmd == "set_auto_visual") {
                    bool av = msg.value("value", false);
                    state.netAutoVisualSwitchEnabled.store(av, std::memory_order_relaxed);
                } else if (cmd == "set_autoflash") {
                    bool af = msg.value("value", true);
                    state.netAutoFlashEnabled.store(af, std::memory_order_relaxed);
                } else if (cmd == "set_kick_target") {
                    int kt = msg.value("value", 32);
                    kt = std::clamp(kt, 8, 64);
                    state.netKickTarget.store(kt, std::memory_order_relaxed);
                } else if (cmd == "stop_camera") {
                    state.cameraActive.store(false, std::memory_order_relaxed);
                } else if (cmd == "flash") {
                    state.netFlashTrigger.store(true, std::memory_order_release);

                } else if (cmd == "set_mode") {
                    int mode = msg.value("value", -1);
                    state.netMacroMode.store(mode, std::memory_order_relaxed);
                } else if (cmd == "set_auto_macro") {
                    bool am = msg.value("value", true);
                    state.netAutoMacro.store(am ? 1 : 0, std::memory_order_relaxed);

                } else if (cmd == "set_zscore_k") {
                    float k = msg.value("value", 2.5f);
                    state.netZScoreK.store(k, std::memory_order_relaxed);

                } else if (cmd == "camera_frame") {
                    std::string b64 = msg.value("data", "");
                    if (b64.empty()) return;

                    auto jpeg = base64Decode(b64);
                    if (jpeg.empty()) return;

                    int w = 0, h = 0, ch = 0;
                    uint8_t* pixels = stbi_load_from_memory(
                        jpeg.data(), static_cast<int>(jpeg.size()),
                        &w, &h, &ch, 3);

                    if (!pixels) {
                        std::cerr << "[WebServer] stbi_load_from_memory failed.\n";
                        return;
                    }

                    {
                        std::lock_guard lock(state.netMutex);
                        state.cameraWidth  = w;
                        state.cameraHeight = h;
                        state.cameraPixels.assign(pixels, pixels + w * h * 3);
                    }
                    stbi_image_free(pixels);

                    if (!state.cameraActive.load(std::memory_order_relaxed)) {
                        state.cameraActive.store(true, std::memory_order_relaxed);
                        state.netBgSource.store(8, std::memory_order_relaxed);
                    }
                    state.newFrameAvailable.store(true, std::memory_order_release);
                }
            } catch (const json::exception& e) {
                std::cerr << "[WebServer] JSON parse error: " << e.what() << "\n";
            }
        });

    m_thread = std::thread([&app, port]() {
        app.port(port).multithreaded().run();
    });

    m_running = true;
    std::cout << "[WebServer] Listening on http://0.0.0.0:" << port << "\n";
    return true;
}

void WebServer::stop() {
    if (!m_running) return;
    if (m_impl) m_impl->app.stop();
    if (m_thread.joinable()) m_thread.join();
    m_running = false;
    std::cout << "[WebServer] Stopped.\n";
}

void WebServer::broadcast(const std::string& msg) {
    if (!m_running || !m_impl) return;
    std::lock_guard<std::mutex> lock(m_impl->wsMutex);
    for (auto* conn : m_impl->connections) {
        conn->send_text(msg);
    }
}
