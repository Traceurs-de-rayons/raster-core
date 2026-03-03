cmake_minimum_required(VERSION 3.16)
project(raster-core VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE RASTER_CORE_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(raster-core STATIC ${RASTER_CORE_SOURCES})

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	target_compile_options(raster-core PUBLIC -std=gnu++2b)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	target_compile_options(raster-core PUBLIC /std:c++latest)
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
		target_link_libraries(raster-core PRIVATE stdc++fs)
	endif()
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
		target_link_libraries(raster-core PRIVATE stdc++fs)
	endif()
endif()

target_include_directories(raster-core
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
		$<INSTALL_INTERFACE:include>
)

# Add scene-core dependency
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../scene-core scene-core-build)

# Add render-api dependency
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../render-api render-api-build)

target_link_libraries(raster-core
	PUBLIC scene-core render-api
)

set_target_properties(raster-core PROPERTIES OUTPUT_NAME "raster-core")

target_compile_definitions(raster-core
	PUBLIC 
		RASTER_CORE_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
		RASTER_CORE_SHADER_FALLBACK_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)

# Shader build and installation - copy precompiled .spv shaders
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
	add_custom_target(raster_core_shaders ALL
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/shaders
		${CMAKE_CURRENT_BINARY_DIR}/shaders
		COMMENT "Copying raster-core shaders to build directory"
	)

	add_custom_target(raster_core_shaders_update
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/shaders
		${CMAKE_CURRENT_BINARY_DIR}/shaders
		COMMENT "Updating raster-core shaders in build directory"
	)
else()
	message(WARNING "raster-core shaders directory not found, skipping shader copy")
endif()

include(GNUInstallDirs)
install(TARGETS raster-core
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_INSTALL_DATADIR}/raster-core)

option(BUILD_RASTER_CORE_EXAMPLES "Build examples for raster-core" OFF)
