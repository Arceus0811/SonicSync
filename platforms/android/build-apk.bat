@echo off
rem Builds the SonicSync debug APK and runs the JVM unit tests.
rem Output: app\build\outputs\apk\debug\app-debug.apk   Log: build-apk.log
setlocal
cd /d "%~dp0"
if exist "build-apk.done" del "build-apk.done"

rem Use a Java that actually exists; a stale system JAVA_HOME is ignored.
if defined JAVA_HOME if not exist "%JAVA_HOME%\bin\java.exe" set "JAVA_HOME="
if not defined JAVA_HOME if exist "E:\And std\jbr\bin\java.exe" set "JAVA_HOME=E:\And std\jbr"
if not defined JAVA_HOME if exist "E:\andsdk\jbr\bin\java.exe" set "JAVA_HOME=E:\andsdk\jbr"
if not defined JAVA_HOME if exist "C:\Program Files\Android\Android Studio\jbr\bin\java.exe" set "JAVA_HOME=C:\Program Files\Android\Android Studio\jbr"

if exist "E:\SonicSyncToolchain\AndroidSdk" set "ANDROID_SDK_ROOT=E:\SonicSyncToolchain\AndroidSdk"
if not defined ANDROID_SDK_ROOT if exist "%LOCALAPPDATA%\Android\Sdk" set "ANDROID_SDK_ROOT=%LOCALAPPDATA%\Android\Sdk"
set "ANDROID_HOME=%ANDROID_SDK_ROOT%"
if exist "E:\SonicSyncToolchain\GradleCache" set "GRADLE_USER_HOME=E:\SonicSyncToolchain\GradleCache"

>build-apk.log echo JAVA_HOME=%JAVA_HOME%
>>build-apk.log echo ANDROID_SDK_ROOT=%ANDROID_SDK_ROOT%
>>build-apk.log echo GRADLE_USER_HOME=%GRADLE_USER_HOME%
echo Building SonicSync APK... this can take several minutes on the first run.
call gradlew.bat --no-daemon testDebugUnitTest assembleDebug >> build-apk.log 2>&1
set "RESULT=%ERRORLEVEL%"
>>build-apk.log echo BUILD_EXIT=%RESULT%
>build-apk.done echo %RESULT%
if "%RESULT%"=="0" (echo Build succeeded.) else (echo Build FAILED - see build-apk.log)
timeout /t 15 >nul
endlocal
