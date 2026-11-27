# P5D 模组安装说明（单 VPK 中文版 · 全自动）

> 适用：PSV（3.60–3.68 + HENkaku/taiHEN + rePatch 插件）
> 游戏：Persona 5: Dancing in Starlight 亚洲版 PCSH10109 版本 1.00
> 特点：**只需安装一个 VPK**（应用名「P5D 模组安装器」，中文界面）；
>       解密、打补丁、注入、复制模组全部在 PSV 上自动完成，不需要电脑。

## 文件夹说明

| 位置 | 内容 |
|---|---|
| `工具`文件夹 | `P5DModInstaller.vpk`：安装器（装到 PSV 上运行） |
| `模组文件`文件夹 | `mod.cpk`：模组数据本体（更换 MOD 时使用） |

---

## 一、前置条件（确认一次）

1. PSV 已破解（HENkaku/taiHEN）
2. **rePatch 插件已启用**：`ur0:tai/config.txt` 的 `*KERNEL` 段里有 `ur0:tai/repatch.skprx`
3. 游戏 **PCSH10109（P5D 亚洲版 1.00）** 已安装

---

## 二、安装（一次性）

1. 用 VitaShell 安装 **`工具\P5DModInstaller.vpk`**
   - 桌面会显示中文名称 **「P5D 模组安装器」**
   - （若仍显示旧名称，重启一次刷新 LiveArea）
2. 运行「P5D 模组安装器」，界面为中文，依次显示：
   - 正在准备文件…
   - 正在加载解密模块…
   - 正在解密游戏启动文件，请稍候…（约几十秒）
   - 正在重建补丁并写入 rePatch…
   - 安装完成！请启动游戏。
3. 启动游戏 → 进入换装界面选择泳装 → 模型已替换

> 安装器会自动完成：读取游戏 eboot → 解密 → 打 3-CPK 挂载补丁 →
> 重建 eboot.bin 写入 `ux0:rePatch/PCSH10109/eboot.bin` →
> 把内置 mod.cpk 复制到 `ux0:rePatch/PCSH10109/data/mod.cpk`。

---

## 三、更换 MOD / 还原

- **更换 MOD**：把 `模组文件\mod.cpk`（或新的 mod.cpk）覆盖到
  `ux0:rePatch/PCSH10109/data/mod.cpk` 即可（不需要再运行安装器）
- **还原原版**：删除 `ux0:rePatch/PCSH10109/eboot.bin` 和
  `ux0:rePatch/PCSH10109/data/mod.cpk`
- **重装/更新游戏后**：再运行一次安装器

---

## 四、排错

| 现象 | 处理 |
|---|---|
| 提示"安装失败！请查看日志" | 查看 `ux0:data/P5DMod/p5d_installer_log.txt` |
| 解密很慢或失败 | 重启 PSV 后再运行一次（同一开机周期模块可能已加载） |
| 游戏启动报错 | 确认 rePatch 插件已启用；删除 `ux0:rePatch/PCSH10109` 后重跑安装器 |
| 模型没变 | 确认 `ux0:rePatch/PCSH10109/data/mod.cpk` 存在；完全退出游戏再进入 |

---

## 五、注意

> 本工具包不包含任何游戏本体数据；eboot 由安装器在玩家自己的主机上现场解密生成。
> 内置解密模块来自开源项目 PSVita-RE-Tools（GPLv3），见 来源与许可.txt。
> 仅限个人使用，禁止二次分享/上传/商用。
