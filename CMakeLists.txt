cmake_minimum_required(VERSION 3.20)
project(DeviousEditorClassicRemastered)

# Geode SDK
find_package(Geode REQUIRED)

# Network libraries for Windows / Apple
if(GEODE_IS_WINDOWS)
    set(PLATFORM_NETWORK_LIBS ws2_32)
    set(OS_DEFINE CURRENT_OS_WINDOWS)
elseif(GEODE_IS_MACOS)
    set(PLATFORM_NETWORK_LIBS "")
    set(OS_DEFINE CURRENT_OS_MACOS)
else()
    set(PLATFORM_NETWORK_LIBS "")
    set(OS_DEFINE CURRENT_OS_UNKNOWN)
endif()

# Gather all cpp source files
file(GLOB_RECURSE SRC "src/*.cpp")

setup_geode_mod(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE ${SRC})
target_link_libraries(${PROJECT_NAME} PRIVATE ${PLATFORM_NETWORK_LIBS})
target_compile_definitions(${PROJECT_NAME} PRIVATE ${OS_DEFINE})
