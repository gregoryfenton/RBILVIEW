@echo off
if exist RBIL.RBD goto skipgenerate
if exist CWRBILBL.EXE goto timetoplayCW
if exist RBILBLD.EXE goto timetoplay

REM If we are here, there is no executable and no RBIL.RBD
REM we need to create both to continue.

if not exist MAKEBLD.BAT goto nomakebld
call MAKEBLD.BAT
if not exist GENERATE.BAT goto nogenerate
call GENERATE.BAT

:skipgenerate
echo RBIL.RBD found
if exist CWRBILVW.EXE goto timetoplayCW
if exist RBILVIEW.EXE goto timetoplay

REM If we are here, we need to build the viewer
if exist MAKEVIEW.BAT call MAKEVIEW.BAT
if exist CWRBILVW.EXE goto timetoplayCW
if exist RBILVIEW.EXE goto timetoplay
goto noviewer

:nomakebld
echo Critical build files are missing, please visit my github
echo missing: makebld.bat
echo github.com/gregoryfenton
if not exist GENERATE.BAT goto nogenerate
goto end

:nogenerate
echo Critical build files are missing, please visit my github
echo missing: generate.bat
echo github.com/gregoryfenton
goto end

:noviewer
echo The viewer is missing, please visit
echo github.com/gregoryfenton

:timetoplay
RBILVIEW RBIL.RBD
goto endgood

:timetoplayCW
CWRBILVW RBIL.RBD
goto endgood

:endgood
echo Ralf Brown's Intlist took me beyond simple
echo 10 PRINT "Greg ";
echo 20 GOTO 10
echo to truly finding out what a powerful machine my 286 actually was.
echo for that I will be forever grateful.
goto end

:end

