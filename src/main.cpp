#include "core/EngineOrchestrator.hpp"

int main(int argc, char* argv[]) {
    EngineOrchestrator engine;
    if (!engine.init(argc, argv)) return 1;
    engine.run();
    return 0;
}
