@echo off

rem Formats a 2HD disk as 1.23 MB (77 cylinders, 2 heads, 8 sectors of 1024 bytes) and writes a floppy image to it
rem Usage: tools\write_floppy.cmd <image.img> [drive, A: when left out]
rem format2hd.exe must be on the PATH; it asks before it formats

setlocal

if "%~1"=="" (
    echo usage: %~nx0 ^<image.img^> [drive]
    exit /b 1
)

if not exist "%~1" (
    echo error: %~1 not found
    exit /b 1
)

if not "%~z1"=="1261568" (
    echo error: %~1 is not a 1.23 MB floppy image ^(1261568 bytes^)
    exit /b 1
)

set "IMAGE=%~f1"

if not "%IMAGE: =%"=="%IMAGE%" (
    echo error: the image path has spaces, move it somewhere without them
    exit /b 1
)

where format2hd.exe >nul 2>&1 || (
    echo error: format2hd.exe is not on the PATH
    exit /b 1
)

set "DRIVE=%~2"
if "%DRIVE%"=="" set "DRIVE=A:"
if "%DRIVE:~1%"=="" set "DRIVE=%DRIVE%:"

format2hd.exe %DRIVE% /F:1.23 /I:%IMAGE% /VERIFY
exit /b %ERRORLEVEL%
