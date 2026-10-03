cmake_minimum_required(VERSION 3.16)
project(bloop_native LANGUAGES CXX)

if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)

set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	tinyxml2
	URL https://github.com/leethomason/tinyxml2/archive/refs/tags/11.0.0.tar.gz
)
FetchContent_MakeAvailable(tinyxml2)

FetchContent_Declare(
	lodepng
	URL https://github.com/lvandeve/lodepng/archive/ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a.tar.gz
)
FetchContent_MakeAvailable(lodepng)
add_library(bloop_lodepng STATIC ${lodepng_SOURCE_DIR}/lodepng.cpp)
target_include_directories(bloop_lodepng PUBLIC ${lodepng_SOURCE_DIR})

add_library(bloop_expression expression.cpp)
target_compile_features(bloop_expression PUBLIC cxx_std_17)
target_include_directories(bloop_expression PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(bloop_expression_tests expression_test.cpp)
target_link_libraries(bloop_expression_tests PRIVATE bloop_expression)

add_executable(bloop_native main.cpp scene.cpp)
target_link_libraries(bloop_native PRIVATE bloop_expression tinyxml2::tinyxml2 bloop_lodepng)
target_compile_features(bloop_native PRIVATE cxx_std_17)

enable_testing()
add_test(NAME bloop_expression_tests COMMAND bloop_expression_tests)
add_test(
	NAME bond_logo_smoke
	COMMAND bloop_native
		-i "${CMAKE_CURRENT_SOURCE_DIR}/../bond_logo.xml"
		-o "${CMAKE_CURRENT_BINARY_DIR}/bond_logo_smoke.png"
		-g 32,32,-17,-16
		-s 3
		size1 19 size2 20 corner 14 thickness 4 cut 1 gap 1 color 0
)