:: This batch file expects "/p:Configuration=<Debug/Release/RelWithDebInfo/MinSizeRel>" as the first parameter.
:: If the parameter is not provided, the solution file seems to default to Debug build.
:: Due to the "=" in the parameter, the actual build type string is nicely available using %2.

@echo off
call RunCMake.cmd

cd ..\..
set CALL_CECHO=..\tools\Windows\Utils\cecho
%CALL_CECHO% {0D}Building %2 Tundra.{# #}{\n}
MSBuild tundra.sln %*
IF NOT %ERRORLEVEL%==0 GOTO :Error
echo.

%CALL_CECHO% {0A}%2 Tundra build finished.{# #}{\n}
goto :End

:Error
echo.
%CALL_CECHO% {0C}%2 Tundra build failed!{# #}{\n}

:End
:: Finish in same directory we started in.
cd tools\Windows
