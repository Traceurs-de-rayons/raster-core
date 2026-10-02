cmake_minimum_required(VERSION 3.16)
project(raster-core VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Collect sources
file(GLOB_RECURSE RASTER_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# Create library
add_library(raster-core STATIC ${RASTER_CORE_SOURCES})

# Compiler options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	target_compile_options(raster-core PUBLIC -std=gnu++2b)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	target_compile_options(raster-core PUBLIC /std:c++latest)
endif()

# Add all src subdirectories to includes
file(GLOB SRC_SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/*)
set(SRC_INCLUDE_DIRS)
foreach(SUBDIR ${SRC_SUBDIRS})
	if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/${SUBDIR})
		list(APPEND SRC_INCLUDE_DIRS "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/${SUBDIR}>")
	endif()
endforeach()

target_include_directories(raster-core
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
		${SRC_INCLUDE_DIRS}
		$<INSTALL_INTERFACE:include>
)

# Dependencies
if(NOT TARGET scene-core)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../scene-core scene-core-build)
endif()

if(NOT TARGET render-api)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../render-api render-api-build)
endif()

target_link_libraries(raster-core PUBLIC scene-core render-api)

# Shader compilation with Slang
find_program(SLANGC slangc)
if(SLANGC)
	file(GLOB SLANG_SHADERS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.slang")
	file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders)

	set(COMPILED_SHADERS)
	foreach(SHADER_FILE ${SLANG_SHADERS})
		get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)

		# Determine shader stage from filename
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
			continue()
		endif()

		set(OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/shaders/${OUTPUT_NAME})

		add_custom_command(
			OUTPUT ${OUTPUT_FILE}
			COMMAND ${SLANGC} -target spirv -stage ${STAGE} -o ${OUTPUT_FILE} ${SHADER_FILE}
			DEPENDS ${SHADER_FILE}
			COMMENT "Compiling ${SHADER_NAME} (${STAGE})"
		)

		list(APPEND COMPILED_SHADERS ${OUTPUT_FILE})
	endforeach()

	if(COMPILED_SHADERS)
		add_custom_target(raster_core_shaders ALL DEPENDS ${COMPILED_SHADERS})
	endif()
else()
	message(WARNING "slangc not found - shaders will not be compiled")
endif()

target_compile_definitions(raster-core PUBLIC
	RASTER_CORE_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
	RASTER_CORE_SHADER_FALLBACK_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)
