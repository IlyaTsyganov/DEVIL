@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>&1
if not errorlevel 1 goto compiler_ready
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio Installer was not found. Install Visual Studio with the C++ desktop workload.
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -find VC\Auxiliary\Build\vcvars64.bat`) do set "VCVARS=%%i"
if not defined VCVARS (
  echo Visual Studio C++ build tools were not found.
  exit /b 1
)
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1
:compiler_ready
set "VULKAN_HEADERS=..\..\third_party\Vulkan-Headers\include"
if not exist "%VULKAN_HEADERS%\vulkan\vulkan.h" (
  echo Vulkan-Headers must be available in the repository at third_party\Vulkan-Headers.
  exit /b 1
)
if not exist build mkdir build
if not exist bin mkdir bin
cl /nologo /std:c++17 /W4 /O2 /EHsc /MD /LD /I"%VULKAN_HEADERS%" src\vulkan_layer.cpp /Fo:build\vulkan_layer.obj /Fe:bin\DoomCollisionLayer.dll user32.lib /link /INCREMENTAL:NO
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /W4 /O2 /EHsc /MD /I"%VULKAN_HEADERS%" tests\viewer_behavior_tests.cpp /Fo:build\viewer_behavior_tests.obj /Fe:bin\ViewerBehaviorTests.exe user32.lib /link /INCREMENTAL:NO
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /W4 /O2 /EHsc /MD /I"%VULKAN_HEADERS%" tests\depth_tracker_tests.cpp /Fo:build\depth_tracker_tests.obj /Fe:bin\DepthTrackerTests.exe user32.lib /link /INCREMENTAL:NO
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /W4 /O2 /EHsc /MD /I"%VULKAN_HEADERS%" tests\layer_loader_probe.cpp /Fo:build\layer_loader_probe.obj /Fe:bin\LayerLoaderProbe.exe /link /INCREMENTAL:NO
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /W4 /O2 /EHsc /MD /I"%VULKAN_HEADERS%" tests\retail_profile_probe.cpp /Fo:build\retail_profile_probe.obj /Fe:bin\RetailProfileProbe.exe user32.lib /link /INCREMENTAL:NO
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /W4 /O2 /EHsc /MD /I"%VULKAN_HEADERS%" tests\vulkan_smoke.cpp /Fo:build\vulkan_smoke.obj /Fe:bin\VulkanSmoke.exe user32.lib /link /INCREMENTAL:NO
if errorlevel 1 exit /b %errorlevel%
exit /b %errorlevel%
