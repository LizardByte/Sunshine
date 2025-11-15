@echo off
setlocal enableDelayedExpansion

rem Run from Qt command prompt with working directory set to root of repo

set BUILD_CONFIG=%1

rem Convert to lower case for windeployqt
if /I "%BUILD_CONFIG%"=="debug" (
    set BUILD_CONFIG=debug
    set WIX_MUMS=10
) else (
    if /I "%BUILD_CONFIG%"=="release" (
        set BUILD_CONFIG=release
        set WIX_MUMS=10
    ) else (
        if /I "%BUILD_CONFIG%"=="signed-release" (
            set BUILD_CONFIG=release
            set SIGN=1
            set MUST_DEPLOY_SYMBOLS=1

            rem Fail if there are unstaged changes
            git diff-index --quiet HEAD --
            if !ERRORLEVEL! NEQ 0 (
                echo Signed release builds must not have unstaged changes!
                exit /b 1
            )
        ) else (
            echo Invalid build configuration - expected 'debug' or 'release'
            echo Usage: scripts\build-arch.bat ^(release^|debug^)
            exit /b 1
        )
    )
)

rem Locate qmake and determine if we're using qmake.exe or qmake.bat
rem qmake.bat is an ARM64 forwarder to the x64 version of qmake.exe
where qmake.bat
if !ERRORLEVEL! EQU 0 (
    set QMAKE_CMD=call qmake.bat
) else (
    where qmake.exe
    if !ERRORLEVEL! EQU 0 (
        set QMAKE_CMD=qmake.exe
    ) else (
        echo Unable to find QMake. Did you add Qt bins to your PATH?
        goto Error
    )
)

rem Find Qt path to determine our architecture
for /F %%i in ('where qmake') do set QT_PATH=%%i

rem Strip the qmake filename off the end to get the Qt bin directory itself
set QT_PATH=%QT_PATH:\qmake.exe=%
set QT_PATH=%QT_PATH:\qmake.bat=%
set QT_PATH=%QT_PATH:\qmake.cmd=%

echo QT_PATH=%QT_PATH%
if not x%QT_PATH:_arm64=%==x%QT_PATH% (
    set ARCH=arm64

    rem Replace the _arm64 suffix with _64 to get the x64 bin path
    set HOSTBIN_PATH=%QT_PATH:_arm64=_64%
    echo HOSTBIN_PATH=!HOSTBIN_PATH!

    if exist %QT_PATH%\windeployqt.exe (
        echo Using windeployqt.exe from QT_PATH
        set WINDEPLOYQT_CMD=windeployqt.exe
    ) else (
        echo Using windeployqt.exe from HOSTBIN_PATH
        set WINDEPLOYQT_CMD=!HOSTBIN_PATH!\windeployqt.exe --qtpaths %QT_PATH%\qtpaths.bat
    )
) else (
    if not x%QT_PATH:_64=%==x%QT_PATH% (
        set ARCH=x64
        set WINDEPLOYQT_CMD=windeployqt.exe
    ) else (
        if not x%QT_PATH:msvc=%==x%QT_PATH% (
            set ARCH=x86
            set WINDEPLOYQT_CMD=windeployqt.exe
        ) else (
            echo Unable to determine Qt architecture
            goto Error
        )
    )
)

echo Detected target architecture: %ARCH%

set SIGNTOOL_PARAMS=sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /sha1 8b9d0d682ad9459e54f05a79694bc10f9876e297 /v

set BUILD_ROOT=%cd%\build
set SOURCE_ROOT=%cd%
set BUILD_FOLDER=%BUILD_ROOT%\build-%ARCH%-%BUILD_CONFIG%
set DEPLOY_FOLDER=%BUILD_ROOT%\deploy-%ARCH%-%BUILD_CONFIG%
set INSTALLER_FOLDER=%BUILD_ROOT%\installer-%ARCH%-%BUILD_CONFIG%
set SYMBOLS_FOLDER=%BUILD_ROOT%\symbols-%ARCH%-%BUILD_CONFIG%

rem Allow CI to override the version.txt with an environment variable
if defined CI_VERSION (
    set VERSION=%CI_VERSION%
) else (
    set /p VERSION=<%SOURCE_ROOT%\app\version.txt
)

rem Use the correct VC tools for the specified architecture
if /I "%ARCH%" EQU "x64" (
    rem x64 is a special case that doesn't match %PROCESSOR_ARCHITECTURE%
    set VC_ARCH=AMD64
) else (
    set VC_ARCH=%ARCH%
)

