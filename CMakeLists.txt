cmake_minimum_required(VERSION 3.16)
project(Modularity LANGUAGES C CXX)

include(CheckCXXCompilerFlag)

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

set(MODULARITY_CXX_STANDARD_LABEL "C++20")
set(MODULARITY_CXX_STANDARD_FLAG "")
if(MSVC)
    check_cxx_compiler_flag("/std:c++latest" MODULARITY_HAS_CXX26_FLAG)
    check_cxx_compiler_flag("/std:c++23" MODULARITY_HAS_CXX23_FLAG)
    if(NOT MODULARITY_HAS_CXX23_FLAG)
        check_cxx_compiler_flag("/std:c++23preview" MODULARITY_HAS_CXX23_PREVIEW_FLAG)
    endif()

    if(MODULARITY_HAS_CXX26_FLAG)
        set(MODULARITY_CXX_STANDARD_LABEL "C++26")
        set(MODULARITY_CXX_STANDARD_FLAG "/std:c++latest")
    elseif(MODULARITY_HAS_CXX23_FLAG)
        set(MODULARITY_CXX_STANDARD_LABEL "C++23")
        set(MODULARITY_CXX_STANDARD_FLAG "/std:c++23")
    elseif(MODULARITY_HAS_CXX23_PREVIEW_FLAG)
        set(MODULARITY_CXX_STANDARD_LABEL "C++23")
        set(MODULARITY_CXX_STANDARD_FLAG "/std:c++23preview")
    else()
        set(MODULARITY_CXX_STANDARD_LABEL "C++20")
        set(MODULARITY_CXX_STANDARD_FLAG "/std:c++20")
    endif()
else()
    check_cxx_compiler_flag("-std=c++26" MODULARITY_HAS_CXX26_FLAG)
    if(NOT MODULARITY_HAS_CXX26_FLAG)
        check_cxx_compiler_flag("-std=c++2c" MODULARITY_HAS_CXX2C_FLAG)
    endif()
    check_cxx_compiler_flag("-std=c++23" MODULARITY_HAS_CXX23_FLAG)
    if(NOT MODULARITY_HAS_CXX23_FLAG)
        check_cxx_compiler_flag("-std=c++2b" MODULARITY_HAS_CXX2B_FLAG)
    endif()

    if(MODULARITY_HAS_CXX26_FLAG)
        set(MODULARITY_CXX_STANDARD_LABEL "C++26")
        set(MODULARITY_CXX_STANDARD_FLAG "-std=c++26")
    elseif(MODULARITY_HAS_CXX2C_FLAG)
        set(MODULARITY_CXX_STANDARD_LABEL "C++26")
        set(MODULARITY_CXX_STANDARD_FLAG "-std=c++2c")
    elseif(MODULARITY_HAS_CXX23_FLAG)
        set(MODULARITY_CXX_STANDARD_LABEL "C++23")
        set(MODULARITY_CXX_STANDARD_FLAG "-std=c++23")
    elseif(MODULARITY_HAS_CXX2B_FLAG)
        set(MODULARITY_CXX_STANDARD_LABEL "C++23")
        set(MODULARITY_CXX_STANDARD_FLAG "-std=c++2b")
    else()
        set(MODULARITY_CXX_STANDARD_LABEL "C++20")
        set(MODULARITY_CXX_STANDARD_FLAG "-std=c++20")
    endif()
endif()

message(STATUS "Modularity C++ API default: ${MODULARITY_CXX_STANDARD_LABEL} (${MODULARITY_CXX_STANDARD_FLAG})")

function(modularity_apply_cxx_api_standard target_name)
    target_compile_options(${target_name} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${MODULARITY_CXX_STANDARD_FLAG}>
    )
endfunction()

# ==================== Compiler cache (ccache / sccache) ====================

option(MODULARITY_USE_COMPILER_CACHE "Enable compiler cache if available" ON)

if(MODULARITY_USE_COMPILER_CACHE)
    find_program(CCACHE_PROGRAM ccache)
    find_program(SCCACHE_PROGRAM sccache)

    if(CCACHE_PROGRAM)
        message(STATUS "Using compiler cache: ccache (${CCACHE_PROGRAM})")
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")

    elseif(SCCACHE_PROGRAM)
        message(STATUS "Using compiler cache: sccache (${SCCACHE_PROGRAM})")
        set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")

        # Optional (helps some MSVC setups): ensure environment is used consistently
        # set(ENV{SCCACHE_IDLE_TIMEOUT} "0")

    else()
        message(STATUS "Compiler cache enabled, but neither ccache nor sccache was found.")
    endif()
else()
    message(STATUS "Compiler cache disabled (MODULARITY_USE_COMPILER_CACHE=OFF).")
endif()

# ==================== WINDOWS FIXES (only active on Windows) ====================
if(WIN32)
    set(MODULARITY_WINDOWS_DEFINES
        NOMINMAX                  # Fixes std::min/std::max clash with Windows.h
        WIN32_LEAN_AND_MEAN       # Speeds up Windows.h includes
        _CRT_SECURE_NO_WARNINGS   # Silences strncpy, sscanf, etc. warnings
    )
endif()

# ==================== Compiler flags ====================
if(MSVC)
    set(MODULARITY_WARNING_FLAGS /W4 /O2 /permissive- /MP)
