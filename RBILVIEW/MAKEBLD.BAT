@echo off
set buildname=rbilbld.exe
echo building %buildname%
del *.obj /e/q
wcl386 -cc -bt=dos -l=dos32a rbilbld.c int2qh2i.c rbilpk-i.c ziprdlib.c -fe=%buildname%
del *.obj /e/q
wcl386 -cc -bt=dos -l=causeway rbilbld.c int2qh2i.c rbilpk-i.c ziprdlib.c -fe=CW%buildname%
if errorlevel 1 goto error
echo.
echo ========================================
echo             Build successful
del /e/q *.obj
dir *.exe /km
goto end

:error
echo Build failed, see the error log for details
dir *.err /kmb

:end
set buildname=

