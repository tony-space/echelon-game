cmake_minimum_required(VERSION 3.22.1)

# Warning/optimization flags shared by all first-party targets.
add_library(ECH_CXX_FLAGS INTERFACE)

target_compile_features(ECH_CXX_FLAGS INTERFACE cxx_std_20)

target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/Qpar>)
target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/WX>)
target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/MP>)
target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/permissive->)
target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>)
target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_compile_definitions(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
target_compile_definitions(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>)

foreach(_id Clang AppleClang GNU)
	target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:${_id}>:-Werror>)
	target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:${_id}>:-Wall>)
	target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:${_id}>:-Wextra>)
endforeach()

target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:Clang>:-Wno-tautological-pointer-compare>)
target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:Clang>:-Wno-pointer-bool-conversion>)
target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:Clang>:-Wmissing-prototypes>)

target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:AppleClang>:-Wno-tautological-pointer-compare>)
target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:AppleClang>:-Wno-pointer-bool-conversion>)
target_compile_options(ECH_CXX_FLAGS INTERFACE $<$<CXX_COMPILER_ID:AppleClang>:-Wmissing-prototypes>)
