@echo off
setlocal
call "%~1\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "SPIDY_CMAKE=%~1\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "SPIDY_NINJA=%~1\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "SPIDY_BUILD=%~dp0..\build\%~2"
"%SPIDY_CMAKE%\cmake.exe" -S "%~dp0.." -B "%SPIDY_BUILD%" -G Ninja "-DCMAKE_MAKE_PROGRAM=%SPIDY_NINJA%" "-DCMAKE_C_COMPILER=%VCToolsInstallDir%bin\Hostx64\x64\cl.exe" "-DCMAKE_CXX_COMPILER=%VCToolsInstallDir%bin\Hostx64\x64\cl.exe" -DCMAKE_BUILD_TYPE=%~3 -DSPIDY_BUILD_XR=%~4 -DSPIDY_BUILD_OBSERVER=%~5
if errorlevel 1 exit /b 1
"%SPIDY_CMAKE%\cmake.exe" --build "%SPIDY_BUILD%" --parallel 4
if errorlevel 1 exit /b 1
"%SPIDY_CMAKE%\ctest.exe" --test-dir "%SPIDY_BUILD%" --output-on-failure
exit /b %errorlevel%