else()
    set(MODULARITY_WARNING_FLAGS -Wall -Wextra -Wpedantic -O2)
endif()

# ==================== Optional PhysX ====================
option(MODULARITY_ENABLE_PHYSX "Enable PhysX physics integration" ON)
option(MODULARITY_BUILD_EDITOR "Build the Modularity editor target" ON)
option(MODULARITY_ENABLE_VULKAN "Enable experimental Vulkan backend" ON)
option(MODULARITY_ENABLE_SNDFILE "Enable libsndfile-based audio import support" ON)
option(MODULARITY_ENABLE_OPUSFILE "Enable libopusfile-based Ogg/Opus decoding support" ON)
set(MODULARITY_SCRIPT_SDK_COPY_SCRIPT ${CMAKE_SOURCE_DIR}/cmake/CopyScriptSDK.cmake)
set(MODULARITY_RUNTIME_DLL_COPY_SCRIPT ${CMAKE_SOURCE_DIR}/cmake/CopyRuntimeDlls.cmake)
set(MODULARITY_FFMPEG_RUNTIME_DLLS)
set(MODULARITY_FFMPEG_RUNTIME_DLL_DIRS)
set(MODULARITY_TEMPLATE_PROJECTS_DIR ${CMAKE_SOURCE_DIR}/Template-Projects)
if(NOT IS_DIRECTORY ${MODULARITY_TEMPLATE_PROJECTS_DIR})
    set(MODULARITY_TEMPLATE_PROJECTS_DIR ${CMAKE_BINARY_DIR}/generated-empty/Template-Projects)
    file(MAKE_DIRECTORY ${MODULARITY_TEMPLATE_PROJECTS_DIR})
endif()

function(modularity_require_real_asset asset_path)
    set(full_path "${CMAKE_SOURCE_DIR}/${asset_path}")
    if(NOT EXISTS "${full_path}")
        message(FATAL_ERROR "Required asset is missing: ${asset_path}. Run git lfs install && git lfs pull.")
    endif()

    file(READ "${full_path}" asset_header LIMIT 64)
    if(asset_header MATCHES "^version https://git-lfs.github.com/spec/v1")
        message(FATAL_ERROR "Required asset is still a Git LFS pointer: ${asset_path}. Run git lfs install && git lfs pull.")
    endif()
endfunction()

modularity_require_real_asset("Resources/Engine-Root/Modu-Logo.png")
modularity_require_real_asset("Resources/Fonts/TheSunset.ttf")
modularity_require_real_asset("Resources/Fonts/Thesunsethd-Regular (1).ttf")

set(MODULARITY_HAS_VULKAN OFF)
if(MODULARITY_ENABLE_VULKAN)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        set(MODULARITY_HAS_VULKAN ON)
        message(STATUS "Experimental Vulkan backend enabled.")
    else()
        message(STATUS "Vulkan SDK not found; Vulkan backend disabled.")
    endif()
endif()

file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}" MODULARITY_SOURCE_DIR_CMAKE)

function(modularity_find_windows_imported_runtime_dll out_var import_lib stem)
    if(NOT WIN32)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    if(NOT import_lib)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    get_filename_component(import_dir "${import_lib}" DIRECTORY)
    get_filename_component(import_parent "${import_dir}" DIRECTORY)

    set(search_dirs
        "${import_dir}"
        "${import_parent}"
        "${import_parent}/bin"
        "${import_parent}/bin/x64"
        "${import_parent}/bin/Release"
        "${import_parent}/../bin"
        "${import_parent}/../bin/x64"
        "${import_parent}/../bin/Release"
    )
    list(REMOVE_DUPLICATES search_dirs)

    foreach(search_dir IN LISTS search_dirs)
        if(NOT EXISTS "${search_dir}")
            continue()
        endif()

        file(GLOB dll_candidates
            "${search_dir}/${stem}.dll"
            "${search_dir}/${stem}-*.dll"
            "${search_dir}/lib${stem}.dll"
            "${search_dir}/lib${stem}-*.dll"
        )
        list(SORT dll_candidates COMPARE NATURAL ORDER DESCENDING)
        list(LENGTH dll_candidates dll_count)
        if(dll_count GREATER 0)
            list(GET dll_candidates 0 resolved_dll)
            set(${out_var} "${resolved_dll}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

# ==================== Third-party libraries ====================

set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build the GLFW documentation" FORCE)
set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "Build Assimp documentation" FORCE)

add_subdirectory(src/ThirdParty/glfw EXCLUDE_FROM_ALL)
find_package(OpenGL REQUIRED)
find_package(PkgConfig QUIET)
set(SNDFILE_FOUND FALSE)
if(MODULARITY_ENABLE_SNDFILE AND PkgConfig_FOUND)
    pkg_check_modules(SNDFILE QUIET IMPORTED_TARGET sndfile)
endif()

set(OPUSFILE_FOUND FALSE)
if(MODULARITY_ENABLE_OPUSFILE AND PkgConfig_FOUND)
    pkg_check_modules(OPUSFILE QUIET IMPORTED_TARGET opusfile)
    if(OPUSFILE_FOUND)
        message(STATUS "Ogg/Opus decoding enabled via libopusfile ${OPUSFILE_VERSION}.")
    else()
        message(STATUS "libopusfile not found; Ogg/Opus playback disabled. "
                       "Install one of: opusfile (Arch), libopusfile-dev (Debian/Ubuntu), "
                       "opusfile-devel (Fedora). Pass -DMODULARITY_ENABLE_OPUSFILE=OFF to silence.")
    endif()
