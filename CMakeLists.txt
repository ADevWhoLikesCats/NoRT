#made by ADevWhoLikesCats
cmake_minimum_required(VERSION 3.16)

project(VulkanSpinningCube LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG QUIET)
find_package(glm  CONFIG REQUIRED)

if(NOT TARGET glfw)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)
    add_library(glfw INTERFACE IMPORTED)
    target_include_directories(glfw INTERFACE ${GLFW_INCLUDE_DIRS})
    target_link_libraries(glfw INTERFACE ${GLFW_LIBRARIES})
    target_link_directories(glfw INTERFACE ${GLFW_LIBRARY_DIRS})
endif()

# ---------------------------------------------------------------------------
# Find glslc (part of the Vulkan SDK)
# ---------------------------------------------------------------------------
find_program(GLSL_COMPILER glslc
    HINTS ENV VULKAN_SDK PATH_SUFFIXES bin
    DOC "Path to glslc (Vulkan shader compiler)")

if(NOT GLSL_COMPILER)
    message(FATAL_ERROR "glslc not found. Install the Vulkan SDK or add glslc to PATH.")
endif()
message(STATUS "Using glslc: ${GLSL_COMPILER}")

# ---------------------------------------------------------------------------
# Compile shaders
#   - Each shader source (bvh.comp, blit.vert, ...) compiles to
#     <build-dir>/shaders/<name>.spv
#   - The build dir mirrors the source layout so readFile("shaders/bvh.comp.spv")
#     works from the build dir.
# ---------------------------------------------------------------------------
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

# Globs the shader dir for *.comp, *.vert, *.frag, *.geom, *.tesc, *.tese
file(GLOB SHADER_SOURCES
    "${SHADER_SRC_DIR}/*.comp"
    "${SHADER_SRC_DIR}/*.vert"
    "${SHADER_SRC_DIR}/*.frag"
    "${SHADER_SRC_DIR}/*.geom"
    "${SHADER_SRC_DIR}/*.tesc"
    "${SHADER_SRC_DIR}/*.tese"
)

set(SHADER_SPIRV_OUTPUTS "")
foreach(src ${SHADER_SOURCES})
    get_filename_component(name "${src}" NAME)   # e.g. "bvh.comp"
    set(out "${SHADER_OUT_DIR}/${name}.spv")     # e.g. "shaders/bvh.comp.spv"

    add_custom_command(
        OUTPUT  "${out}"
        COMMAND "${GLSL_COMPILER}" "${src}" -o "${out}"
        DEPENDS "${src}"
        COMMENT "glslc: ${name} -> ${name}.spv"
        VERBATIM
    )

    list(APPEND SHADER_SPIRV_OUTPUTS "${out}")
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_SPIRV_OUTPUTS})

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(VulkanSpinningCube main.cpp)
add_dependencies(VulkanSpinningCube shaders)

target_link_libraries(VulkanSpinningCube PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)

# ---------------------------------------------------------------------------
# Copy shaders next to the binary at build time
#   The SPIR-V files already live at <build>/shaders/*.spv, which is exactly
#   where readFile("shaders/bvh.comp.spv") will look when run from the build
#   dir. No copy needed. But copy the .obj assets next to the binary so
#   loadOBJ("suzanne.obj") works with cwd = build.
# ---------------------------------------------------------------------------
add_custom_command(TARGET VulkanSpinningCube POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/suzanne.obj"
        "$<TARGET_FILE_DIR:VulkanSpinningCube>/"
    COMMENT "Copying suzanne.obj to build output directory"
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(VulkanSpinningCube PRIVATE NDEBUG)
endif()
