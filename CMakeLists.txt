cmake_minimum_required(VERSION 3.16)
project(Modularity LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

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

# ==================== Third-party libraries ====================

set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build the GLFW documentation" FORCE)
set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "Build Assimp documentation" FORCE)

add_subdirectory(src/ThirdParty/glfw EXCLUDE_FROM_ALL)
find_package(OpenGL REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SNDFILE QUIET IMPORTED_TARGET sndfile)
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

list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/ThirdParty/assimp/.*")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/ThirdParty/PhysX/.*")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/main_player.cpp")
list(FILTER ENGINE_HEADERS EXCLUDE REGEX ".*/ThirdParty/assimp/.*")
list(FILTER ENGINE_HEADERS EXCLUDE REGEX ".*/ThirdParty/PhysX/.*")

add_library(core STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "Disable Assimp warnings as errors" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Disable Assimp tests" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "Disable Assimp samples" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "Disable Assimp tools" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "Disable Assimp install targets" FORCE)
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
if(WIN32)
    target_compile_definitions(core PUBLIC ${MODULARITY_WINDOWS_DEFINES})
endif()
if(SNDFILE_FOUND)
    target_link_libraries(core PUBLIC PkgConfig::SNDFILE)
endif()
target_compile_options(core PRIVATE ${MODULARITY_WARNING_FLAGS})

add_library(core_player STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})
target_compile_definitions(core_player PUBLIC MODULARITY_PLAYER)
target_link_libraries(core_player PUBLIC assimp)
target_include_directories(core_player PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/ThirdParty/assimp/include
)
target_link_libraries(core_player PUBLIC glad glm imgui imguizmo)
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
if(WIN32)
    target_compile_definitions(core_player PUBLIC ${MODULARITY_WINDOWS_DEFINES})
endif()
if(SNDFILE_FOUND)
    target_link_libraries(core_player PUBLIC PkgConfig::SNDFILE)
endif()
target_compile_options(core_player PRIVATE ${MODULARITY_WARNING_FLAGS})

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
    set(PX_BUILDPUBLICSAMPLES OFF CACHE BOOL "" FORCE)
    set(PX_BUILDTESTS OFF CACHE BOOL "" FORCE)
    set(PX_BUILDFASTXML OFF CACHE BOOL "" FORCE)

    # PhysX build system expects output locations when using the GameWorks layout.
    set(PX_OUTPUT_LIB_DIR ${CMAKE_BINARY_DIR}/physx CACHE PATH "PhysX output lib directory")
    set(PX_OUTPUT_BIN_DIR ${CMAKE_BINARY_DIR}/physx/bin CACHE PATH "PhysX output bin directory")
    add_subdirectory(${PHYSX_ROOT_DIR}/compiler/public ${CMAKE_BINARY_DIR}/physx)
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
    target_compile_options(Modularity PRIVATE ${MODULARITY_WARNING_FLAGS})
    if(WIN32)
        target_compile_definitions(Modularity PRIVATE ${MODULARITY_WINDOWS_DEFINES})
    endif()
endif()
add_executable(ModularityPlayer src/main_player.cpp)
target_compile_options(ModularityPlayer PRIVATE ${MODULARITY_WARNING_FLAGS})
if(WIN32)
    target_compile_definitions(ModularityPlayer PRIVATE ${MODULARITY_WINDOWS_DEFINES})
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
    endif()
    target_link_libraries(ModularityPlayer PRIVATE core_player glfw OpenGL::GL)
endif()

# Copy resources once to avoid parallel target races writing the same directory.
if(MODULARITY_BUILD_EDITOR)
    add_custom_command(TARGET Modularity POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Resources
            $<TARGET_FILE_DIR:Modularity>/Resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Template-Projects
            $<TARGET_FILE_DIR:Modularity>/Template-Projects
    )
else()
    add_custom_command(TARGET ModularityPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Resources
            $<TARGET_FILE_DIR:ModularityPlayer>/Resources
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

include(GNUInstallDirs)

if(MODULARITY_BUILD_EDITOR)
    install(TARGETS Modularity
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(TARGETS ModularityPlayer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(NOT WIN32 AND MODULARITY_ENABLE_PHYSX)
    file(GLOB PHYSX_SO_FILES
        "${CMAKE_BINARY_DIR}/physx/bin/linux.x86_64/release/*.so"
    )

    install(FILES ${PHYSX_SO_FILES}
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

install(DIRECTORY ${CMAKE_SOURCE_DIR}/Resources
    DESTINATION ./bin/
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/Template-Projects
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

set(CPACK_GENERATOR "TGZ")

set(CPACK_INSTALL_CMAKE_PROJECTS
    "${CMAKE_BINARY_DIR};${CMAKE_PROJECT_NAME};ALL;/"
)

include(CPack)
