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

# Fallback for systems that only ship FindGLFW3.cmake or pkg-config
if(NOT TARGET glfw)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)
    add_library(glfw INTERFACE IMPORTED)
    target_include_directories(glfw INTERFACE ${GLFW_INCLUDE_DIRS})
    target_link_libraries(glfw INTERFACE ${GLFW_LIBRARIES})
    target_link_directories(glfw INTERFACE ${GLFW_LIBRARY_DIRS})
endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(VulkanSpinningCube main.cpp)

target_link_libraries(VulkanSpinningCube PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)

# ---------------------------------------------------------------------------
# Copy shaders next to the binary at build time
# ---------------------------------------------------------------------------
add_custom_command(TARGET VulkanSpinningCube POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:VulkanSpinningCube>/shaders"
    COMMENT "Copying shaders/ to build output directory"
)

# Optional: define NDEBUG in Release builds so validation layers turn off
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(VulkanSpinningCube PRIVATE NDEBUG)
endif()