endif()

set(MODULARITY_HAS_FFMPEG OFF)
if(PkgConfig_FOUND AND NOT WIN32)
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET
        libavformat
        libavcodec
        libavutil
        libswresample
        libswscale
    )
endif()

if(FFMPEG_FOUND)
    add_library(modularity_ffmpeg INTERFACE)
    target_link_libraries(modularity_ffmpeg INTERFACE PkgConfig::FFMPEG)
    set(MODULARITY_HAS_FFMPEG ON)
else()
    find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h)
    find_library(FFMPEG_AVFORMAT_LIBRARY avformat)
    find_library(FFMPEG_AVCODEC_LIBRARY avcodec)
    find_library(FFMPEG_AVUTIL_LIBRARY avutil)
    find_library(FFMPEG_SWRESAMPLE_LIBRARY swresample)
    find_library(FFMPEG_SWSCALE_LIBRARY swscale)

    if(FFMPEG_INCLUDE_DIR AND
       FFMPEG_AVFORMAT_LIBRARY AND
       FFMPEG_AVCODEC_LIBRARY AND
       FFMPEG_AVUTIL_LIBRARY AND
       FFMPEG_SWRESAMPLE_LIBRARY AND
       FFMPEG_SWSCALE_LIBRARY)
        add_library(modularity_ffmpeg INTERFACE)
        target_include_directories(modularity_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIR})
        target_link_libraries(modularity_ffmpeg INTERFACE
            ${FFMPEG_AVFORMAT_LIBRARY}
            ${FFMPEG_AVCODEC_LIBRARY}
            ${FFMPEG_AVUTIL_LIBRARY}
            ${FFMPEG_SWRESAMPLE_LIBRARY}
            ${FFMPEG_SWSCALE_LIBRARY}
        )
        if(WIN32)
            modularity_find_windows_imported_runtime_dll(FFMPEG_AVFORMAT_DLL "${FFMPEG_AVFORMAT_LIBRARY}" "avformat")
            modularity_find_windows_imported_runtime_dll(FFMPEG_AVCODEC_DLL "${FFMPEG_AVCODEC_LIBRARY}" "avcodec")
            modularity_find_windows_imported_runtime_dll(FFMPEG_AVUTIL_DLL "${FFMPEG_AVUTIL_LIBRARY}" "avutil")
            modularity_find_windows_imported_runtime_dll(FFMPEG_SWRESAMPLE_DLL "${FFMPEG_SWRESAMPLE_LIBRARY}" "swresample")
            modularity_find_windows_imported_runtime_dll(FFMPEG_SWSCALE_DLL "${FFMPEG_SWSCALE_LIBRARY}" "swscale")
            foreach(ffmpeg_runtime_dll IN ITEMS
                "${FFMPEG_AVFORMAT_DLL}"
                "${FFMPEG_AVCODEC_DLL}"
                "${FFMPEG_AVUTIL_DLL}"
                "${FFMPEG_SWRESAMPLE_DLL}"
                "${FFMPEG_SWSCALE_DLL}"
            )
                if(ffmpeg_runtime_dll)
                    list(APPEND MODULARITY_FFMPEG_RUNTIME_DLLS "${ffmpeg_runtime_dll}")
                    get_filename_component(ffmpeg_runtime_dir "${ffmpeg_runtime_dll}" DIRECTORY)
                    list(APPEND MODULARITY_FFMPEG_RUNTIME_DLL_DIRS "${ffmpeg_runtime_dir}")
                endif()
            endforeach()
            list(REMOVE_DUPLICATES MODULARITY_FFMPEG_RUNTIME_DLLS)
            list(REMOVE_DUPLICATES MODULARITY_FFMPEG_RUNTIME_DLL_DIRS)
            if(MODULARITY_FFMPEG_RUNTIME_DLLS)
                message(STATUS "Windows FFmpeg runtime DLLs: ${MODULARITY_FFMPEG_RUNTIME_DLLS}")
                message(STATUS "Windows FFmpeg runtime DLL directories: ${MODULARITY_FFMPEG_RUNTIME_DLL_DIRS}")
            else()
                message(WARNING "FFmpeg import libraries were found, but matching Windows runtime DLLs were not detected. The executables will rely on PATH to locate avformat/avcodec/avutil/swresample/swscale.")
            endif()
        endif()
        set(MODULARITY_HAS_FFMPEG ON)
    endif()
endif()

if(NOT MODULARITY_HAS_FFMPEG)
    message(FATAL_ERROR "FFmpeg development libraries are required for Modularity video playback integration.")
endif()

# ==================== Mono (managed scripting) ====================
option(MODULARITY_USE_MONO "Enable Mono embedding for managed scripts" ON)

