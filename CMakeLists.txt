cmake_minimum_required(VERSION 3.16)
project(Modularity LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
    add_compile_definitions(
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

# ==================== Third-party libraries ====================

add_subdirectory(src/ThirdParty/glfw EXCLUDE_FROM_ALL)
find_package(OpenGL REQUIRED)

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

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${PROJECT_SOURCE_DIR}/src/ThirdParty/glfw/include    # <-- FIX
)

target_link_libraries(imgui PRIVATE glfw)

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
list(FILTER ENGINE_HEADERS EXCLUDE REGEX ".*/ThirdParty/assimp/.*")
list(FILTER ENGINE_HEADERS EXCLUDE REGEX ".*/ThirdParty/PhysX/.*")

add_library(core STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "Disable Assimp warnings as errors" FORCE)
add_subdirectory(src/ThirdParty/assimp EXCLUDE_FROM_ALL)
target_link_libraries(core PUBLIC assimp)
target_include_directories(core PUBLIC 
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/ThirdParty/assimp/include
)
target_link_libraries(core PUBLIC glad glm imgui imguizmo)
target_compile_options(core PRIVATE ${MODULARITY_WARNING_FLAGS})

if(MODULARITY_ENABLE_PHYSX)
    set(PHYSX_ROOT_DIR ${PROJECT_SOURCE_DIR}/src/ThirdParty/PhysX/physx CACHE PATH "PhysX root directory")
    set(TARGET_BUILD_PLATFORM "linux" CACHE STRING "PhysX build platform (linux/windows)")
    # PhysX build system expects output locations when using the GameWorks layout.
    set(PX_OUTPUT_LIB_DIR ${CMAKE_BINARY_DIR}/physx CACHE PATH "PhysX output lib directory")
    set(PX_OUTPUT_BIN_DIR ${CMAKE_BINARY_DIR}/physx/bin CACHE PATH "PhysX output bin directory")
    add_subdirectory(${PHYSX_ROOT_DIR}/compiler/public ${CMAKE_BINARY_DIR}/physx)
    target_include_directories(core PUBLIC ${PHYSX_ROOT_DIR}/include)
    target_compile_definitions(core PUBLIC MODULARITY_ENABLE_PHYSX PX_PHYSX_STATIC_LIB)
    target_link_libraries(core PUBLIC PhysX PhysXCommon PhysXFoundation PhysXExtensions PhysXCooking)
endif()

# ==================== Executable ====================
add_executable(Modularity src/main.cpp)
target_compile_options(Modularity PRIVATE ${MODULARITY_WARNING_FLAGS})

# Link order matters on Linux
if(NOT WIN32)
    find_package(X11 REQUIRED)
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
else()
    target_link_libraries(Modularity PRIVATE core glfw OpenGL::GL)
endif()

# ==================== Copy Resources folder after build ====================
add_custom_command(TARGET Modularity POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Resources
        $<TARGET_FILE_DIR:Modularity>/Resources
)
