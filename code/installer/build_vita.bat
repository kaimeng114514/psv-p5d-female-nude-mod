@echo off
setlocal
set "VITASDK=C:\dev\vitasdk"
set "SRC=C:\dev\p5d-vita-installer\src"
set "FAG=C:\dev\p5d-vita-installer\fagdec"
set "OUT=C:\dev\p5d-vita-installer\build"
set "PATH=%VITASDK%\bin;%PATH%"
cd /d "%SRC%"
arm-vita-eabi-gcc -O2 -std=gnu11 -Wall -I . -I "%FAG%\include" -I "%VITASDK%\arm-vita-eabi\include" ^
  main.c p5d_patch.c p5d_inject.c sha256.c debugScreenFont.c ^
  zlib/adler32.c zlib/compress.c zlib/crc32.c zlib/deflate.c zlib/trees.c zlib/zutil.c ^
  zlib/inflate.c zlib/inftrees.c zlib/inffast.c zlib/uncompr.c ^
  -Wl,-q -o "%OUT%\p5d_installer.elf" ^
  -L "%FAG%\build\uastubs" -luserAlliedLib_stub_weak ^
  -L "%VITASDK%\arm-vita-eabi\lib" -lSceDisplay_stub -lSceCtrl_stub -lSceIofilemgr_stub -lSceKernelThreadMgr_stub -lSceAppMgr_stub -lSceNpDrm_stub -ltaihen_stub
if errorlevel 1 goto fail

vita-elf-create "%OUT%\p5d_installer.elf" "%OUT%\p5d_installer.velf"
if errorlevel 1 goto fail
vita-make-fself -a 0x2808000000000000 "%OUT%\p5d_installer.velf" "%OUT%\eboot.bin"
if errorlevel 1 goto fail
vita-mksfoex -s TITLE_ID=P5DINST01 -d ATTRIBUTE2=12 "P5D Mod Installer" "%OUT%\param.sfo"
if errorlevel 1 goto fail
vita-pack-vpk -s "%OUT%\param.sfo" -b "%OUT%\eboot.bin" ^
  -a "%FAG%\..\assets\kentente.skprx=assets/kentente.skprx" ^
  -a "%FAG%\..\assets\userAllied.suprx=assets/userAllied.suprx" ^
  -a "%FAG%\..\assets\mod.cpk=assets/mod.cpk" ^
  "%OUT%\P5DModInstaller.vpk"
if errorlevel 1 goto fail
echo OK
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
