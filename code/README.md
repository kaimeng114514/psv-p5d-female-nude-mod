# 代码说明（PSV 单 VPK 安装器）

本目录是 PSV 实机「单 VPK」方案的完整源码。

## 目录

- `installer/`：PSV 端安装器（自制，MIT）
  - `src/main.c`：主流程（挂载游戏、找 rif、加载解密模块、重建 ELF、打补丁、写 rePatch）
  - `src/p5d_patch.c`：ELF 补丁（挂载计数 2→3、重定位条目目标、3 条目 CPK 表）
  - `src/p5d_inject.c`：decrypted SELF 重建（zlib 重压缩 + ELF SHA256 + 清 NPDRM）
  - `src/zh_font.h`：中文界面 1bpp 点阵（由 `tools/gen_zh_font.ps1` 生成）
  - `src/debugScreen.h` / `debugScreenFont.c`：屏幕输出
  - `zlib/`：zlib 压缩/解压
  - `build_vita.bat`：VitaSDK 构建脚本
- `fagdec/`：解密模块源码（来自 PSVita-RE-Tools / FAGDec，GPLv3）
  - `src/kplugin/kentente.c`：SceSblAuthMgr 解密；本项目修复了：
    1) 新 SDK 头文件兼容（ksceKernelCopyToUser 等）；
    2) 解密缓冲改用多内核分区尝试（默认分区 3.1MB 段会 NO_FREE_PHYSICAL_PAGE）；
    3) `_log` 改用 vsnprintf。
  - `src/uplugin/userAllied.c`：用户层包装。
  - `build_kent.bat` / `build_stubs.bat` / `build_ua.bat`：构建与 syscall 存根生成。

## 构建

环境：VitaSDK（arm-vita-eabi-gcc 15.x）+ taiHEN 开发头/库。

1. 构建解密模块与存根：
   - 运行 `fagdec/build_kent.bat`（kentente.elf）
   - 运行 `fagdec/build_stubs.bat`（kentente.skprx + 用户存根）
   - 运行 `fagdec/build_ua.bat`（userAllied.suprx + 存根）
2. 构建安装器：
   - 准备 `assets/`（kentente.skprx、userAllied.suprx、mod.cpk）
   - 运行 `installer/build_vita.bat` → 生成 `P5DModInstaller.vpk`

> 注意：VPK 内的 SELF 必须用 **UNSAFE SELF + authid 0x2808000000000000**
> （`vita-make-fself -a 0x2808000000000000`），否则 taiHEN 会拒绝加载内核模块。

## 许可

- 安装器代码：MIT
- `fagdec/`：GPLv3（来自 PSVita-RE-Tools，见 docs/来源与许可.txt）
- zlib：zlib license
