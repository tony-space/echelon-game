cmake -S . -B build-ninja -G Ninja -DIMF_USE_PRECOMPILED_HEADERS=OFF
ninja -C build-ninja
