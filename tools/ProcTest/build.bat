@echo off
rem Builds ProcTest.exe (Win32, to match RedAlert.dll) into tools\ProcTest\bin.
setlocal
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars32.bat
if not exist "%VCVARS%" (
    echo vcvars32.bat not found: "%VCVARS%"
    exit /b 1
)
call "%VCVARS%" >nul
cd /d "%~dp0"
if not exist bin mkdir bin
cl /nologo /W3 /O2 /MT /EHsc /D_CRT_SECURE_NO_WARNINGS ProcTest.cpp /Fobin\ /Febin\ProcTest.exe /link dbghelp.lib
exit /b %ERRORLEVEL%
