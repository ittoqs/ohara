cmake_minimum_required(VERSION 3.16)
project(OharaGPT VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
#  Dependencies
# ============================================================

# Qt6
find_package(Qt6 COMPONENTS Core Gui Qml Quick Network Sql Widgets REQUIRED)

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
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# GPU Support — auto-detect
# CUDA
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
    set(GGML_CUDA ON CACHE BOOL "" FORCE)
    message(STATUS "CUDA found — enabling GPU acceleration")
endif()

# Vulkan (AMD + Intel + NVIDIA)
find_package(Vulkan QUIET)
if(Vulkan_FOUND AND NOT CUDAToolkit_FOUND)
    set(GGML_VULKAN ON CACHE BOOL "" FORCE)
    message(STATUS "Vulkan found — enabling GPU acceleration")
endif()

# Metal (macOS)
if(APPLE)
    set(GGML_METAL ON CACHE BOOL "" FORCE)
    message(STATUS "macOS detected — enabling Metal GPU acceleration")
endif()

FetchContent_MakeAvailable(llama_cpp)

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
)

# ============================================================
#  Link libraries
# ============================================================
target_link_libraries(ohara_gpt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Network
    Qt6::Sql
    Qt6::Widgets
    llama
    common
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
install(TARGETS ohara_gpt
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
