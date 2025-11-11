cmake_minimum_required(VERSION 3.20)
project(TaskEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -march=native -O3)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files
set(SOURCES
    src/task_queue.cpp
    src/thread_pool.cpp
    src/task_executor.cpp
    src/dependency_graph.cpp
    src/distributed_executor.cpp
    src/performance_monitor.cpp
)

# Library
add_library(taskengine STATIC ${SOURCES})

# Executables
add_executable(task_engine_demo examples/demo.cpp)
target_link_libraries(task_engine_demo taskengine pthread)

add_executable(performance_test examples/performance_test.cpp)
target_link_libraries(performance_test taskengine pthread)

add_executable(distributed_demo examples/distributed_demo.cpp)
target_link_libraries(distributed_demo taskengine pthread)

add_executable(regression_tests examples/regression_tests.cpp)
target_link_libraries(regression_tests taskengine pthread)

add_executable(advanced_examples examples/advanced_examples.cpp)
target_link_libraries(advanced_examples taskengine pthread)

add_executable(new_features_demo examples/new_features_demo.cpp)
target_link_libraries(new_features_demo taskengine pthread)

# Install
install(TARGETS taskengine DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
