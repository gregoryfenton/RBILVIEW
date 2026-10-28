@echo off
if "%1" == "" set intdir=../intlist
if not "%1" == "" set intdir=%1

set zipfiles=inter61
set okay=true
set missing=

for %%n in (a b c d e f) do if not exist %intdir%\%zipfiles%%%n.zip goto f%%n

:ca
if not exist %intdir%\inter61a.zip goto fa
:cb
if not exist %intdir%\inter61b.zip goto fb
:cc
if not exist %intdir%\inter61c.zip goto fc
:cd
if not exist %intdir%\inter61d.zip goto fd
:ce
if not exist %intdir%\inter61e.zip goto fe
:cf
if not exist %intdir%\inter61f.zip goto ff

:built
set rbil=
if exist rbilbld.exe set rbil=rbilbld.exe
if exist cwrbilbl.exe set rbil=cwrbilbl.exe
if "%rbil%" == "" goto noapp

echo Converting the intlist files to RBILVIEW format.
echo This may take a moment.

%rbil% rbil.rbd %intdir%

goto cleanup

:fa
set missing=%intdir%\%zipfiles%a.zipÿ
echo file %intdir%\%zipfiles%a.zip not found
set okay=false
goto cb
:fb
set missing=%missing%%intdir%\%zipfiles%b.zipÿ
echo file %intdir%\%zipfiles%b.zip not found
set okay=false
goto cc
:fc
set missing=%missing%%intdir%\%zipfiles%c.zipÿ
echo file %intdir%\%zipfiles%c.zip not found
set okay=false
goto cd
:fd
set missing=%missing%%intdir%\%zipfiles%d.zipÿ
echo file %intdir%\%zipfiles%d.zip not found
set okay=false
goto ce
:fe
set missing=%missing%%intdir%\%zipfiles%e.zipÿ
echo file %intdir%\%zipfiles%e.zip not found
set okay=false
goto cf
:ff
set missing=%missing%%intdir%\%zipfiles%f.zipÿ
echo file %intdir%\%zipfiles%f.zip not found
set okay=false
goto end

:cleanup
echo Removing intermediate files
del /q 86bugs.* interrup.* ports.? *.?st *.qh

:noapp
set return=
echo neither rbilbld.exe nor cwrblibl.exe were found.
echo attempting to build them.
call MAKEBLD.BAT
if "%return%" == "success" goto built
echo I am unable to continue.
goto end

:notokay
echo One or more required files are missing:
echo %missing%
echo find them by searching for inter61a.zip online
echo or mirrored in my github repository https://github.com/gregoryfenton
set okay=

:end
if "%okay%" == "false" goto notokay

set zipfiles=
set okay=
