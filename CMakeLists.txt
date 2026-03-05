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

file(GLOB SRC_CHILDREN RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/*)
set(SRC_INCLUDE_DIRS)
foreach(CHILD ${SRC_CHILDREN})
	if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/${CHILD})
		list(APPEND SRC_INCLUDE_DIRS "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/${CHILD}>")
	endif()
endforeach()

target_include_directories(raster-core
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
		${SRC_INCLUDE_DIRS}
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

# Shader compilation with Slang
find_program(SLANGC slangc)
if(NOT SLANGC)
	message(FATAL_ERROR "slangc not found. Please install Slang compiler.")
endif()

file(GLOB SLANG_SHADERS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.slang")

# Create output directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders)

# Compile each Slang shader to SPIR-V
set(COMPILED_SHADERS)
foreach(SHADER_FILE ${SLANG_SHADERS})
	get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
	get_filename_component(SHADER_EXT ${SHADER_FILE} EXT)

	# Determine shader stage
	if(${SHADER_FILE} MATCHES "\\.vert\\.slang$")
		set(STAGE "vertex")
		set(OUTPUT_NAME "${SHADER_NAME}.vert.spv")
	elseif(${SHADER_FILE} MATCHES "\\.frag\\.slang$")
		set(STAGE "fragment")
		set(OUTPUT_NAME "${SHADER_NAME}.frag.spv")
	elseif(${SHADER_FILE} MATCHES "\\.comp\\.slang$")
		set(STAGE "compute")
		set(OUTPUT_NAME "${SHADER_NAME}.comp.spv")
	elseif(${SHADER_FILE} MATCHES "\\.mesh\\.slang$")
		set(STAGE "mesh")
		set(OUTPUT_NAME "${SHADER_NAME}.mesh.spv")
	else()
		message(WARNING "Unknown shader stage for ${SHADER_FILE}, skipping")
		continue()
	endif()

	set(OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/shaders/${OUTPUT_NAME})

	add_custom_command(
		OUTPUT ${OUTPUT_FILE}
		COMMAND ${SLANGC}
			-target spirv
			-stage ${STAGE}
			-o ${OUTPUT_FILE}
			${SHADER_FILE}
		DEPENDS ${SHADER_FILE}
		COMMENT "Compiling Slang shader: ${SHADER_NAME} (${STAGE})"
		VERBATIM
	)

	list(APPEND COMPILED_SHADERS ${OUTPUT_FILE})
endforeach()

add_custom_target(raster_core_shaders ALL
	DEPENDS ${COMPILED_SHADERS}
	COMMENT "Building all Slang shaders"
)

include(GNUInstallDirs)
install(TARGETS raster-core
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_INSTALL_DATADIR}/raster-core)

option(BUILD_RASTER_CORE_EXAMPLES "Build examples for raster-core" OFF)
