@echo off
setlocal
set "VITASDK=C:\dev\vitasdk"
set "SRC=C:\dev\p5d-vita-installer\fagdec\src\kplugin"
set "OUT=C:\dev\p5d-vita-installer\fagdec\build"
set "PATH=%VITASDK%\bin;%PATH%"
cd /d "%SRC%"
arm-vita-eabi-gcc -c -O3 -std=gnu99 -fno-inline -fno-builtin -Wno-restrict -Wall -I "%VITASDK%\arm-vita-eabi\include" -I . -o "%OUT%\kentente.o" kentente.c
if errorlevel 1 goto fail
echo COMPILE_OK
arm-vita-eabi-gcc -nostdlib -Wl,-q -o "%OUT%\kentente.elf" "%OUT%\kentente.o" -L "%VITASDK%\arm-vita-eabi\lib" -lSceSysclibForDriver_stub -lSceDebugForDriver_stub -lSceSysmemForDriver_stub -lSceSysmemForKernel_stub -lSceNpDrmForDriver_stub -lSceIofilemgrForDriver_stub -lSceThreadmgrForDriver_stub -lSceSblAuthMgrForKernel_stub -lSceSblSmCommForKernel_stub -ltaihenModuleUtils_stub -lgcc
if errorlevel 1 goto fail
echo LINK_OK
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