if(MODULARITY_USE_MONO)
    set(MONO_ROOT "" CACHE PATH "Mono root directory")
    if(NOT MONO_ROOT)
        if(DEFINED ENV{MONO_ROOT} AND NOT "$ENV{MONO_ROOT}" STREQUAL "")
            set(MONO_ROOT "$ENV{MONO_ROOT}" CACHE PATH "Mono root directory" FORCE)
        else()
            set(MONO_ROOT "${PROJECT_SOURCE_DIR}/src/ThirdParty/mono" CACHE PATH "Mono root directory" FORCE)
        endif()
    endif()
    find_path(MONO_INCLUDE_DIR mono/jit/jit.h
        HINTS
            ${MONO_ROOT}/include/mono-2.0
            ${MONO_ROOT}/include
    )
    find_library(MONO_LIBRARY
        NAMES mono-2.0-sgen mono-2.0 monosgen-2.0
        HINTS
            ${MONO_ROOT}/lib
            ${MONO_ROOT}/lib64
    )
    if(NOT MONO_INCLUDE_DIR OR NOT MONO_LIBRARY)
        message(WARNING "Mono not found. Disabling MODULARITY_USE_MONO. Set MONO_ROOT to a Mono runtime with include/mono-2.0 and libmono-2.0-sgen.")
        set(MODULARITY_USE_MONO OFF CACHE BOOL "Enable Mono embedding for managed scripts" FORCE)
    elseif(WIN32)
        find_file(MONO_RUNTIME_DLL
            NAMES mono-2.0-sgen.dll libmono-2.0-sgen.dll monosgen-2.0.dll
            HINTS
                ${MONO_ROOT}/bin
                ${MONO_ROOT}/lib
                ${MONO_ROOT}/lib64
        )
    endif()
endif()

# GLAD
add_library(glad STATIC src/ThirdParty/glad/glad.c)
target_include_directories(glad PUBLIC src/ThirdParty/glad)

# GLM (header-only)
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE src/ThirdParty/glm)

# ImGuizmo
add_library(imguizmo STATIC
    src/ThirdParty/ImGuizmo/ImGuizmo.cpp
)
target_include_directories(imguizmo PUBLIC src/ThirdParty/ImGuizmo)
target_link_libraries(imguizmo PUBLIC imgui glm)

# Dear ImGui
set(IMGUI_DIR ${PROJECT_SOURCE_DIR}/src/ThirdParty/imgui)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
if(MODULARITY_HAS_VULKAN)
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
endif()

add_library(imgui STATIC ${IMGUI_SOURCES})
if(WIN32 AND MSVC)
    target_compile_definitions(imgui PRIVATE IMGUI_API=__declspec\(dllexport\))
endif()

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${PROJECT_SOURCE_DIR}/src/ThirdParty/glfw/include    # <-- FIX
)

target_link_libraries(imgui PRIVATE glfw)
if(MODULARITY_HAS_VULKAN)
    target_link_libraries(imgui PRIVATE Vulkan::Vulkan)
endif()

# ==================== Your code (separated files) ====================
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/*.cpp
)

file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/*.h
    ${PROJECT_SOURCE_DIR}/src/*.hpp
)

set(MODULARITY_EDITOR_TEXTEDITOR_SOURCE
    ${PROJECT_SOURCE_DIR}/src/ThirdParty/ImGuiColorTextEdit/TextEditor.cpp
)

set(MODULARITY_PLAYER_EDITOR_ONLY_SOURCES
    ${MODULARITY_EDITOR_TEXTEDITOR_SOURCE}
    ${PROJECT_SOURCE_DIR}/src/EditorWindows/AnimationWindow.cpp
    ${PROJECT_SOURCE_DIR}/src/EditorWindows/BuildSettingsWindow.cpp
    ${PROJECT_SOURCE_DIR}/src/EditorWindows/EnvironmentWindows.cpp
    ${PROJECT_SOURCE_DIR}/src/EditorWindows/FileBrowserWindow.cpp
    ${PROJECT_SOURCE_DIR}/src/EditorWindows/GameProfilerWindow.cpp
    ${PROJECT_SOURCE_DIR}/src/EditorWindows/PixelSpriteEditorWindow.cpp
    ${PROJECT_SOURCE_DIR}/src/EditorWindows/ProjectManagerWindow.cpp
    ${PROJECT_SOURCE_DIR}/src/EditorWindows/RegistryPackagesWindow.cpp
    ${PROJECT_SOURCE_DIR}/src/EditorWindows/Panels/SceneHierarchyWindow.cpp
    ${PROJECT_SOURCE_DIR}/src/EditorWindows/ScriptingWindow.cpp
)

list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/ThirdParty/.*")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/ThirdParty/assimp/.*")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/ThirdParty/PhysX/.*")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/main_lsp\\.cpp$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/main_player.cpp")
list(FILTER ENGINE_HEADERS EXCLUDE REGEX ".*/ThirdParty/.*")
list(FILTER ENGINE_HEADERS EXCLUDE REGEX ".*/ThirdParty/assimp/.*")
list(FILTER ENGINE_HEADERS EXCLUDE REGEX ".*/ThirdParty/PhysX/.*")

set(ENGINE_PLAYER_SOURCES ${ENGINE_SOURCES})
list(APPEND ENGINE_SOURCES ${MODULARITY_EDITOR_TEXTEDITOR_SOURCE})
list(REMOVE_ITEM ENGINE_PLAYER_SOURCES ${MODULARITY_PLAYER_EDITOR_ONLY_SOURCES})

