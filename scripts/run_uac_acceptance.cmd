@echo off
REM Clash Win - one-click UAC acceptance launcher.
REM Double-click this file, approve the single UAC prompt, and the whole
REM #2 package -> #1 verify-signature -> #3 /KEEP x2 -> #4 uninstall chain
REM runs automatically. Reports land in docs\evidence\.
REM Optional args are forwarded, e.g.: run_uac_acceptance.cmd -SkipRebuild
echo Clash Win UAC acceptance launcher
echo Script : %~dp0acceptance_uac.ps1
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0elevate_and_run.ps1" %*
set "CW_RC=%ERRORLEVEL%"
echo.
echo ==========================================
echo  EXITCODE=%CW_RC%
echo  Reports in: %~dp0..\docs\evidence
echo ==========================================
echo.
pause
exit /b %CW_RC%
