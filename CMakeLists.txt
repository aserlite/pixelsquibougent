cmake_minimum_required(VERSION 3.22)
project(VJ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
include(FetchContent)

# ── GLFW ──────────────────────────────────────────────────────────────────────
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ── GLAD2 ─────────────────────────────────────────────────────────────────────
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v2.0.6
    SOURCE_SUBDIR  cmake
)
FetchContent_MakeAvailable(glad)
glad_add_library(glad_gl REPRODUCIBLE LOADER API gl:core=4.1)

# ── Dear ImGui (docking branch) ───────────────────────────────────────────────
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD2)
target_link_libraries(imgui_lib PUBLIC glfw glad_gl)

# ── miniaudio (single-header audio library) ───────────────────────────────────
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(miniaudio)

# ── stb (stb_image — single-header JPEG/PNG decode) ──────────────────────────
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

# ── Asio standalone (networking backend for Crow) ────────────────────────────
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(asio)
# Expose Asio as an interface target for Crow to find
add_library(asio_standalone INTERFACE)
target_include_directories(asio_standalone INTERFACE ${asio_SOURCE_DIR}/asio/include)
# Do NOT add ASIO_NO_DEPRECATED: Crow v1.2.0 still uses the io_service alias
target_compile_definitions(asio_standalone INTERFACE ASIO_STANDALONE)

# ── Crow (HTTP/WebSocket micro-framework) ─────────────────────────────────────
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(CROW_USE_BOOST      OFF CACHE BOOL "" FORCE)
set(ASIO_INCLUDE_DIR    "${asio_SOURCE_DIR}/asio/include" CACHE PATH "" FORCE)
FetchContent_Declare(
    crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG        v1.2.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(crow)

# ── nlohmann/json ─────────────────────────────────────────────────────────────
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/core/AudioEngine.cpp
    src/core/DeckLoader.cpp
    src/core/EngineOrchestrator.cpp
    src/core/RenderEngine.cpp
    src/core/VideoExport.cpp
    src/network/WebServer.cpp
    src/gui/Interface.cpp
    src/stb_image_impl.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${miniaudio_SOURCE_DIR}
    ${stb_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        glfw
        glad_gl
        imgui_lib
        OpenGL::GL
        Crow::Crow
        asio_standalone
        nlohmann_json::nlohmann_json
        Threads::Threads
        dl
        m
)

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(DIRECTORY shaders/    DESTINATION share/${PROJECT_NAME}/shaders)
install(DIRECTORY web_client/ DESTINATION share/${PROJECT_NAME}/web_client)
