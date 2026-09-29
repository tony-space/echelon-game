cmake_minimum_required(VERSION 3.22.1)

project(miniz LANGUAGES C)

# zlib-compatible inflate/deflate (richgel999/miniz 3.0.2). ZIP and file I/O are off.
set(SRC
	miniz.c
	miniz.h
	miniz_common.h
	miniz_export.h
	miniz_tdef.c
	miniz_tdef.h
	miniz_tinfl.c
	miniz_tinfl.h
	miniz_zip.h
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SRC})

add_library(${PROJECT_NAME} STATIC ${SRC})

set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER ext)
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(${PROJECT_NAME}
	PUBLIC MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
