cmake_minimum_required(VERSION 3.16)
project(Modularity LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    add_compile_options(/W4 /O2 /permissive- /MP)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
endif()

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
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PRIVATE glfw)

# ==================== Your code (separated files) ====================
set(ENGINE_SOURCES
    src/Camera.cpp
    src/Rendering.cpp
    src/ProjectManager.cpp
    src/EditorUI.cpp
    src/Engine.cpp
    src/EnginePanels.cpp
    src/Shaders/Shader_Manager/Shader.cpp
    src/Skybox/Skybox.cpp
    src/Textures/Texture.cpp
    src/WinView/Window.cpp
)

set(ENGINE_HEADERS
    src/Common.h
    src/SceneObject.h
    src/Camera.h
    src/Rendering.h
    src/ProjectManager.h
    src/EditorUI.h
    src/Engine.h
)

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


# ==================== Executable ====================
add_executable(Modularity src/main.cpp)

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
else()
    target_link_libraries(Modularity PRIVATE core glfw OpenGL::GL)
endif()

# ==================== Copy Resources folder after build ====================
add_custom_command(TARGET Modularity POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Resources
        $<TARGET_FILE_DIR:Modularity>/Resources
)
