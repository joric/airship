@echo off

::call "C:\Program Files (x86)\Microsoft Visual Studio 8\Common7\Tools\vsvars32.bat"
call "C:\Program Files (x86)\Microsoft Visual Studio 9.0\Common7\Tools\vsvars32.bat"
::call "C:\Program Files (x86)\Microsoft Visual Studio 10.0\Common7\Tools\vsvars32.bat"

cl airship.c /W3 /Zl /Oi /Ox /Os /Oy /Gy kernel32.lib shell32.lib gdi32.lib user32.lib advapi32.lib /link /entry:WinMain

::3362 bytes after mew11:
::mew11 airship.exe 2>nul

::2441 bytes after crinkler:
::crinkler airship.obj kernel32.lib shell32.lib gdi32.lib user32.lib advapi32.lib /ENTRY:WinMain /SUBSYSTEM:WINDOWS /COMPMODE:SLOW /TRUNCATEFLOATS /HASHSIZE:1000 /HASHTRIES:1000 /ORDERTRIES:1000 /PRINT:IMPORTS /PRINT:LABELS /TRANSFORM:CALLS
