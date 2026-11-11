@echo off
cd /d "%~dp0"
copy /y "app\build\outputs\apk\debug\app-debug.apk" "SonicSync-debug.apk"