add_library(core STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})
modularity_apply_cxx_api_standard(core)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "Disable Assimp warnings as errors" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Disable Assimp tests" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "Disable Assimp samples" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "Disable Assimp tools" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "Disable Assimp install targets" FORCE)
if(WIN32)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build package with shared libraries." FORCE)
endif()
# Force vendored zlib/minizip in assimp to avoid host minizip/unzip.h dependency on clean builds.
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "Build Assimp zlib/minizip from source" FORCE)
set(ASSIMP_BUILD_MINIZIP ON CACHE BOOL "Build Assimp minizip from source" FORCE)
# Some distros expose broken minizip pkg-config include dirs (/usr/include instead of /usr/include/minizip).
# Ensure assimp does not prefer that cached result.
set(UNZIP_FOUND FALSE CACHE INTERNAL "Force Assimp to use vendored unzip headers" FORCE)
add_subdirectory(src/ThirdParty/assimp EXCLUDE_FROM_ALL)
target_include_directories(assimp PRIVATE ${PROJECT_SOURCE_DIR}/src/ThirdParty/assimp/contrib/unzip)
target_link_libraries(core PUBLIC assimp)
target_include_directories(core PUBLIC 
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/ThirdParty/assimp/include
)
target_link_libraries(core PUBLIC glad glm imgui imguizmo)
target_link_libraries(core PUBLIC modularity_ffmpeg)
if(MODULARITY_HAS_VULKAN)
    target_link_libraries(core PUBLIC Vulkan::Vulkan)
endif()
if(MODULARITY_USE_MONO)
    target_include_directories(core PUBLIC ${MONO_INCLUDE_DIR})
    target_link_libraries(core PUBLIC ${MONO_LIBRARY})
endif()
target_compile_definitions(core PUBLIC MODULARITY_USE_MONO=$<BOOL:${MODULARITY_USE_MONO}>)
target_compile_definitions(core PUBLIC MODULARITY_HAS_VULKAN=$<BOOL:${MODULARITY_HAS_VULKAN}>)
target_compile_definitions(core PUBLIC MODULARITY_HAS_SNDFILE=$<BOOL:${SNDFILE_FOUND}>)
target_compile_definitions(core PUBLIC MODULARITY_HAS_OPUSFILE=$<BOOL:${OPUSFILE_FOUND}>)
target_compile_definitions(core PUBLIC MODULARITY_SOURCE_DIR="${MODULARITY_SOURCE_DIR_CMAKE}")
if(WIN32)
    target_compile_definitions(core PUBLIC ${MODULARITY_WINDOWS_DEFINES} MODULARITY_SCRIPT_HOST)
endif()
if(SNDFILE_FOUND)
    target_link_libraries(core PUBLIC PkgConfig::SNDFILE)
endif()
if(OPUSFILE_FOUND)
    target_link_libraries(core PUBLIC PkgConfig::OPUSFILE)
endif()
target_compile_options(core PRIVATE ${MODULARITY_WARNING_FLAGS})

add_library(core_player STATIC ${ENGINE_PLAYER_SOURCES} ${ENGINE_HEADERS})
modularity_apply_cxx_api_standard(core_player)
target_compile_definitions(core_player PUBLIC MODULARITY_PLAYER)
target_compile_definitions(core_player PUBLIC MODULARITY_RUNTIME_ONLY=1)
target_link_libraries(core_player PUBLIC assimp)
target_include_directories(core_player PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/ThirdParty/assimp/include
)
target_link_libraries(core_player PUBLIC glad glm imgui imguizmo)
target_link_libraries(core_player PUBLIC modularity_ffmpeg)
if(MODULARITY_HAS_VULKAN)
    target_link_libraries(core_player PUBLIC Vulkan::Vulkan)
endif()
if(MODULARITY_USE_MONO)
    target_include_directories(core_player PUBLIC ${MONO_INCLUDE_DIR})
    target_link_libraries(core_player PUBLIC ${MONO_LIBRARY})
endif()
target_compile_definitions(core_player PUBLIC MODULARITY_USE_MONO=$<BOOL:${MODULARITY_USE_MONO}>)
target_compile_definitions(core_player PUBLIC MODULARITY_HAS_VULKAN=$<BOOL:${MODULARITY_HAS_VULKAN}>)
target_compile_definitions(core_player PUBLIC MODULARITY_HAS_SNDFILE=$<BOOL:${SNDFILE_FOUND}>)
target_compile_definitions(core_player PUBLIC MODULARITY_HAS_OPUSFILE=$<BOOL:${OPUSFILE_FOUND}>)
target_compile_definitions(core_player PUBLIC MODULARITY_SOURCE_DIR="${MODULARITY_SOURCE_DIR_CMAKE}")
if(WIN32)
    target_compile_definitions(core_player PUBLIC ${MODULARITY_WINDOWS_DEFINES} MODULARITY_SCRIPT_HOST)
endif()
if(SNDFILE_FOUND)
    target_link_libraries(core_player PUBLIC PkgConfig::SNDFILE)
endif()
if(OPUSFILE_FOUND)
    target_link_libraries(core_player PUBLIC PkgConfig::OPUSFILE)
endif()
target_compile_options(core_player PRIVATE ${MODULARITY_WARNING_FLAGS})

