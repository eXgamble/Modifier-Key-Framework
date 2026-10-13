@echo off
rem Configure + build the Modifier Key Framework SKSE plugin from a VS 2026 developer environment.
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul || exit /b 1
cd /d "%~dp0"
rem The skse-dev-rules checker, when installed (a Claude Code skill): warnings print, an error stops the build.
set "SKSE_LINT=%USERPROFILE%\.claude\skills\skse-dev-rules\scripts\skse_lint.py"
if exist "%SKSE_LINT%" python "%SKSE_LINT%" src || exit /b 4
cmake --preset release || exit /b 2
cmake --build --preset release || exit /b 3
echo BUILD OK
