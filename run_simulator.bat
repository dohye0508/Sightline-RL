@echo off
setlocal

:: Check if the executable exists
if exist "build\bin\Release\rl_constrained_fov.exe" (
    echo [Sightline-RL] Starting the simulator...
    cd build\bin\Release
    start rl_constrained_fov.exe
) else (
    echo [Error] The simulator executable was not found!
    echo Please make sure you have built the project using CMake as described in README.md.
    pause
)

endlocal