rem If we're not building for the current platform, use the cross compiling toolchain
if /I "%VC_ARCH%" NEQ "%PROCESSOR_ARCHITECTURE%" (
    set VC_ARCH=%PROCESSOR_ARCHITECTURE%_%VC_ARCH%
)

rem Find Visual Studio and run vcvarsall.bat
set VSWHERE="%SOURCE_ROOT%\scripts\vswhere.exe"
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -property installationPath`) do (
    call "%%i\VC\Auxiliary\Build\vcvarsall.bat" %VC_ARCH%
)
if !ERRORLEVEL! NEQ 0 goto Error

rem Find VC redistributable DLLs
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -find VC\Redist\MSVC\*\%ARCH%\Microsoft.VC*.CRT`) do set VC_REDIST_DLL_PATH=%%i

echo Cleaning output directories
rmdir /s /q %DEPLOY_FOLDER%
rmdir /s /q %BUILD_FOLDER%
rmdir /s /q %INSTALLER_FOLDER%
rmdir /s /q %SYMBOLS_FOLDER%
mkdir %BUILD_ROOT%
mkdir %DEPLOY_FOLDER%
mkdir %BUILD_FOLDER%
mkdir %INSTALLER_FOLDER%
mkdir %SYMBOLS_FOLDER%

echo Configuring the project
pushd %BUILD_FOLDER%
%QMAKE_CMD% %SOURCE_ROOT%\moonlight-qt.pro
if !ERRORLEVEL! NEQ 0 goto Error
popd

echo Compiling Moonlight in %BUILD_CONFIG% configuration
pushd %BUILD_FOLDER%
%SOURCE_ROOT%\scripts\jom.exe %BUILD_CONFIG%
if !ERRORLEVEL! NEQ 0 goto Error
popd

echo Saving PDBs
for /r "%BUILD_FOLDER%" %%f in (*.pdb) do (
    copy "%%f" %SYMBOLS_FOLDER%
    if !ERRORLEVEL! NEQ 0 goto Error
)
copy %SOURCE_ROOT%\libs\windows\lib\%ARCH%\*.pdb %SYMBOLS_FOLDER%
if !ERRORLEVEL! NEQ 0 goto Error
7z a %SYMBOLS_FOLDER%\MoonlightDebuggingSymbols-%ARCH%-%VERSION%.zip %SYMBOLS_FOLDER%\*.pdb
if !ERRORLEVEL! NEQ 0 goto Error

if "%ML_SYMBOL_STORE%" NEQ "" (
    echo Publishing PDBs to symbol store: %ML_SYMBOL_STORE%
    symstore add /f %SYMBOLS_FOLDER%\*.pdb /s %ML_SYMBOL_STORE% /t Moonlight
    if !ERRORLEVEL! NEQ 0 goto Error
) else (
    if "%MUST_DEPLOY_SYMBOLS%"=="1" (
        echo "A symbol server must be specified in ML_SYMBOL_STORE for signed release builds"
        exit /b 1
    )
)

if "%ML_SYMBOL_ARCHIVE%" NEQ "" (
    echo Copying PDB ZIP to symbol archive: %ML_SYMBOL_ARCHIVE%
    copy %SYMBOLS_FOLDER%\MoonlightDebuggingSymbols-%ARCH%-%VERSION%.zip %ML_SYMBOL_ARCHIVE%
    if !ERRORLEVEL! NEQ 0 goto Error
) else (
    if "%MUST_DEPLOY_SYMBOLS%"=="1" (
        echo "A symbol archive directory must be specified in ML_SYMBOL_ARCHIVE for signed release builds"
        exit /b 1
    )
)

echo Copying DLL dependencies
copy %SOURCE_ROOT%\libs\windows\lib\%ARCH%\*.dll %DEPLOY_FOLDER%
if !ERRORLEVEL! NEQ 0 goto Error

echo Copying AntiHooking.dll
copy %BUILD_FOLDER%\AntiHooking\%BUILD_CONFIG%\AntiHooking.dll %DEPLOY_FOLDER%
if !ERRORLEVEL! NEQ 0 goto Error

echo Copying GC mapping list
copy %SOURCE_ROOT%\app\SDL_GameControllerDB\gamecontrollerdb.txt %DEPLOY_FOLDER%
if !ERRORLEVEL! NEQ 0 goto Error

