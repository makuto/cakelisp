echo off
rem Set environment variables. The user may need to adjust this path
rem See https://docs.microsoft.com/en-us/cpp/build/building-on-the-command-line?view=msvc-160#developer_command_file_locations
rem See https://docs.microsoft.com/en-us/cpp/build/building-on-the-command-line?view=msvc-160#developer_command_file_locations
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars64.bat" (
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars64.bat"
) else (
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
  call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
  ) else (
  echo This script builds using MSVC.
  echo You must download and install MSVC before it will work. Download it here:
  echo https://visualstudio.microsoft.com/downloads/
  echo Select workloads for C++ projects. Ensure you install the C++ developer tools.
  echo If you're still seeing this, you may need to edit Build.bat to your vcvars path
  echo Please see the following link:
  echo https://docs.microsoft.com/en-us/cpp/build/building-on-the-command-line?view=msvc-160
  goto fail
))

if not exist "bin" (
mkdir bin
)

if not exist "bin\cakelisp_bootstrap_v2.exe" (
  goto manualBuild
) else (
  goto bootstrapBuild
)

:manualBuild
CL.exe src/Tokenizer.cpp ^
 src/Evaluator.cpp ^
 src/Utilities.cpp ^
 src/FileUtilities.cpp ^
 src/Converters.cpp ^
 src/Writer.cpp ^
 src/Generators.cpp ^
 src/GeneratorHelpers.cpp ^
 src/RunProcess.cpp ^
 src/OutputPreambles.cpp ^
 src/DynamicLoader.cpp ^
 src/ModuleManager.cpp ^
 src/Logging.cpp ^
 src/Build.cpp ^
 src/Metadata.cpp ^
 src/Main.cpp ^
 3rdPartySrc/FindVisualStudio.cpp ^
 Advapi32.lib Ole32.lib OleAut32.lib ^
 /I 3rdPartySrc ^
 /EHsc /MP /DWINDOWS /DCAKELISP_EXPORTING ^
 /Fe"bin\cakelisp_bootstrap_v2" /Zi /Fd"bin\cakelisp_bootstrap_v2.pdb" /DEBUG:FASTLINK
 echo %ERRORLEVEL%

@if %ERRORLEVEL% == 0 (
  echo Success building
  rem Clean up working directory
  del *.obj
  goto bootstrapBuild
) else (
  echo Error while building
  goto fail
)

:bootstrapBuild
"bin\cakelisp_bootstrap_v2.exe" Bootstrap_MSVC.cake
@if %ERRORLEVEL% == 0 (
  echo Success! Use bin\cakelisp.exe to build your programs
  goto build_user
) else (
  echo Error while bootstrapping cakelisp
  goto fail
)

:build_user
"bin\cakelisp.exe" --execute runtime/Config_Windows.cake test/RunTests.cake
@if %ERRORLEVEL% == 0 (
  echo Success!
  goto success
) else (
  echo Error while building user program
  goto fail
)

:fail
goto end

:success
goto end

:end
echo Done
