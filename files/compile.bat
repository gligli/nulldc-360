@echo off

del *.bin

rshadercompiler vs.hlsl vs.bin /vs

rshadercompiler ps.hlsl ps.bin /ps

del ..\build\bininc.o

rem pause