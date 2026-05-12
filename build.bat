@echo off
setlocal EnableExtensions

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"
set "APP_DIR=%ROOT_DIR%\app"
set "VCPKG_DIR=%ROOT_DIR%\vcpkg"

if /I "%~1"=="--help" goto show_help
if /I "%~1"=="-h" goto show_help
if /I "%~1"=="/?" goto show_help
if "%~1"=="" goto show_help

set "COMMAND=%~1"
shift

if /I "%COMMAND%"=="help" goto show_help
if /I "%COMMAND%"=="setup" goto cmd_setup
if /I "%COMMAND%"=="native" goto cmd_native
if /I "%COMMAND%"=="app" goto cmd_app
if /I "%COMMAND%"=="bundle-win" goto cmd_bundle_win
if /I "%COMMAND%"=="bundle" goto cmd_bundle_win
if /I "%COMMAND%"=="dev" goto cmd_dev
if /I "%COMMAND%"=="test" goto cmd_test

echo [build.bat] Unknown command: %COMMAND%
echo.
goto show_help_error

:cmd_setup
call :setup_env
if errorlevel 1 exit /b %ERRORLEVEL%
setlocal EnableExtensions

call :ensure_vcpkg
if errorlevel 1 exit /b %ERRORLEVEL%

call :install_vcpkg_deps
if errorlevel 1 exit /b %ERRORLEVEL%

if not exist "%APP_DIR%\package.json" (
  echo [build.bat] Error: app directory is missing or invalid: "%APP_DIR%"
  exit /b 1
)
pushd "%APP_DIR%"
npm install
set "RUN_EXIT=%ERRORLEVEL%"
popd
exit /b %RUN_EXIT%

:cmd_native
call :setup_env
if errorlevel 1 exit /b %ERRORLEVEL%
setlocal EnableExtensions

if not exist "%APP_DIR%\package.json" (
  echo [build.bat] Error: app directory is missing or invalid: "%APP_DIR%"
  exit /b 1
)
pushd "%APP_DIR%"
npm run build:native
set "RUN_EXIT=%ERRORLEVEL%"
popd
exit /b %RUN_EXIT%

:cmd_app
if not exist "%APP_DIR%\package.json" (
  echo [build.bat] Error: app directory is missing or invalid: "%APP_DIR%"
  exit /b 1
)
pushd "%APP_DIR%"
if not exist "%APP_DIR%\node_modules" (
  npm install
  if errorlevel 1 (
    set "RUN_EXIT=%ERRORLEVEL%"
    popd
    exit /b %RUN_EXIT%
  )
)
npm run build:main
if errorlevel 1 (
  set "RUN_EXIT=%ERRORLEVEL%"
  popd
  exit /b %RUN_EXIT%
)
npm run build:renderer
set "RUN_EXIT=%ERRORLEVEL%"
popd
exit /b %RUN_EXIT%

:cmd_bundle_win
call :setup_env
if errorlevel 1 exit /b %ERRORLEVEL%
setlocal EnableExtensions

if not exist "%APP_DIR%\package.json" (
  echo [build.bat] Error: app directory is missing or invalid: "%APP_DIR%"
  exit /b 1
)
pushd "%APP_DIR%"
if not exist "%APP_DIR%\node_modules" (
  npm install
  if errorlevel 1 (
    set "RUN_EXIT=%ERRORLEVEL%"
    popd
    exit /b %RUN_EXIT%
  )
)
npm run bundle:win
set "RUN_EXIT=%ERRORLEVEL%"
popd
exit /b %RUN_EXIT%

:cmd_dev
call :setup_env
if errorlevel 1 exit /b %ERRORLEVEL%
setlocal EnableExtensions

if not exist "%APP_DIR%\package.json" (
  echo [build.bat] Error: app directory is missing or invalid: "%APP_DIR%"
  exit /b 1
)
pushd "%APP_DIR%"
npm run dev
set "RUN_EXIT=%ERRORLEVEL%"
popd
exit /b %RUN_EXIT%

:cmd_test
call :setup_env
if errorlevel 1 exit /b %ERRORLEVEL%
setlocal EnableExtensions

if not exist "%APP_DIR%\package.json" (
  echo [build.bat] Error: app directory is missing or invalid: "%APP_DIR%"
  exit /b 1
)
pushd "%APP_DIR%"
npm test
set "RUN_EXIT=%ERRORLEVEL%"
popd
exit /b %RUN_EXIT%

:resolve_triplet
set "ARCH=%PROCESSOR_ARCHITECTURE%"
if /I "%PROCESSOR_ARCHITEW6432%"=="ARM64" set "ARCH=ARM64"
if /I "%ARCH%"=="ARM64" (
  set "VCPKG_TRIPLET=arm64-windows-static-md"
) else (
  set "VCPKG_TRIPLET=x64-windows-static-md"
)
exit /b 0

:ensure_vcpkg
if not exist "%VCPKG_DIR%\.git" (
  echo [build.bat] Cloning vcpkg into "%VCPKG_DIR%"...
  git clone https://github.com/microsoft/vcpkg.git "%VCPKG_DIR%"
  if errorlevel 1 exit /b %ERRORLEVEL%
) else (
  echo [build.bat] vcpkg exists; updating...
  git -C "%VCPKG_DIR%" pull --ff-only >nul 2>nul
)

