@echo off

set PATH=%XEDK%\bin\win32;%PATH%;
set INCLUDE=%XEDK%\include\win32;%XEDK%\include\xbox;%XEDK%\include\xbox\sys;%INCLUDE%
set LIB=%XEDK%\lib\win32;%XEDK%\lib\xbox;%LIB%
set _NT_SYMBOL_PATH=SRV*%XEDK%\bin\xbox\symsrv;%_NT_SYMBOL_PATH%

echo.
echo Setting environment for using Microsoft Xbox 360 SDK tools.
echo.

echo Compile libwiigui shader
fxc /Fh vs.h /Tvs_3_0 libwiigui.hlsl /EVSmain
fxc /Fh ps.c.h /Tps_3_0 libwiigui.hlsl /EpsC
fxc /Fh ps.t.h /Tps_3_0 libwiigui.hlsl /EpsT
fxc /Fh ps.tc.h /Tps_3_0 libwiigui.hlsl /EpsTC
cmd