if not x%QT_PATH:\5.=%==x%QT_PATH% (
    echo Copying qt.conf for Qt 5
    copy %SOURCE_ROOT%\app\qt_qt5.conf %DEPLOY_FOLDER%\qt.conf
    if !ERRORLEVEL! NEQ 0 goto Error

    rem Qt 5.15
    set WINDEPLOYQT_ARGS=--no-qmltooling --no-virtualkeyboard
) else (
    rem Qt 6.5+
    set WINDEPLOYQT_ARGS=--no-system-d3d-compiler --no-system-dxc-compiler --skip-plugin-types qmltooling,generic --no-ffmpeg
    set WINDEPLOYQT_ARGS=!WINDEPLOYQT_ARGS! --no-quickcontrols2fusion --no-quickcontrols2imagine --no-quickcontrols2universal
    set WINDEPLOYQT_ARGS=!WINDEPLOYQT_ARGS! --no-quickcontrols2fusionstyleimpl --no-quickcontrols2imaginestyleimpl --no-quickcontrols2universalstyleimpl --no-quickcontrols2windowsstyleimpl
)

echo Deploying Qt dependencies
%WINDEPLOYQT_CMD% --dir %DEPLOY_FOLDER% --%BUILD_CONFIG% --qmldir %SOURCE_ROOT%\app\gui --no-opengl-sw --no-compiler-runtime --no-sql %WINDEPLOYQT_ARGS% %BUILD_FOLDER%\app\%BUILD_CONFIG%\Moonlight.exe
if !ERRORLEVEL! NEQ 0 goto Error

echo Deleting unused styles
rem Qt 5.x directories
rmdir /s /q %DEPLOY_FOLDER%\QtQuick\Controls.2\Fusion
rmdir /s /q %DEPLOY_FOLDER%\QtQuick\Controls.2\Imagine
rmdir /s /q %DEPLOY_FOLDER%\QtQuick\Controls.2\Universal
rem Qt 6.5+ directories
rmdir /s /q %DEPLOY_FOLDER%\qml\QtQuick\Controls\Fusion
rmdir /s /q %DEPLOY_FOLDER%\qml\QtQuick\Controls\Imagine
rmdir /s /q %DEPLOY_FOLDER%\qml\QtQuick\Controls\Universal
rmdir /s /q %DEPLOY_FOLDER%\qml\QtQuick\Controls\Windows
rmdir /s /q %DEPLOY_FOLDER%\qml\QtQuick\NativeStyle

if "%SIGN%"=="1" (
    echo Signing deployed binaries
    set FILES_TO_SIGN=%BUILD_FOLDER%\app\%BUILD_CONFIG%\Moonlight.exe
    for /r "%DEPLOY_FOLDER%" %%f in (*.dll *.exe) do (
        set FILES_TO_SIGN=!FILES_TO_SIGN! %%f
    )
    signtool %SIGNTOOL_PARAMS% !FILES_TO_SIGN!
    if !ERRORLEVEL! NEQ 0 goto Error
)

if "%ML_SYMBOL_STORE%" NEQ "" (
    echo Publishing binaries to symbol store: %ML_SYMBOL_STORE%
    symstore add /r /f %DEPLOY_FOLDER%\*.* /s %ML_SYMBOL_STORE% /t Moonlight
    if !ERRORLEVEL! NEQ 0 goto Error
    symstore add /r /f %BUILD_FOLDER%\app\%BUILD_CONFIG%\Moonlight.exe /s %ML_SYMBOL_STORE% /t Moonlight
    if !ERRORLEVEL! NEQ 0 goto Error
)

echo Building MSI
cmd /c "set VERSION= && msbuild -Restore %SOURCE_ROOT%\wix\Moonlight\Moonlight.wixproj /p:Configuration=%BUILD_CONFIG% /p:Platform=%ARCH% /p:MSBuildProjectExtensionsPath=%BUILD_FOLDER%\"
if !ERRORLEVEL! NEQ 0 goto Error

echo Copying application binary to deployment directory
copy %BUILD_FOLDER%\app\%BUILD_CONFIG%\Moonlight.exe %DEPLOY_FOLDER%
if !ERRORLEVEL! NEQ 0 goto Error

echo Building portable package
rem This must be done after WiX harvesting and signing, since the VCRT dlls are MS signed
rem and should not be harvested for inclusion in the full installer
copy "%VC_REDIST_DLL_PATH%\*.dll" %DEPLOY_FOLDER%
if !ERRORLEVEL! NEQ 0 goto Error
rem This file tells Moonlight that it's a portable installation
echo. > %DEPLOY_FOLDER%\portable.dat
if !ERRORLEVEL! NEQ 0 goto Error
7z a %INSTALLER_FOLDER%\MoonlightPortable-%ARCH%-%VERSION%.zip %DEPLOY_FOLDER%\*
if !ERRORLEVEL! NEQ 0 goto Error

echo Build successful for Moonlight v%VERSION% %ARCH% binaries!
exit /b 0

:Error
echo Build failed!
exit /b !ERRORLEVEL!
