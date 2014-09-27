:: Assumes that Cppcheck is installed in the default location or accessible via PATH by default.
:: Disable following warnings:
:: - 'unusedFunction' because as an SDK not all of the functions are not used by the library itself.
:: - 'preprocessorErrorDirective' because MOC files cause plethora of these.
:: - 'missingInclude' until deps directory is configured to be included.
::   Probably not ever gonna happen though due to the massive amount of dependencies.

@echo off

set ORIG_PATH=%PATH%
set PATH=%PATH%;C:\Program Files (x86)\Cppcheck

call cppcheck --version
IF ERRORLEVEL 1 GOTO NoCppcheck
:: TODO Only scan TundraCore; the whole project is way too much for Cppcheck to handle and the
:: 32-bit process runs out of memory and crashes for Core alone very quickly.
:: TODO If moc files would be ignored would it help?
set SRC_DIR=../../../src/Core/TundraCore
call cppcheck --template "{file}({line}): ({severity}) ({id}): {message}" -DTUNDRACORE_API= -I%SRC_DIR% ^
    -rp=%SRC_DIR% --enable=all --suppress=unusedFunction --suppress=preprocessorErrorDirective ^
    --suppress=missingInclude %SRC_DIR%

GOTO End

:NoCppcheck
echo Could not find the cppcheck executable! Please add it to system PATH and try again!

:End
set PATH=%ORIG_PATH%

pause