if not exist "%VCPKG_DIR%\vcpkg.exe" (
  echo [build.bat] Bootstrapping vcpkg...
  call "%VCPKG_DIR%\bootstrap-vcpkg.bat" -disableMetrics
  if errorlevel 1 exit /b %ERRORLEVEL%
)
exit /b 0

:install_vcpkg_deps
call :resolve_triplet
if errorlevel 1 exit /b %ERRORLEVEL%

set "OVERLAY_ARG="
if exist "%ROOT_DIR%\triplets\%VCPKG_TRIPLET%.cmake" (
  set "OVERLAY_ARG=--overlay-triplets=%ROOT_DIR%\triplets"
)

echo [build.bat] Installing vcpkg dependencies (triplet: %VCPKG_TRIPLET%)...
"%VCPKG_DIR%\vcpkg.exe" install --triplet="%VCPKG_TRIPLET%" --x-manifest-root="%ROOT_DIR%" --x-install-root="%ROOT_DIR%\vcpkg_installed" %OVERLAY_ARG%
if errorlevel 1 exit /b %ERRORLEVEL%
exit /b 0

:setup_env
if defined TIGHTROPE_VS_READY exit /b 0

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" goto setup_try_vswhere
goto setup_try_scan

:setup_try_vswhere
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -version [17.0,18.0^) -property installationPath`) do set "VS_INSTALL=%%i"
if not defined VS_INSTALL (
  for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -version [18.0,19.0^) -property installationPath`) do set "VS_INSTALL=%%i"
)
if not defined VS_INSTALL goto setup_try_scan
set "VSCONFIG=%VS_INSTALL%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VSCONFIG%" goto setup_try_scan
goto setup_finish_env

:setup_try_scan
set "VS_INSTALL="
set "VSCONFIG="
set "VSBASE=%ProgramFiles%\Microsoft Visual Studio"
if not exist "%VSBASE%" goto setup_error_not_found

if exist "%VSBASE%\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
  set "VSCONFIG=%VSBASE%\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "%VSBASE%\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
  set "VSCONFIG=%VSBASE%\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "%VSBASE%\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
  set "VSCONFIG=%VSBASE%\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "%VSBASE%\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" (
  set "VSCONFIG=%VSBASE%\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat"
)

if not defined VSCONFIG (
  for /f "usebackq delims=" %%i in (`dir /b /s "%VSBASE%\\*\\VC\\Auxiliary\\Build\\vcvarsall.bat" 2^>nul`) do (
    echo %%i | findstr /i "\\Professional\\" >nul && set "VSCONFIG=%%i"
    if not defined VSCONFIG set "VSCONFIG=%%i"
  )
)

if not defined VSCONFIG goto setup_error_not_found
for %%i in ("%VSCONFIG%\..\..\..\..") do set "VS_INSTALL=%%~fi"
goto setup_finish_env

:setup_finish_env
echo [build.bat] Using Visual Studio: %VS_INSTALL%
rem Prevent MSYS2/MinGW compiler paths from taking precedence over Visual Studio and vcpkg.
set "INCLUDE="
set "LIB="
set "LIBPATH="
set "CPATH="
set "C_INCLUDE_PATH="
set "CPLUS_INCLUDE_PATH="
set "LIBRARY_PATH="
set "CMAKE_INCLUDE_PATH="
set "CMAKE_LIBRARY_PATH="
set "CMAKE_PREFIX_PATH="
set "PKG_CONFIG_PATH="
call "%VSCONFIG%" x64
if errorlevel 1 goto setup_error_vcvars

set "VCPKG_VISUAL_STUDIO_PATH=%VS_INSTALL%"
if exist "%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" (
  set "PATH=%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
)
if exist "C:\ninja\ninja.exe" (
  set "PATH=C:\ninja;%PATH%"
)

set "TIGHTROPE_VS_READY=1"
exit /b 0

:setup_error_not_found
echo [build.bat] Error: Could not locate a Visual Studio installation with C++ tools.
echo [build.bat] Install Visual Studio 2022 or VS 18 Insiders with the "Desktop development with C++" workload.
exit /b 1

:setup_error_vcvars
echo [build.bat] Error: Failed to initialize the Visual Studio developer environment.
exit /b 1

:show_help
echo tightrope build commands
echo.
echo Usage: build.bat ^<command^>
echo.
echo Commands:
echo   setup    Clone/bootstrap vcpkg, install C++ deps, install app npm deps
echo   native   Build tightrope-core.node
echo   app      Build Electron TypeScript main + renderer bundles
echo   bundle   Build Windows desktop bundle + NSIS installer
echo   dev      Start Electron dev mode ^(ensures native module freshness^)
echo   test     Run app tests
echo.
echo Examples:
echo   build.bat setup
echo   build.bat native
echo   build.bat bundle
echo   build.bat dev
exit /b 0

:show_help_error
echo Usage: build.bat ^<command^>
exit /b 1
