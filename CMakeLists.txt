cmake_minimum_required(VERSION 3.16)
project(MemoryOptimizer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use static runtime for portable deployment
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Sources
set(SOURCES
    src/main.cpp
    src/MemoryOptimizer.cpp
    src/FloatingWindow.cpp
)

set(HEADERS
    src/MemoryOptimizer.h
    src/FloatingWindow.h
)

# Windows GUI application (not console)
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})

# Windows libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    psapi       # EmptyWorkingSet, EnumProcesses
    user32      # window management
    gdi32       # GDI rendering
    shell32     # Shell_NotifyIcon
    comctl32    # common controls
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Compiler options for MSVC
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3 /utf-8)
    # Enable large address awareness
    target_link_options(${PROJECT_NAME} PRIVATE /LARGEADDRESSAWARE)
    # No manifest dependency on CRT DLLs
    set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_DOTNET_REFERENCES ""
    )
endif()

# Install
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION .)
