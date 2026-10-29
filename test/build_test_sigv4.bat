@echo off
rem Compiles test_sigv4.cpp with MSVC (run from a "x64 Native Tools Command Prompt").
rem The test reads real ~/.aws files when run - compile only unless you know what you are doing.
setlocal
if not exist "%~dp0..\build\test" mkdir "%~dp0..\build\test"
cl /nologo /std:c++17 /EHsc /MT /utf-8 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
  /Fo"%~dp0..\build\test\\" /Fe"%~dp0..\build\test\test_sigv4.exe" ^
  "%~dp0test_sigv4.cpp" "%~dp0..\src\s3sigv4.cpp" "%~dp0..\src\awscred.cpp" ^
  /link bcrypt.lib advapi32.lib shell32.lib user32.lib
endlocal
