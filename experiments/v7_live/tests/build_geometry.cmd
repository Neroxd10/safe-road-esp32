@echo off
set ZIG_GLOBAL_CACHE_DIR=%CD%\experiments\v7_live\tests\.zig-cache
experiments\v7_direct\.venv\Scripts\python.exe -m ziglang c++ -std=c++17 -Wall -Wextra -Werror experiments\v7_live\tests\geometry_test.cpp -o experiments\v7_live\tests\geometry_test.exe
if errorlevel 1 exit /b 1
experiments\v7_live\tests\geometry_test.exe
