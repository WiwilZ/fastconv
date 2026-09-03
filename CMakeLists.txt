cmake_minimum_required(VERSION 3.20)

project(fastconv_benchmarks LANGUAGES CXX)

option(FASTCONV_NATIVE_ARCH "Enable instructions for the runner's native CPU" ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep executable paths identical for single- and multi-config generators.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/bin")
endforeach()

function(add_fastconv_benchmark target source)
    add_executable(${target} ${source} int2str.h str2int.h utils.h)

    if(FASTCONV_NATIVE_ARCH)
        if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
                target_compile_options(${target} PRIVATE /arch:AVX2)
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
            target_compile_options(${target} PRIVATE -march=native)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
            target_compile_options(${target} PRIVATE -mcpu=native)
        endif()
    endif()
endfunction()

add_fastconv_benchmark(benchmark_str2int benchmark_str2int.cpp)
add_fastconv_benchmark(benchmark_int2str benchmark_int2str.cpp)