function(modularity_stage_windows_runtime_dlls target_name)
    if(NOT WIN32)
        return()
    endif()

    set(runtime_dlls "$<TARGET_RUNTIME_DLLS:${target_name}>")
    if(MODULARITY_USE_MONO AND MONO_RUNTIME_DLL)
        list(APPEND runtime_dlls "${MONO_RUNTIME_DLL}")
    endif()
    if(MODULARITY_FFMPEG_RUNTIME_DLLS)
        list(APPEND runtime_dlls ${MODULARITY_FFMPEG_RUNTIME_DLLS})
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DDST_DIR=$<TARGET_FILE_DIR:${target_name}>"
            "-DRUNTIME_DLLS=$<JOIN:${runtime_dlls},;>"
            "-DRUNTIME_DLL_DIRS=$<JOIN:${MODULARITY_FFMPEG_RUNTIME_DLL_DIRS},;>"
            -P "${MODULARITY_RUNTIME_DLL_COPY_SCRIPT}"
        VERBATIM
    )
endfunction()

if(MODULARITY_ENABLE_PHYSX)
    set(PHYSX_ROOT_DIR ${PROJECT_SOURCE_DIR}/src/ThirdParty/PhysX/physx CACHE PATH "PhysX root directory")
    if(WIN32)
    set(TARGET_BUILD_PLATFORM "windows" CACHE STRING "PhysX build platform" FORCE)
    else()
    set(TARGET_BUILD_PLATFORM "linux" CACHE STRING "PhysX build platform" FORCE)
    endif()
    # ---- PhysX hard disables (must be BEFORE add_subdirectory) ----
    set(PX_SUPPORT_GLUT OFF CACHE BOOL "" FORCE)
    set(PX_USE_GLUT OFF CACHE BOOL "" FORCE)
    set(PX_ENABLE_GLUT OFF CACHE BOOL "" FORCE)
    set(PX_BUILDSNIPPETS OFF CACHE BOOL "" FORCE)
    set(PX_BUILDPVDRUNTIME OFF CACHE BOOL "" FORCE)
    set(PX_BUILDPUBLICSAMPLES OFF CACHE BOOL "" FORCE)
    set(PX_BUILDTESTS OFF CACHE BOOL "" FORCE)
    set(PX_BUILDFASTXML OFF CACHE BOOL "" FORCE)
    set(PX_GENERATE_GPU_PROJECTS OFF CACHE BOOL "" FORCE)
    set(PX_GENERATE_GPU_PROJECTS_ONLY OFF CACHE BOOL "" FORCE)

    # PhysX build system expects output locations when using the GameWorks layout.
    set(PX_OUTPUT_LIB_DIR ${CMAKE_BINARY_DIR}/physx CACHE PATH "PhysX output lib directory")
    set(PX_OUTPUT_BIN_DIR ${CMAKE_BINARY_DIR}/physx/bin CACHE PATH "PhysX output bin directory")
    if(WIN32)
        # The PhysX public wrapper is required on Windows because the raw SDK
        # entrypoint references private NVIDIA-only sources when PUBLIC_RELEASE
        # is not enabled. Its public-release path still tries to copy optional
        # freeglut DLLs, so provide local placeholders to satisfy configure.
        set(PHYSX_FREEGLUT_STUB_DIR ${CMAKE_BINARY_DIR}/physx/freeglut-stub/bin/win64)
        file(MAKE_DIRECTORY ${PHYSX_FREEGLUT_STUB_DIR})
        file(WRITE ${PHYSX_FREEGLUT_STUB_DIR}/freeglut.dll "")
        file(WRITE ${PHYSX_FREEGLUT_STUB_DIR}/freeglutd.dll "")
        set(PHYSX_SLN_FREEGLUT_PATH ${CMAKE_BINARY_DIR}/physx/freeglut-stub/bin CACHE PATH "PhysX freeglut copy path" FORCE)
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(MODULARITY_SAVED_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=free-nonheap-object")
    endif()
    add_subdirectory(${PHYSX_ROOT_DIR}/compiler/public ${CMAKE_BINARY_DIR}/physx)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_CXX_FLAGS "${MODULARITY_SAVED_CMAKE_CXX_FLAGS}")
        unset(MODULARITY_SAVED_CMAKE_CXX_FLAGS)
    endif()
    target_include_directories(core PUBLIC ${PHYSX_ROOT_DIR}/include)
    target_compile_definitions(core PUBLIC MODULARITY_ENABLE_PHYSX PX_PHYSX_STATIC_LIB)
    target_link_libraries(core PUBLIC PhysX PhysXCommon PhysXFoundation PhysXExtensions PhysXCooking)
    target_include_directories(core_player PUBLIC ${PHYSX_ROOT_DIR}/include)
    target_compile_definitions(core_player PUBLIC MODULARITY_ENABLE_PHYSX PX_PHYSX_STATIC_LIB)
    target_link_libraries(core_player PUBLIC PhysX PhysXCommon PhysXFoundation PhysXExtensions PhysXCooking)
endif()

