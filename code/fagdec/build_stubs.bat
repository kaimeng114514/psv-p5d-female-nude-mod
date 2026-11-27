@echo off
setlocal
set "VITASDK=C:\dev\vitasdk"
set "SRC=C:\dev\p5d-vita-installer\fagdec\src\kplugin"
set "OUT=C:\dev\p5d-vita-installer\fagdec\build"
set "PATH=%VITASDK%\bin;%PATH%"
cd /d "%SRC%"
vita-elf-create -e exports.yml "%OUT%\kentente.elf" "%OUT%\kentente.velf"
if errorlevel 1 goto fail
vita-make-fself "%OUT%\kentente.velf" "%OUT%\kentente.skprx"
if errorlevel 1 goto fail
echo SELF_OK
vita-elf-export kernel "%OUT%\kentente.elf" exports.yml "%OUT%\kentente_exports.yml"
if errorlevel 1 goto fail
echo EXPORT_OK
vita-libs-gen "%OUT%\kentente_exports.yml" "%OUT%\stubs"
if errorlevel 1 goto fail
make -C "%OUT%\stubs"
if errorlevel 1 goto fail
echo STUBS_OK
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
