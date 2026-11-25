@echo off
rem Headless check of the Godot importer (res/engine/godot) on the sample packs of tests/EnginePackTests.cpp.
rem   1. out\build\x64-release\ptg_tests.exe --no-skip -tc="Engine sample packs"   (writes out\engine_samples\)
rem   2. tools\check_godot_import.bat <path to Godot 4 console exe>
rem Exit code 0 = every pack passed.
setlocal
if "%~1"=="" (
	echo usage: %~nx0 ^<Godot_v4.x_win64_console.exe^>
	exit /b 2
)
set GODOT=%~1
set REPO=%~dp0..
set SAMPLES=%REPO%\out\engine_samples
set PROJECT=%TEMP%\ptg_godot_check
if not exist "%SAMPLES%\full\full.json" (
	echo Sample packs missing: run ptg_tests --no-skip -tc="Engine sample packs" first
	exit /b 2
)

if exist "%PROJECT%" rmdir /s /q "%PROJECT%"
mkdir "%PROJECT%"
> "%PROJECT%\project.godot" echo config_version=5
>> "%PROJECT%\project.godot" echo [application]
>> "%PROJECT%\project.godot" echo config/name="PTG import check"
xcopy /e /i /q "%REPO%\ProceduralTerrainGenerator\res\engine\godot\addons" "%PROJECT%\addons" > nul
copy /y "%REPO%\ProceduralTerrainGenerator\tests\engine\godot_import_check.gd" "%PROJECT%\" > nul

set FAILED=0
for %%P in (full dem minimal) do (
	"%GODOT%" --headless --path "%PROJECT%" --script res://godot_import_check.gd -- "%SAMPLES%\%%P\%%P.json" copy || set FAILED=1
)
"%GODOT%" --headless --path "%PROJECT%" --import || set FAILED=1
for %%P in (full dem minimal) do (
	"%GODOT%" --headless --path "%PROJECT%" --script res://godot_import_check.gd -- "%SAMPLES%\%%P\%%P.json" build || set FAILED=1
)
if %FAILED%==1 (
	echo GODOT IMPORT CHECK: FAILED
	exit /b 1
)
echo GODOT IMPORT CHECK: PASSED
exit /b 0
