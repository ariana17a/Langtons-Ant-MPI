@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64
set "MSMPI_INC_TRIM=%MSMPI_INC:~0,-1%"
set "MSMPI_LIB64_TRIM=%MSMPI_LIB64:~0,-1%"
cl /EHsc /std:c++17 /I"%MSMPI_INC_TRIM%" main.cpp /link /LIBPATH:"%MSMPI_LIB64_TRIM%" msmpi.lib user32.lib gdi32.lib gdiplus.lib
