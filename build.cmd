@echo off
rem Builds PATio Boot Doctor (64-bit only) into PATio Commander's PatSuite folder, so it shows
rem up in the PatSuite menu of PATio Commander and of every other PatKit app.
rem 64-bit only (x64): no 32-bit builds.
cd /d "%~dp0"
rem Collect the texts for translation and the language packs to build in
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\extract-strings.ps1"
if errorlevel 1 exit /b 1
call "%~dp0..\PatKit\build-app.cmd" PATioBootDoctor "%~dp0..\PATioCommander\dist\PatSuite\PATio Boot Doctor" x64
