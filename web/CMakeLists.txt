# WebAssembly bindings build for Box3D.
#
# This consumes the Box3D engine as a subproject (library only — samples, tests,
# and benchmarks are skipped when Box3D is not the top-level project) and links an
# embind binding layer into a single ES6 WASM module for the browser. Configure it
# with Emscripten's toolchain via `emcmake cmake`; see build.sh.
#
# The engine itself is built entirely unmodified: Box3D's C17 core compiles to
# wasm32 as-is, and src/CMakeLists.txt already selects the wasm SIMD path
# (-msimd128 -msse2) under Emscripten.
cmake_minimum_required(VERSION 3.22)
project(box3d_web LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
	message(FATAL_ERROR "Configure with emcmake: `emcmake cmake -B build`")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pull in the engine. Library-only because this is not the top-level project.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/box3d)

add_executable(box3d_demo src/box3d_bindings.cpp)
target_link_libraries(box3d_demo PRIVATE box3d::box3d)

# Emit box3d.js + box3d.wasm.
set_target_properties(box3d_demo PROPERTIES OUTPUT_NAME box3d SUFFIX ".js")

target_link_options(box3d_demo PRIVATE
	"--bind"                        # embind runtime
	"-sMODULARIZE=1"
	"-sEXPORT_ES6=1"
	"-sEXPORT_NAME=createBox3D"
	"-sALLOW_MEMORY_GROWTH=1"
	"-sENVIRONMENT=web,worker"   # the module is instantiated inside a Web Worker
)
