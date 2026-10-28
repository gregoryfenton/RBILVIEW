@echo off

echo Deleting any previous error logs
del *.log
set loop=stage1
set return=
goto %loop%

:stage1
set buildname=rbilview.exe
set b1=%buildname%
set message=Building with the DOS/32A DOS extender
set dosext=dos32a
goto build

:stage1done
set buildname=cwrbilvw.exe
set b2=%buildname%
set message=Building with the CauseWay DOS extender
set dosext=causeway
goto build

:stage1donedone
rem I know, sucky name but it works so meh :P
goto goodend

:build
echo building %buildname%
del /e/q *.obj
wcl386 -bt=dos -l=%dosext% -fe=%buildname% rbilview.c
if errorlevel 1 goto error
echo.
echo ========================================
echo             Build successful
set return=success
del /e/q *.obj
set loop=%loop%done
goto %loop%

:error
echo Build failed, see the error log for details
dir *.err /kmb
goto end

:goodend
dir *.exe
echo You can now use one of these executables to view the
echo Ralf Brown Intlist database.
if exist *.rbd goto rbdexists
echo you did build or download the database, right?
echo github.com/gregoryfenton or build and run rbilbld
goto continue

:dbexists
echo Usage:
echo %b1 [rbil.rbd]
echo ÿÿor
echo %b2 [rbil.rbd]
dir *.rbd
goto continue

:continue
set buildname=
set loop=
set message=
set dosext=
set b1=
set b2=
goto end

:end


