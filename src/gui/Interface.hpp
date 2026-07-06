#pragma once

struct GLFWwindow;
struct VJState;

class Interface {
public:
    Interface()  = default;
    ~Interface() = default;

    Interface(const Interface&)            = delete;
    Interface& operator=(const Interface&) = delete;

    bool init(GLFWwindow* window);
    void shutdown();

    void beginFrame();
    void render(VJState& state);
    void endFrame();
};
