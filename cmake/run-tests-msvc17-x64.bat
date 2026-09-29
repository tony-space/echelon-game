@echo off

call cmake\gen-msvc17-x64.bat
cmake --build build-x64-msvc17 --config=Release
.\build-x64-msvc17\tests\Release\master-test-suite.exe --log_level=all
REM ctest --test-dir build-x64-msvc17/ --build-config Release
