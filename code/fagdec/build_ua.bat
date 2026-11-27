@echo off
setlocal
set "VITASDK=C:\dev\vitasdk"
set "ROOT=C:\dev\p5d-vita-installer\fagdec"
set "INC=%ROOT%\include"
set "SRC=%ROOT%\src\uplugin"
set "OUT=%ROOT%\build"
set "PATH=%VITASDK%\bin;%PATH%"
cd /d "%SRC%"
arm-vita-eabi-gcc -c -O2 -std=gnu99 -fno-builtin -Wl,-q -Wall -I "%INC%" -I "%VITASDK%\arm-vita-eabi\include" -o "%OUT%\userAllied.o" userAllied.c
if errorlevel 1 goto fail
echo COMPILE_OK
arm-vita-eabi-gcc -nostdlib -Wl,-q -o "%OUT%\userAllied.elf" "%OUT%\userAllied.o" -L "%OUT%\stubs" -L "%VITASDK%\arm-vita-eabi\lib" -lkentente_stub -lSceLibKernel_stub -lgcc
if errorlevel 1 goto fail
echo LINK_OK
vita-elf-create -e exports.yml "%OUT%\userAllied.elf" "%OUT%\userAllied.velf"
if errorlevel 1 goto fail
vita-make-fself "%OUT%\userAllied.velf" "%OUT%\userAllied.suprx"
if errorlevel 1 goto fail
echo SELF_OK
vita-elf-export user "%OUT%\userAllied.elf" exports.yml "%OUT%\userAllied_exports.yml"
if errorlevel 1 goto fail
vita-libs-gen "%OUT%\userAllied_exports.yml" "%OUT%\uastubs"
if errorlevel 1 goto fail
echo GEN_OK
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
