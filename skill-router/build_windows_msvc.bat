@echo off
setlocal

if not exist build mkdir build

REM SQLite with FTS5.
cl /nologo /O2 /MT /c /TC /DSQLITE_THREADSAFE=1 /DSQLITE_ENABLE_FTS5 third_party\sqlite3.c /Fobuild\sqlite3.obj
if errorlevel 1 exit /b %errorlevel%

REM Main executable.
cl /nologo /std:c++20 /EHsc /O2 /MT /W4 /I. main.cpp build\sqlite3.obj ws2_32.lib /Fe:build\skillrouter_msvc.exe
if errorlevel 1 exit /b %errorlevel%

REM C++ contract suite.
cl /nologo /std:c++20 /EHsc /O2 /MT /W4 /I. test_library.cpp build\sqlite3.obj /Fe:build\test_library.exe
if errorlevel 1 exit /b %errorlevel%

REM Stable C ABI DLL and import library.
cl /nologo /std:c++20 /EHsc /O2 /MT /W4 /LD /DSKILLLIB_BUILD_SHARED /I. skilllib_c.cpp build\sqlite3.obj /Fe:build\skillrouter_c.dll /link /IMPLIB:build\skillrouter_c.lib
if errorlevel 1 exit /b %errorlevel%

REM C ABI smoke test, linked directly to the implementation for deterministic CI.
cl /nologo /std:c++20 /EHsc /O2 /MT /W4 /I. test_c_api.cpp skilllib_c.cpp build\sqlite3.obj /Fe:build\test_c_api.exe
if errorlevel 1 exit /b %errorlevel%

copy /Y build\skillrouter_msvc.exe skillrouter.exe >nul
if errorlevel 1 exit /b %errorlevel%

echo Build OK: skillrouter.exe and skillrouter_c.dll updated.
