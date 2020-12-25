rem Set environment variables. The user may need to adjust this path
rem See https://docs.microsoft.com/en-us/cpp/build/building-on-the-command-line?view=msvc-160#developer_command_file_locations
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars64.bat"

if not exist "bin" (
mkdir bin
)

if not exist "bin\cakelisp_bootstrap.exe" (
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
 src/Main.cpp ^
 /EHsc /DWINDOWS /Fe"bin\cakelisp_bootstrap"

  @if ERRORLEVEL == 0 (
     "bin\cakelisp_bootstrap.exe"
  ) else (
    echo "Error while building"
    goto :fail
  )
) else (
  "bin\cakelisp_bootstrap.exe" --verbose-processes Bootstrap_Windows.cake
  rem Left off: Remove space between /Fo and output, -Isrc isn't going to work either
  rem Figure out why CL isn't being found by CreateProcess
  rem CL /c src/Tokenizer.cpp /Fo"cakelisp_cache/Bootstrap_Windows/Tokenizer.cpp.o" -Isrc /DWINDOWS /EHsc
)

:fail
pause

:end
