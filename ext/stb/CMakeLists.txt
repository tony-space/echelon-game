cmake_minimum_required(VERSION 3.22.1)

project(STB LANGUAGES C)

set(SRC
	include/stb/stb_image.h
	include/stb/stb_image_write.h

	detail/stb.c
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SRC})

add_library(${PROJECT_NAME} STATIC ${SRC})

set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER ext)
target_compile_definitions(${PROJECT_NAME} PRIVATE STB_IMAGE_WRITE_IMPLEMENTATION)
target_compile_definitions(${PROJECT_NAME} PRIVATE STB_IMAGE_IMPLEMENTATION)

target_include_directories(${PROJECT_NAME} PUBLIC "include")
