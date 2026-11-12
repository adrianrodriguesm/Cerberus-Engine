@echo off
setlocal
rem Run in a VS 2022 x64 Native Tools prompt. Optional argument is a checkout
rem with an already built Release yaml-cpp library; it is used read-only.
set "stressRepo=%~dp0..\.."
set "stressDeps=%~1"
if "%stressDeps%"=="" set "stressDeps=%stressRepo%"
if not exist "%stressRepo%\intermediate\stress-validation" mkdir "%stressRepo%\intermediate\stress-validation"
cl /nologo /std:c++20 /EHsc /MD /I "%stressDeps%\Cerberus\vendor\yaml-cpp\include" "%~dp0validate_yaml.cpp" "%stressDeps%\Cerberus\vendor\yaml-cpp\bin\Release-windows-x86_64\yaml-cpp\yaml-cpp.lib" /Fe:"%stressRepo%\intermediate\stress-validation\validate_yaml.exe" /Fo:"%stressRepo%\intermediate\stress-validation\validate_yaml.obj"
exit /b %errorlevel%
