# CMakeList.txt : CMake project for StopMotionTool, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("StopMotionTool")

find_package(Alembic CONFIG REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)
find_package(cxxopts REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

find_path(EARCUT_HPP_INCLUDE_DIRS "mapbox/earcut.hpp")

# Add source to this project's executable.
add_executable (StopMotionTool "StopMotionTool.cpp" "StopMotionTool.h")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET StopMotionTool PROPERTY CXX_STANDARD 20)
endif()

target_link_libraries(
	StopMotionTool
	PRIVATE
		Alembic::Alembic
		tinyobjloader::tinyobjloader
		cxxopts::cxxopts
		JsonCpp::JsonCpp
)

target_include_directories(StopMotionTool PRIVATE ${EARCUT_HPP_INCLUDE_DIRS})

# TODO: Add tests and install targets if needed.
