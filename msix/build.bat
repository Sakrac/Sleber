REM Build the Sleber MSIX package, copy the Sleber.exe from the build output to the current directory
copy /Y ..\build\msvc-x64-release\Release\Sleber.exe Sleber.exe
"%ProgramFiles(x86)%\Windows Kits\10\bin\10.0.26100.0\x64\makeappx.exe" pack /d . /p Sleber.msix
