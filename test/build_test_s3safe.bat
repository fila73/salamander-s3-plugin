@echo off
rem Builds and runs test_s3safe.cpp with MSVC (run from a "x64 Native Tools Command Prompt").
rem Pure local tests: no network, files only in %TEMP%.
setlocal
if not exist "%~dp0..\build\test" mkdir "%~dp0..\build\test"
cl /nologo /std:c++17 /EHsc /MT /utf-8 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
  /Fo"%~dp0..\build\test\\" /Fe"%~dp0..\build\test\test_s3safe.exe" ^
  "%~dp0test_s3safe.cpp" /link
if errorlevel 1 exit /b 1
"%~dp0..\build\test\test_s3safe.exe"
endlocal
