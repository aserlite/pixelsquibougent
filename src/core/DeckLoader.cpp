#define GLFW_INCLUDE_NONE
#include <glad/gl.h>

#include "DeckLoader.hpp"
#include "VJState.hpp"

#include <stb_image.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static std::string resolveDeckDir() {
    for (const auto& prefix : {
        "assets/deck",
        "../../assets/deck",
        "/home/arthur/Bureau/test/Vjing_armvr/assets/deck"
    }) {
        if (std::filesystem::exists(prefix) && std::filesystem::is_directory(prefix))
            return prefix;
    }
    return "";
}

static bool isHiddenOrSystemFile(const std::filesystem::path& p) {
    const std::string name = p.filename().string();
    if (name.empty() || name[0] == '.') return true;
    if (name == "Thumbs.db" || name == "desktop.ini") return true;
    return false;
}

static GLuint uploadTexture(const unsigned char* data, int w, int h) {
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    return texID;
}

static void loadGif(const std::filesystem::path& path, VJState& state) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return;

    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return;

    int w, h, frames, channels;
    int* delays = nullptr;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load_gif_from_memory(buffer.data(), static_cast<int>(size),
                                                     &delays, &w, &h, &frames, &channels, 4);
    if (!data) {
        std::cerr << "[Deck] Failed to decode GIF: " << path.string() << "\n";
        return;
    }

    VJState::DeckItem item;
    for (int i = 0; i < frames; ++i)
        item.textureIDs.push_back(uploadTexture(data + i * (w * h * 4), w, h));

    stbi_image_free(data);
    if (delays) stbi_image_free(delays);

    state.deckItems.push_back(std::move(item));
    std::cout << "[Deck] Loaded GIF: " << path.string() << " (" << w << "x" << h << ", " << frames << " frames)\n";
}

static void loadStaticImage(const std::filesystem::path& path, VJState& state) {
    int w, h, channels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
    if (!data) {
        std::cerr << "[Deck] Failed to load: " << path.string() << "\n";
        return;
    }

    VJState::DeckItem item;
    item.textureIDs.push_back(uploadTexture(data, w, h));
    stbi_image_free(data);

    state.deckItems.push_back(std::move(item));
    std::cout << "[Deck] Loaded: " << path.string() << " (" << w << "x" << h << ")\n";
}

namespace DeckLoader {

void loadDeck(VJState& state) {
    const std::string deckDir = resolveDeckDir();
    if (deckDir.empty()) {
        std::cerr << "[Deck] Warning: assets/deck directory not found.\n";
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(deckDir)) {
        if (!entry.is_regular_file()) continue;
        if (isHiddenOrSystemFile(entry.path())) continue;

        std::string ext = entry.path().extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (ext == ".gif")
            loadGif(entry.path(), state);
        else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
            loadStaticImage(entry.path(), state);
    }

    if (!state.deckItems.empty()) {
        state.hasDeckImages = true;
        std::cout << "[Deck] " << state.deckItems.size() << " items initialized.\n";
    }
}

}
