cmake_minimum_required(VERSION 3.16)
project(OharaGPT VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
#  Dependencies
# ============================================================

# Qt6
find_package(Qt6 COMPONENTS Core Gui Qml Quick Network Sql Widgets Multimedia Concurrent REQUIRED)

# GPU Support — auto-detect
# CUDA
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
    set(GGML_CUDA ON CACHE BOOL "" FORCE)
    message(STATUS "CUDA found — enabling GPU acceleration")
endif()

# Vulkan (AMD + Intel + NVIDIA)
find_package(Vulkan QUIET)
if(Vulkan_FOUND AND NOT CUDAToolkit_FOUND AND NOT DEFINED GGML_VULKAN)
    set(GGML_VULKAN ON CACHE BOOL "" FORCE)
    message(STATUS "Vulkan found — enabling GPU acceleration")
endif()

# Metal (macOS)
if(APPLE)
    set(GGML_METAL ON CACHE BOOL "" FORCE)
    message(STATUS "macOS detected — enabling Metal GPU acceleration")
endif()

# llama.cpp via FetchContent
include(FetchContent)
FetchContent_Declare(
    llama_cpp
    GIT_REPOSITORY https://github.com/ggerganov/llama.cpp.git
    GIT_TAG b5460
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

# Configure llama.cpp build options
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES ON CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON ON CACHE BOOL "" FORCE)
set(LLAMA_CURL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(llama_cpp)

FetchContent_Declare(
    whisper_cpp
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG v1.6.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(whisper_cpp)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.11.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(pybind11)

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# ============================================================
#  Qt Auto-tools
# ============================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ============================================================
#  Sources
# ============================================================
set(OHARA_SOURCES
    src/main.cpp
    src/hardware_detector.cpp
    src/hardware_detector.h
    src/settings_manager.cpp
    src/settings_manager.h
    src/database_manager.cpp
    src/database_manager.h
    src/model_manager.cpp
    src/model_manager.h
    src/inference_engine.cpp
    src/inference_engine.h
    src/document_processor.cpp
    src/document_processor.h
    src/voice_manager.cpp
    src/voice_manager.h
    src/script_engine.cpp
    src/script_engine.h
    src/ILlmBackend.h
    src/LlamaBackend.h
    src/LlamaBackend.cpp
)

add_executable(ohara_gpt ${OHARA_SOURCES})

# ============================================================
#  QML Module
# ============================================================
qt_add_qml_module(ohara_gpt
    URI Ohara
    VERSION 1.0
    QML_FILES
        qml/main.qml
)

# ============================================================
#  Include paths
# ============================================================
target_include_directories(ohara_gpt PRIVATE
    ${llama_cpp_SOURCE_DIR}/include
    ${llama_cpp_SOURCE_DIR}/common
    ${llama_cpp_SOURCE_DIR}/ggml/include
    ${whisper_cpp_SOURCE_DIR}/include
    ${pybind11_SOURCE_DIR}/include
    ${Python3_INCLUDE_DIRS}
)

# ============================================================
#  Link libraries
# ============================================================
target_link_libraries(ohara_gpt PRIVATE
    Qt6::Core
    Qt6::Concurrent
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Network
    Qt6::Sql
    Qt6::Widgets
    Qt6::Multimedia
    llama
    common
    whisper
    Python3::Module
    Python3::Python
)

# ============================================================
#  Compile definitions
# ============================================================
target_compile_definitions(ohara_gpt PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

# Platform-specific
if(WIN32)
    set_target_properties(ohara_gpt PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(APPLE)
    set_target_properties(ohara_gpt PROPERTIES MACOSX_BUNDLE TRUE)
endif()

# ============================================================
#  Install
# ============================================================
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/config.json DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

install(TARGETS ohara_gpt
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES config.json DESTINATION ${CMAKE_INSTALL_BINDIR})