# ==================== Executable ====================
if(MODULARITY_BUILD_EDITOR)
    add_executable(Modularity src/main.cpp)
    modularity_apply_cxx_api_standard(Modularity)
    target_compile_options(Modularity PRIVATE ${MODULARITY_WARNING_FLAGS})
    if(WIN32)
        target_compile_definitions(Modularity PRIVATE ${MODULARITY_WINDOWS_DEFINES})
        set_target_properties(Modularity PROPERTIES
            ENABLE_EXPORTS ON
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            WIN32_EXECUTABLE "$<NOT:$<CONFIG:Debug>>"
        )
    endif()
endif()
add_executable(ModularityPlayer src/main_player.cpp)
modularity_apply_cxx_api_standard(ModularityPlayer)
target_compile_options(ModularityPlayer PRIVATE ${MODULARITY_WARNING_FLAGS})
if(WIN32)
    target_compile_definitions(ModularityPlayer PRIVATE ${MODULARITY_WINDOWS_DEFINES})
    set_target_properties(ModularityPlayer PROPERTIES
        ENABLE_EXPORTS ON
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        WIN32_EXECUTABLE "$<NOT:$<CONFIG:Debug>>"
    )
endif()

# Link order matters on Linux
if(NOT WIN32)
    find_package(X11 REQUIRED)
    if(MODULARITY_BUILD_EDITOR)
        target_include_directories(Modularity PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(Modularity PRIVATE
            core
            imgui
            imguizmo
            glad
            glm
            glfw
            OpenGL::GL
            pthread
            dl
            ${X11_LIBRARIES}
            Xrandr
            Xi
            Xinerama
            Xcursor
        )
        # Export symbols so runtime-loaded scripts can resolve ImGui/engine symbols.
        target_link_options(Modularity PRIVATE "-rdynamic")
    endif()

    target_include_directories(ModularityPlayer PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(ModularityPlayer PRIVATE
        core_player
        imgui
        imguizmo
        glad
        glm
        glfw
        OpenGL::GL
        pthread
        dl
        ${X11_LIBRARIES}
        Xrandr
        Xi
        Xinerama
        Xcursor
    )
    target_link_options(ModularityPlayer PRIVATE "-rdynamic")
else()
    if(MODULARITY_BUILD_EDITOR)
        target_link_libraries(Modularity PRIVATE core glfw OpenGL::GL)
        if(MSVC)
            target_link_options(Modularity PRIVATE
                "$<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>"
                "$<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS>"
                "/WHOLEARCHIVE:$<TARGET_FILE:core>"
                "/WHOLEARCHIVE:$<TARGET_FILE:imgui>"
            )
        endif()
    endif()
    target_link_libraries(ModularityPlayer PRIVATE core_player glfw OpenGL::GL)
    if(MSVC)
        target_link_options(ModularityPlayer PRIVATE
            "$<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>"
            "$<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS>"
            "/WHOLEARCHIVE:$<TARGET_FILE:core_player>"
            "/WHOLEARCHIVE:$<TARGET_FILE:imgui>"
        )
    endif()
endif()

# Copy resources once to avoid parallel target races writing the same directory.
if(MODULARITY_BUILD_EDITOR)
    add_custom_command(TARGET Modularity POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Resources
            $<TARGET_FILE_DIR:Modularity>/Resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/docs
            $<TARGET_FILE_DIR:Modularity>/docs
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${MODULARITY_TEMPLATE_PROJECTS_DIR}
            $<TARGET_FILE_DIR:Modularity>/Template-Projects
        COMMAND ${CMAKE_COMMAND}
            "-DSRC_ROOT=${CMAKE_SOURCE_DIR}"
            "-DBUILD_ROOT=${CMAKE_BINARY_DIR}"
            "-DDST_ROOT=$<TARGET_FILE_DIR:Modularity>/Resources/ScriptSDK"
            -P "${MODULARITY_SCRIPT_SDK_COPY_SCRIPT}"
    )
else()
    add_custom_command(TARGET ModularityPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Resources
            $<TARGET_FILE_DIR:ModularityPlayer>/Resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/docs
            $<TARGET_FILE_DIR:ModularityPlayer>/docs
        COMMAND ${CMAKE_COMMAND}
            "-DSRC_ROOT=${CMAKE_SOURCE_DIR}"
            "-DBUILD_ROOT=${CMAKE_BINARY_DIR}"
            "-DDST_ROOT=$<TARGET_FILE_DIR:ModularityPlayer>/Resources/ScriptSDK"
            -P "${MODULARITY_SCRIPT_SDK_COPY_SCRIPT}"
    )
endif()

# ==================== Copy PhysX DLLs (Windows) ====================
if(WIN32 AND MODULARITY_ENABLE_PHYSX)
    set(PHYSX_COPY_SCRIPT ${CMAKE_SOURCE_DIR}/cmake/CopyPhysXDLLs.cmake)
    if(MODULARITY_BUILD_EDITOR)
        add_custom_command(TARGET Modularity POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -D SRC_DIR=${PX_OUTPUT_BIN_DIR}
                -D DST_DIR=$<TARGET_FILE_DIR:Modularity>
                -P ${PHYSX_COPY_SCRIPT}
            COMMAND ${CMAKE_COMMAND}
                -D SRC_DIR=${CMAKE_BINARY_DIR}/physx/sdk_source_bin
                -D DST_DIR=$<TARGET_FILE_DIR:Modularity>
                -P ${PHYSX_COPY_SCRIPT}
        )
    endif()
    add_custom_command(TARGET ModularityPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D SRC_DIR=${PX_OUTPUT_BIN_DIR}
            -D DST_DIR=$<TARGET_FILE_DIR:ModularityPlayer>
            -P ${PHYSX_COPY_SCRIPT}
        COMMAND ${CMAKE_COMMAND}
            -D SRC_DIR=${CMAKE_BINARY_DIR}/physx/sdk_source_bin
            -D DST_DIR=$<TARGET_FILE_DIR:ModularityPlayer>
            -P ${PHYSX_COPY_SCRIPT}
    )
endif()

if(WIN32 AND MODULARITY_BUILD_EDITOR)
    modularity_stage_windows_runtime_dlls(Modularity)
endif()
if(WIN32)
    modularity_stage_windows_runtime_dlls(ModularityPlayer)
endif()

include(GNUInstallDirs)
set(MODULARITY_SCRIPT_SDK_INSTALL_SCRIPT ${CMAKE_BINARY_DIR}/InstallScriptSDK.cmake)
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/InstallScriptSDK.cmake.in
    ${MODULARITY_SCRIPT_SDK_INSTALL_SCRIPT}
    @ONLY
)

if(MODULARITY_BUILD_EDITOR)
    install(TARGETS Modularity
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(TARGETS ModularityPlayer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(SCRIPT ${MODULARITY_SCRIPT_SDK_INSTALL_SCRIPT})

if(NOT WIN32 AND MODULARITY_ENABLE_PHYSX)
    file(GLOB PHYSX_SO_FILES
        "${CMAKE_BINARY_DIR}/physx/bin/linux.x86_64/release/*.so"
    )

    install(FILES ${PHYSX_SO_FILES}
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

if(WIN32)
    # Only install the FFmpeg DLLs the engine actually links against
    # (avformat, avcodec, avutil, swresample, swscale). The build-time copy
    # script in CopyRuntimeDlls.cmake also pulls everything in the FFmpeg
    # bin/ dir for safety while iterating, but for distribution we ship only
    # the linked set — avoids dragging in avfilter (~109 MB) and avdevice
    # (~6 MB), which the engine doesn't reference.
    if(MODULARITY_FFMPEG_RUNTIME_DLLS)
        install(FILES ${MODULARITY_FFMPEG_RUNTIME_DLLS}
            DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    if(MODULARITY_USE_MONO AND MONO_RUNTIME_DLL)
        install(FILES "${MONO_RUNTIME_DLL}"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    if(MODULARITY_ENABLE_PHYSX)
        install(CODE "
            file(GLOB _modularity_physx_dlls
                \"${PX_OUTPUT_BIN_DIR}/*/*/*.dll\"
                \"${PX_OUTPUT_BIN_DIR}/*.dll\"
                \"${CMAKE_BINARY_DIR}/physx/sdk_source_bin/*.dll\"
            )
            foreach(_physx_dll IN LISTS _modularity_physx_dlls)
                file(INSTALL \"\${_physx_dll}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
            endforeach()
        ")
    endif()

    # PhysX (and a few other vendored CMake subprojects) install their own
    # static .lib / .pdb / .exp files alongside the runtime DLLs. The runtime
    # player does not need any of them — they exist only for SDK consumers
    # linking against PhysX. Prune them after all install rules have run so
    # the CPack-produced ZIP stays close to the actual runtime payload.
    install(CODE "
        file(GLOB_RECURSE _modularity_runtime_junk
            \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/*.lib\"
            \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/*.pdb\"
            \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/*.exp\"
        )
        foreach(_f IN LISTS _modularity_runtime_junk)
            message(STATUS \"Pruning non-runtime artifact from package: \${_f}\")
            file(REMOVE \"\${_f}\")
        endforeach()

        # Drop empty PhysX layout directories that get left behind
        # (e.g. bin/win.x86_64.vc143.md/release after we strip its .lib/.pdb).
        file(GLOB _modularity_empty_dirs LIST_DIRECTORIES true
            \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/win.*\"
        )
        foreach(_d IN LISTS _modularity_empty_dirs)
            if(IS_DIRECTORY \"\${_d}\")
                file(GLOB _remaining \"\${_d}/*\" \"\${_d}/*/*\")
                if(NOT _remaining)
                    file(REMOVE_RECURSE \"\${_d}\")
                endif()
            endif()
        endforeach()
    ")
endif()

install(DIRECTORY ${CMAKE_SOURCE_DIR}/Resources
    DESTINATION ./bin/
)

install(DIRECTORY ${MODULARITY_TEMPLATE_PROJECTS_DIR}
    DESTINATION ./bin/
)

# ==================== Packaging (CPack) ====================

set(CPACK_PACKAGE_NAME "Modularity")
set(CPACK_PACKAGE_VENDOR "Modularity")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modularity Game Engine")
set(CPACK_PACKAGE_CONTACT "support@moduengine.xyz")

set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}"
)

set(CPACK_GENERATOR "7Z")
# Use all available cores for the LZMA pass — packaging is otherwise
# single-threaded and dominates total CPack wall time on big trees.
set(CPACK_THREADS 0)

set(CPACK_INSTALL_CMAKE_PROJECTS
    "${CMAKE_BINARY_DIR};${CMAKE_PROJECT_NAME};ALL;/"
)

include(CPack)
