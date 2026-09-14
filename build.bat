@echo off
gcc -o edid.exe main.c edid_parser.c -Wall
if %errorlevel% equ 0 (
    echo Build OK: edid.exe
) else (
    echo Build FAILED
    exit /b 1
)
