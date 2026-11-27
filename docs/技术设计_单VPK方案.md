# P5D PSV「拖放即用」模组方案（D 方案）技术设计

> 目标：玩家只把 `mod.cpk` 放进 PSV（rePatch 目录），**不需要 FAGDec、不需要 PC 工具、不需要修改 eboot**，游戏启动时自动挂载模组。
> 适用：Persona 5: Dancing in Starlight 亚洲版 PCSH10109 版本 1.00，PSV 固件 3.60–3.68 + taiHEN + rePatch。
> 本文是离线设计文档，所有"已验证"均来自本机（3.65）实测；"待验证"项已标注。

---

## 1. 背景：为什么大方案/小方案能成功，而插件方案难

| 方案 | 工作层次 | 结果 |
|---|---|---|
| 大方案：替换 1.9GB `data.cpk` | 数据文件 | ✅ 成功 |
| 小方案：patched eboot（改挂载计数 + 重定位表项） | 加载前的文件 | ✅ 成功（已实机验证） |
| 插件方案：运行时修改游戏 | 运行中的进程/内核 | ❌ 未成功（本文要解决） |

根本原因：
- **小方案**在 SELF 加载器运行**之前**改写文件。加载器按重定位表把 3 条目表地址写入代码；游戏启动时，挂载循环读到的就是"原生 3 条目表"，引擎的 CRI binder 从初始化起就把它当作自己的一部分。
- **插件方案**在游戏**已经运行**时从外部修改。引擎的 CPK 列表由它自己的挂载循环维护：加锁 → 异步 bind → 轮询完成（状态 2 成功 / 6 失败）→ 记录 handle/offset → 解锁。外部注入的挂载；
  - 在锁内调用：同线程重复加锁失败 → bind 被立刻拒绝（状态 6），不生效、不崩溃；
  - 在锁外调用：binder 状态不完整，条目不进搜索表，同样不生效；
  - 唯一可靠办法是改游戏代码页（RX）中的挂载计数和表指针——必须内核态注入。

---

## 2. 已验证事实（本机 3.65 实测）

### 2.1 用户态插件（title plugin, `.suprx`）
- ✅ `PCSH10109` 的 title 插件可以加载（`ur0:tai/...` + `*PCSH10109` 段）。
- ✅ 可 hook 游戏主模块的 `module_start`（NID `0x935CD196`），hook 在 CPK 挂载之前执行。
- ✅ 可 hook 游戏内部函数（`taiHookFunctionOffset`），如挂载循环 `fn_811820b6`、绑定包装 `fn_81286ec4`。
- ✅ 可从 hook 引用 `struct _tai_hook_user.old` 反推运行时代码基址（无需 `sceKernelGetModuleInfo`）。
- ✅ 可读取/解码运行时重定位后的 movw/movt（取 data 段基址）。
- ❌ 不能写 RX 代码页；taiHEN 的注入 API（`taiInjectAbs`/`taiInjectDataForUser`）要求**源数据在内核地址空间**，用户插件无法满足。

### 2.2 内核态插件（`.skprx`）
- ❌ 通过 `*KERNEL` + config.txt 开机加载**失败**（多次尝试：3.65 专用 stub、attributes 7/0、正确 NID、ur0/ux0 路径、正确的 `.vita_link` 导入均无效果；原因未定位）。
- ✅ **运行时加载成功**（xerpi 式 loader VPK：`taiLoadStartKernelModuleForUser("ux0:data/tai/kplugin.skprx")`），屏幕显示 `Module loaded, ID: 0x4002006D`。
- ✅ 内核插件可写文件日志（`ksceIoOpen`/`ksceIoWrite` 在 3.65 可用）。
- ✅ 内核插件可 `taiHookFunctionExportForKernel` 挂内核导出函数；对 `ksceKernelLoadModuleForPid`/`ksceKernelLoadStartModuleForPid` 的 hook 能收到**游戏进程加载库**的事件（pid `0x4A2913` 等）。
- ✅ 内核插件可用 `taiInjectAbsForKernel(pid, dest, src_kernel, size)` 向用户进程注入数据（API 存在且可用）。
- ⚠️ **游戏主模块的启动不走** `ksceKernelStartModuleForPid`（挂上后从不触发），而走 SceAppMgr 内部路径。
- ⚠️ **在 modulemgr 的 hook 内调用 `ksceKernelGetModuleList/GetModuleInfo` 会死锁**（同线程重复加锁）→ 整机卡死，必须避免。

### 2.3 游戏内部结构（静态分析 + 实证）
- 挂载循环 `fn_811820b6`（ELF 偏移 `0x1820b6`，Thumb）：
  - 循环 2 次，hardcode 表地址（movw/movt 重定位到运行时 data 段地址）；
  - 每次：构造路径 `"%s%s/%s"`（前缀来自 getter，拼上表项名字），调用绑定包装 `fn_81286ec4(binder,0,path,0,idx,&handle)`；
  - 轮询 `fn_81287bba(handle,&status)` 直到 2/6；若表项 offset != -1 再调 `fn_8128853e(handle,offset)`；
  - 绑定全程在 `0x815676c0` 的锁内。
- 绑定包装 `fn_81286ec4`（`0x286ec4`）→ 实现 `fn_81286b7e`。
- 初始化 binder：`fn_812859fa(&global_813ad174)`。
- 原始 2 条目表：data 段偏移 `0x8af6c`（`0x813acf6c`）；第三条目区 `0x1aecd0`（全零可用空档，patched eboot 已实证可用）。
- 挂载计数指令：text 偏移 `0x1821b2`（`cmp r4,#2`，字节 `02 2c`）；表指针 movw/movt 在 `0x1820d6/0x1820da`。
- 表项结构：`{i32 offset=-1, char name[...]}`，步长 `0x104`。
- 路径格式：`%s%s/%s`；实测路径形如 `app0:/data/vita.cpk`。
- rePatch 可为 `app0:/data/mod.cpk` 提供新增文件（小方案实证）。

---

## 3. 失败方案归因表

| 尝试 | 结果 | 原因 |
|---|---|---|
| 用户插件 hook 绑定包装，插额外 bind（锁内） | 不生效 | 同线程锁重入失败，bind 返回 6 |
| 用户插件 hook 绑定包装，插额外 bind + 轮询 | 不生效 | 同上（锁内） |
| 用户插件 hook 挂载循环，锁前预绑定 | 不生效 | 无 IPC/地址推导不完整时静默跳过；即使调用成功，条目/时机不被 binder 接受 |
| 用户插件改写路径（vita→mod） | 不生效 | 异步 bind 读取路径时已被恢复/或 hook 未命中 |
| 内核插件经 `*KERNEL` 开机加载 | 不加载 | 未定位（SELF 有效但 taiHEN 不加载；运行时加载同一文件成功） |
| 内核插件 hook `StartModuleForPid` 注入 | 不触发 | 游戏主模块启动不走该函数 |
| 内核插件在 modulemgr hook 内查模块信息 | **整机卡死** | modulemgr 锁重入死锁 |

结论：**正确架构 = 用户插件负责"时机与地址"（已全部验证可行），内核插件只负责"注入"（API 可用），两者用安全 IPC 连接，内核插件内部绝不调用 modulemgr 查询 API。**

---

## 4. 推荐架构：用户插件 + 内核插件（IPC 注入）

### 4.1 组件

```
┌─────────────────────────────┐
│ 游戏进程（PCSH10109）        │
│  - 用户插件 p5d_user.suprx   │
│    * hook 主模块 module_start│
│    * 推算 text/data 基址     │
│    * 在挂载前发起注入请求    │
└──────────────┬──────────────┘
               │ IPC（见 4.3）
┌──────────────▼──────────────┐
│ 内核插件 p5d_kernel.skprx    │
│  - 常驻（loader 加载或开机） │
│  - 收到请求 → 校验 → 注入    │
│  - 不调用 modulemgr 查询接口 │
└─────────────────────────────┘
```

### 4.2 时序

1. 开机后运行一次 loader（或最终方案由 config 开机加载内核插件成功后再简化）。
2. 玩家启动 P5D。
3. 用户插件 `module_start` hook 触发（挂载之前）。
4. 用户插件：
   - 从 hook 引用反推 text base（`old - 0x1820b6`，以 `0x1821b2` 处字节 `02 2c` 校验）；
   - 解码 `0x1820d6` 处已重定位 movw/movt → 原始表运行时地址 → data base = 该值 - `0x8af6c`；
   - 组装请求结构（pid、text、data、magic）。
5. 用户插件通过 IPC 触发内核插件 → 内核插件做 3 处注入：
   - `text+0x1821b2`：`02 2c` → `03 2c`
   - `text+0x1820d6`：写入 `table_addr` 的 movw/movt（table_addr = data+0x1aecd0）
   - `data+0x1aecd0`：写入 3 条目表（mod.cpk / vita.cpk / data.cpk）
6. 用户插件 hook 返回 → 游戏 `module_start` 继续执行 → 挂载循环看到 3 条目表 → 挂载 mod.cpk（优先级最高）→ **模型替换成功**。
7. 之后换 MOD 只需替换 `ux0:rePatch/PCSH10109/data/mod.cpk`。

### 4.3 IPC 设计（需在实机上确认细节，二选一）

**方案 A（推荐）：hook 内核导出做"请求通道"**

- 内核插件 hook 一个**用户态可以主动调用的内核导出**，且该导出**不在 modulemgr 锁内**。候选：
  - `ksceKernelGetProcessId`（SceThreadmgrForDriver）——用户插件一定会调用，但参数无法携带数据；
  - `ksceKernelGetModuleInfo`（SceKernelModulemgr）——用户侧是 `sceKernelGetModuleInfo`，需要确认用户的 syscall 是否到达该导出；此调用本身可能涉及模块管理器锁，**需实测确认 hook 入口是否在锁前**（若在锁内则放弃该通道）。
- 请求协议：用户插件用**魔数**作为参数触发（例如 `modid=0x50354431`），并把请求结构体指针作为 `info` 参数传入；内核 hook 检测到魔数后：
  1. `ksceKernelMemcpyUserToKernelForPid(pid, kbuf, uptr, sizeof)` 复制请求；
  2. 校验字段（text/data 非空、`text_base+0x1821b2` 处字节为 `02 2c`）；
  3. 执行 3 处 `taiInjectAbsForKernel`；
  4. 把结果码写回用户结构体并返回 0（不调用原函数）。
- 优点：不需要自定义 syscall，不需要轮询；缺点：需要确认目标导出的 hook 时机安全。

**方案 B：自定义 syscall**

- 内核插件在 `exports.yml` 中声明 `syscall: true` 并注册 syscall；用户插件用对应 syscall 号调用，参数（pid/text/data）直接由内核读取。
- 优点：语义最干净、无死锁风险；缺点：需要 vitasdk 侧 syscall stub 与 taiHEN 版本支持，工程量更大（待验证）。

> 若两种 IPC 都不可用，退路：内核插件起**独立内核线程**轮询一个由用户插件写好的共享结构（共享内存块需解决"如何让内核找到用户 memblock"的问题，复杂度最高）。

### 4.4 内核插件职责（绝不越界）

必须遵守：
1. **不得**在 hook 内调用 `ksceKernelGetModuleList` / `ksceKernelGetModuleInfo` / 任何 modulemgr 查询函数（已证实死锁）。
2. **不得**在 modulemgr 相关 hook 内做耗时操作。
3. 注入前做**只读校验**（读取用户内存用 `ksceKernelMemcpyUserToKernelForPid`，不调用模块管理器）。
4. 注入失败时**不重试、不崩溃**，返回错误码给用户插件（用户插件忽略即可，游戏按原版运行）。
5. 记录日志到 `ux0:data/p5d_klog.txt`（已实证可用），便于出问题时定位。

### 4.5 用户插件职责

1. 只 hook 主模块 `module_start`（已实证可靠）。
2. 地址推导（已实证）：hook ref old → text base；解码 movw/movt → data base。
3. 发起 IPC；若失败（返回错误），什么都不做，让游戏原版运行（安全兜底）。
4. 可选：把成功/失败写到 `ux0:data`（用户插件在游戏进程内可能无法写文件——实测写 ux0:data 失败；因此失败状态只能靠内核日志/屏幕提示）。

---

## 5. 备选方案（若不采用 4 的架构）

| 备选 | 思路 | 评估 |
|---|---|---|
| B1. 纯用户态 hook 绑定函数 | 在 bind hook 里额外挂载 | 已实测：锁重入/binder 所有权被拒，不可行 |
| B2. 改写表内容（数据段） | 只改 data 不写代码 | 表只有 2 条目，第三条目位置被 binder 全局指针占用，不可行 |
| B3. 内核线程 + 内存扫描 | 内核线程等待游戏进程后扫描内存找模块 | 时机难以保证（module_start 极快），风险高 |
| B4. 让 taiHEN 开机加载内核插件 | 修复 config 加载 | 原因未定位；若能解决，玩家无需每次运行 loader，是最终形态 |

---

## 6. 关键常量与偏移（附录）

### 6.1 游戏（PCSH10109 1.00）
| 名称 | 值 |
|---|---|
| 主模块名 | `xrd757_psp2_p5d_master` |
| module_start NID | `0x935CD196` |
| 挂载循环 | text 偏移 `0x1820b6`（Thumb） |
| 挂载计数指令 | text 偏移 `0x1821b2`（`02 2c` → `03 2c`） |
| 表指针 movw/movt | text 偏移 `0x1820d6/0x1820da` |
| 原始表 | data 偏移 `0x8af6c`（值 `0xFFFFFFFF` 起） |
| 空档（放 3 条目表） | data 偏移 `0x1aecd0` |
| binder 全局指针 | data 偏移 `0x8b174` |
| 初始化 binder | text 偏移 `0x2859fa` |
| 绑定包装 | text 偏移 `0x286ec4` |
| 绑定实现 | text 偏移 `0x286b7e` |
| 轮询 | text 偏移 `0x287bba` |
| 路径 getter | text 偏移 `0x199d7e` / `0x199d8a` |
| 表项步长 | `0x104` |
| 表项布局 | `i32 offset`（-1）+ 内联名字 |
| 路径格式 | `"%s%s/%s"` |

### 6.2 内核函数（3.65）
| 名称 | 库 | NID |
|---|---|---|
| ksceKernelLoadModuleForPid | SceModulemgrForKernel (`0x92C9FFC2`) | `0x4E85022D` |
| ksceKernelStartModuleForPid | SceModulemgrForKernel | `0x3FE47DDF`（3.60：`0x6DF745D5`） |
| ksceKernelLoadStartModuleForPid | SceModulemgrForKernel | 3.60：`0x9D953C22`（3.65 未确认） |
| ksceKernelGetModuleInfo | SceModulemgrForKernel | `0xDAA90093`（3.60：`0xD269F915`） |
| ksceKernelGetModuleList | SceModulemgrForKernel | flags1=`0x7FFFFFFF`, flags2=`1`（文档） |

### 6.3 已确认的运行时行为
- 游戏进程加载库走 `ksceKernelLoadModuleForPid` / `ksceKernelLoadStartModuleForPid`（hook 可收到，pid 即游戏进程）。
- 游戏主模块启动**不**走 `ksceKernelStartModuleForPid`。
- 在 modulemgr hook 内调 modulemgr 查询 API → 整机卡死。

---

## 7. 测试与发布计划

### 7.1 离线可完成
1. 实现用户插件（地址推导 + IPC 触发），**不注入**，仅用内核日志验证地址推导正确（可先在 PC 用 FAGDec ELF 模拟偏移）。
2. 实现内核插件（IPC 通道 + 校验 + 注入 + 日志），全部代码可离线编写。
3. 静态审查：确保没有 modulemgr 查询调用、没有锁内耗时操作。

### 7.2 实机最小验证（安全优先）
1. 实机测试 IPC 通道是否触发（内核日志出现"请求收到"）。
2. 实机测试注入结果与模型替换（成功即完成）。
3. 失败兜底：注入失败不崩溃、游戏原版运行。

### 7.3 发布形态（若成功）
- 组件：`P5D Mod Loader`（loader VPK）+ `p5d_kernel.skprx` + `mod.cpk`；
- 玩家流程（当前形态）：安装 loader VPK → 把 `p5d_kernel.skprx` 放 `ux0:data/tai/kplugin.skprx` → 每次开机运行一次 loader → 进游戏即用；换 MOD 只替换 `mod.cpk`；
- 最终形态（若能解决 config 开机加载）：把 `p5d_kernel.skprx` 写进 `*KERNEL`，玩家开机即自动生效（无需运行 loader）。

---

## 8. 风险与工作量

| 风险 | 说明 | 缓解 |
|---|---|---|
| IPC 通道选择错误 | 目标导出可能在锁内或不经过 | 先做通道探针（只读日志）验证 |
| 注入时机偏差 | 用户插件 module_start hook 已实证在挂载前，风险低 | 校验字节后才注入 |
| 死锁/卡死 | 只要不在 modulemgr hook 内查模块即避免 | 代码审查 + 仅用 IPC 通道 |
| 开机加载内核插件仍失败 | 玩家每次需运行 loader | 作为后续独立课题（taiHEN 加载原因） |
| 固件差异 | 3.60/3.65/3.68 NID 不同 | 用 db/363 与 db/360 常量，运行时探测 |

**工作量估计**：
- 用户插件（地址推导 + IPC 发送）+ 内核插件（IPC 接收 + 注入 + 校验 + 日志）：约 1–2 天编码；
- 通道/实机验证：1–2 轮实机（每轮 5–10 分钟）；
- 若一切顺利，D 方案可在数天内打通；若 IPC 通道都不可行，需要改用自定义 syscall，再增加 1–2 天。

---

## 9. 结论

- D 方案**技术上仍然成立**，关键路径已经收敛到"**内核注入 + 正确 IPC/时机**"这一个问题上；
- 所有易踩的坑（用户态无法写 RX、taiHEN 注入源必须在核心空间、binder 锁重入、modulemgr hook 死锁、游戏主模块启动路径）都已查清；
- 下一步最稳妥的做法是：**先实现 IPC 通道探针（只记录、不注入）**，确认通道安全后，再接入注入逻辑。

---

## 10. IPC 通道探针（已编写完成，待实机验证）

### 10.1 文件
| 文件 | 作用 |
|---|---|
| `build/p5d_ipc.skprx` (16,729 B) | 内核侧：挂 `ksceKernelGetModuleInfo`(0xDAA90093)、`ksceKernelGetProcessId`(0x9DCB4B7A)、`ksceKernelDelayThread`(0x4B675D05)，只记录、不修改任何东西，日志写 `ux0:data/p5d_ipc.txt` |
| `build/p5d_ipc_user.suprx` (15,992 B) | 用户侧：在游戏 `module_start` hook 里依次调用三个用户 API（`sceKernelGetProcessId`、`sceKernelDelayThread(1000)`、`sceKernelGetModuleInfo(0,...)`），用于触发内核侧 hook |

### 10.2 目的（回答一个关键问题）
用户态 API 调用是否会经过我们 hook 的内核导出？日志里出现哪些 tag（`GMI`/`GPID`/`DELAY`）+ pid，就能确定 IPC 通道：
- 若 `GMI pid=游戏进程` 出现 → 通道 A 可用：把请求结构经 `info` 指针 + 魔数传入，内核插件解析后注入；
- 若只有 `GPID`/`DELAY` 出现 → 参数无法携带指针，改用方案 B（自定义 syscall）；
- 若一个都没有 → 这些导出不可触达，改试其它导出（`ksceKernelGetModuleInfoForDebugger`、`ksceKernelGetModuleList` 等），或直接做 syscall 方案。

### 10.3 实机测试步骤（唯一需要实机的一次，安全：纯日志）
1. 上传 `p5d_ipc.skprx` → `ux0:data/tai/kplugin.skprx`；
2. 上传 `p5d_ipc_user.suprx` → `ur0:tai/p5d_ipc_user.suprx`（或 ux0:tai），`ur0:tai/config.txt` 末尾加：
   ```
   *PCSH10109
   ur0:tai/p5d_ipc_user.suprx
   ```
3. 重启 PSV（读新配置）；运行 `P5D Kernel Loader`（加载内核探针）→ 按 START 退出；
4. 启动 P5D，进主菜单；
5. 打开 VitaShell FTP，读 `ux0:data/p5d_ipc.txt`。
6. 预计日志格式：
   ```
   ipc probe loaded
   hooks_ok <a> <b>
   hook_delay <c> 00000000
   DELAY 000003E8 00000000      ← 用户侧 sceKernelDelayThread(1000) 被内核 hook 捕获
   GPID 00000000 00000000       ← 用户侧 sceKernelGetProcessId 被捕获
   GMI 00000000 xxxxxxxx        ← 用户侧 sceKernelGetModuleInfo 被捕获（如通道存在）
   ```

### 10.4 测试后动作
- 记录哪些 tag 出现、对应 pid；
- 按 10.2 的决策树选定 IPC 通道，进入第 4 章的实现；
- 实现顺序：通道 → 请求结构 → 校验 → 注入 → 模型验证。

### 10.5 安全说明
本探针**只读日志**：不注入、不修改游戏内存、不在 hook 内调用 modulemgr 查询 API（无死锁风险）。最坏结果是探针不工作/无日志，不会影响系统和游戏。
---

## 11. IPC 探针 v2（安全版，已编译完成）

### 11.1 v1 失败教训（实机掉电关机）
v1 探针在**内核热函数**（`ksceKernelGetProcessId`）的 hook 内直接做文件 IO（`ksceIoOpen`）→ 在关键内核路径里死锁/崩溃 → 整机掉电关机。
结论（已写入代码规范）：
1. **hook 内绝不调用文件 IO / 模块管理 API**；
2. 不要 hook 每秒被调用成千上万次的超热函数；
3. 日志由独立内核线程写，hook 只写内存环形缓冲。

### 11.2 v2 结构
| 文件 | 大小 | 作用 |
|---|---|---|
| `build/p5d_ipc2.skprx` | 17,809 B | 内核侧：hook `ksceKernelGetModuleInfo`(0xDAA90093) 与 `ksceKernelDelayThread`(0x4B675D05)；hook 只 push 到 64 entries 静态环；独立内核线程每 300ms 刷到 `ux0:data/p5d_ipc2.txt` |
| `build/p5d_ipc_user2.suprx` | 15,784 B | 用户侧：游戏 `module_start` hook 里调用 `sceKernelDelayThread(1000)` 和 `sceKernelGetModuleInfo(0,&mi)` 各一次（触发内核侧记录） |

### 11.3 日志格式
```
ipc2 probe loaded
hooks <a> <b>
ipc2 started
ipc2 thread alive
DELAY 000003E8 00000000        <- 用户侧 sceKernelDelayThread(1000) 被内核 hook 捕获
GMI   <pid> <modid>            <- 用户侧 sceKernelGetModuleInfo 被内核 hook 捕获（通道存在的证据）
```

### 11.4 测试步骤（安全，仅日志）
1. `p5d_ipc2.skprx` → `ux0:data/tai/kplugin.skprx`
2. `p5d_ipc_user2.suprx` → `ur0:tai/p5d_ipc_user2.suprx`
3. `ur0:tai/config.txt` 末尾：
   ```
   *PCSH10109
   ur0:tai/p5d_ipc_user2.suprx
   ```
4. 重启 PSV → 运行 P5D Kernel Loader（显示 Module loaded）→ START 退出 → 启动 P5D 进主菜单
5. 读 `ux0:data/p5d_ipc2.txt`

### 11.5 判读
- `DELAY` 出现 → `ksceKernelDelayThread` 通道可触达（可用于"心跳/触发"，但参数只能带一个整数）；
- `GMI <游戏pid>` 出现 → **最优通道**：用户插件传结构指针 + 魔数，内核插件解析后注入；
- 仅 `DELAY` → 需要自定义 syscall 或"魔数编码在 usec 参数"的简化方案；
- 都无 → 换其它导出/做 syscall。

### 11.6 安全度
v2 的 hook 只做"取数 + 写内存数组"，不调用任何系统 API；写文件在内核线程中进行。相比 v1 风险已大幅降低，但仍属内核插件，建议在你有空时测试并做好重启准备。
---

## 12. 实机实测结论（D 方案最终状态，2026-11 实测）

### 12.1 已打通并验证的环节
| 环节 | 证据 |
|---|---|
| 内核插件运行时加载 | loader 屏幕 `Module loaded`，日志 `plugin loaded` |
| 用户插件（title suprx）加载与 hook | 游戏启动时 hook 触发 |
| 用户→内核 IPC | `ksceKernelDelayThread` 通道：分片传 6 个 16 位数据字全部到达 |
| 运行时地址获取 | 用户 API `sceKernelGetModuleInfo(modid)` 拿到段地址，`text+0x1821b2` 校验字节 `0x2C02` 通过（多次不同 ASLR 基址均通过） |
| 表地址推导 | 解码加载器已重定位的 movw/movt → 原始表地址 + `0x13D64` |
| 内核注入 | `taiInjectAbsForKernel` 返回成功，**回读确认**：cmp 变为 `0x2C03`、表首项 `0xFFFF`、movw 编码正确 |

### 12.2 最终阻塞点
在以下两个注入点（module_start 入口 / 挂载循环入口）**同步注入**、注入内容与已成功的小方案（patched eboot）**逐字节一致**，游戏仍在启动时崩溃（C2，无转储）：
- 补丁已确认写入内存（用户插件回读 0x2C03 / 0xFFFF）
- 崩溃发生在注入之后、游戏启动早期
- 同样的补丁在「patched eboot」方案中完全正常

### 12.3 判断
运行时修改用户进程代码页在该环境（3.65 + 原版 eboot）下**不可行**，尽管注入 API 返回成功且数据回读正确。可能原因（未能在不改内核的前提下进一步验证）：
1. SELF 加载器/内存保护对原版 eboot 代码页的写后执行（I-cache/页表状态）处理与小方案不同；
2. 游戏启动早期对数据段/代码段存在一致性校验或重初始化；
3. 运行时补丁与 3 条 CPK 挂载的交互在加载器层面不被支持（小方案改的是加载器读取的文件，运行时改的是已映射内存）。

### 12.4 结论与建议
- D 方案技术上限：**所有链路已验证成功，唯独"运行时改代码页"这一关无法安全通过**；
- 建议继续以「小方案（FAGDec + 一键 bat）」作为发布方案（已实机验证，模型替换成功）；
- D 方案保留文档与全部源码（`C:\dev\p5d-psv-plugin\`），若未来有内核级调试手段（可定位崩溃 PC）可再继续；
- 设备已恢复干净状态（无 taiHEN 插件、无加载器配置行）。
---

## 13. 最终结论（决定性实验，2026-11-27 实测）

### 13.1 消融实验矩阵（全部实机）
| 实验 | 注入内容 | 结果 |
|---|---|---|
| A. 不注入（只 hook） | 无 | ✅ 游戏正常启动 |
| B. 写表数据（不动代码） | 数据段写 3 条目表 | ✅ 正常（未被引用，无害） |
| C. 只写代码页，写入**原值** | movw/movt 原样重写（语义 100% 不变） | ❌ 崩溃 |
| D. 改表指针（克隆 vita/data 表） | 代码页写新表地址 | ❌ 崩溃 |
| E. 改计数+表指针（3 条目挂载） | 完整补丁 | ❌ 崩溃 |

### 13.2 判定
**任何对游戏主模块代码页的运行时写入（即使写入完全相同的字节）都会导致游戏启动崩溃。**
- 与补丁数值无关（C 实验证明）；
- 与是否挂载第 3 个 CPK 无关（C 实验未改计数）；
- 与我们的 IPC/时序无关（B 实验证明数据写入与 hook 均安全）；
- 与 patched eboot 小方案形成鲜明对比：小方案在**加载前**修改文件，由 SELF 加载器完成映射，游戏正常。

### 13.3 根因（高置信度）
原版 eboot 的代码页由 SELF 加载器以特殊保护方式映射（代码段完整性/缓存/页表属性），运行时通过 `taiInjectAbsForKernel` 写该页会破坏这套状态，导致后续执行崩溃；taiHEN 的 cache flush 只作用于当前核心且无法修复该状态。
小方案之所以可行，是因为修改发生在**加载器解析之前**，由加载器自己把补丁字节当作原始内容映射。

### 13.4 D 方案的真实上限
- ✅ 已完全打通：内核插件运行时加载、用户插件 hook、用户→内核 IPC、运行时地址推导、数据段注入；
- ❌ 无法逾越：**运行时代码页写入**（在 3.65 原版 eboot 上必崩）；
- 理论上仅剩两条路，但都超出"插件"范畴：
  1. **在 Vita 上本地生成 patched eboot**（把 FAGDec 解密 + 补丁 + vita-elf-inject 移植成 Vita 自制程序，再用 rePatch 加载）——工程量大，但玩家仍能"只放 mod.cpk"（首次需在 Vita 上运行一次生成器）；
  2. 等待/寻找能安全写代码页的内核级方法（需更强的内核调试能力）。

### 13.5 建议
- 发布继续使用小方案（FAGDec + 一键 bat），已验证稳定；
- 若未来要做"全程 Vita 一键"，选择 13.4 的第 1 条路线（本地生成 patched eboot），而不是插件注入；
- 设备已恢复干净状态（无插件、无配置行、无加载器残留）。
---

## 14. D 方案替代路线（已成功）：全 PSV 生成 patched eboot

### 14.1 背景
第 13 章的实测证明：**运行时写游戏代码页不可行**（哪怕写回原值也崩）。
但小方案之所以可行，是因为它在**加载器解析之前**改文件。于是改变思路：
**在 Vita 上现场生成 patched eboot.bin，再交给 rePatch 加载** —— 完全不需要 PC。

### 14.2 已打通的全流程（实机验证成功）
```
FAGDec（PSV 上运行，解密一次）
   → ux0:FAGDec/app/PCSH10109/eboot.bin + eboot.bin.elf
[VitaShell 复制两个文件到 ux0:data/P5DMod/]
P5DModInstaller.vpk（本项目自制，运行于 PSV）
   → 补丁 ELF（mount count/reloc/table）
   → 用 zlib 重新压缩每个 ELF 段
   → 重建 decrypted SELF（更新段信息长度/偏移、ELF SHA256 摘要、清 NPDRM）
   → 写 ux0:rePatch/PCSH10109/eboot.bin
   → 复制 mod.cpk 到 ux0:rePatch/PCSH10109/data/
启动游戏 → 引擎挂载 mod.cpk（优先于 vita.cpk/data.cpk）→ 模型替换
```

### 14.3 关键技术点（已在实机验证）
1. FAGDec 输出的 decrypted SELF 段是 **zlib 压缩流**（`78 9C` 头，compression=2）；
2. 注入 = 对每个 ELF 段重新 `compress2(level 9)`，写回并更新 SEGMENT_INFO（offset/length）；
3. 段增长时按原偏移顺序整体后移（0x10 对齐），并更新 header.self_filesize；
4. 重算 **ELF 文件 SHA256** 写入 ControlInfo(type 4) 的 elf_digest，NPDrm(type 5) 清零；
5. 输出前 0x1000 头 + ELF 头(52B) + 原 SELF 程序头块，与原工具一致。
6. **实机校验：Vita 上生成的 eboot.bin 与 PC 验证版 SHA256 完全一致**
   （EAB993176031E8883083BDED66BBA1950EC33E7C5CC5BBED68DDCD7237C98E5A），
   游戏启动后模型替换成功。

### 14.4 玩家流程（无需电脑）
1. 安装 FAGDec.vpk（PSV 上运行一次，解密 PCSH10109）
2. VitaShell 把 `eboot.bin`、`eboot.bin.elf` 复制到 `ux0:data/P5DMod/`
3. 把 `mod.cpk` 放进 `ux0:data/P5DMod/`
4. 运行 **P5D Mod Installer** → 显示 `DONE!`
5. 启动游戏；换 MOD 只需替换 `ux0:rePatch/PCSH10109/data/mod.cpk`

> 唯一需要 VitaShell 手工复制的一次，是因为普通应用看不到 `ux0:FAGDec` 根目录
> （实测 stat 返回 0x80010002）。

### 14.5 产物
- `C:\dev\p5d-vita-installer\`：Vita 安装器完整源码与构建脚本
  - `src/p5d_patch.c`（ELF 补丁器）
  - `src/p5d_inject.c`（SELF 重建：zlib + SHA256）
  - `src/main.c`（Vita 界面与文件流程）
  - `build/P5DModInstaller.vpk`（261,233 字节，SHA256 F40B0621…）
- 发布目录已更新：`C:\Users\凯梦\Downloads\P5D女角色裸体模组\psv实机用\`
  - 新增 `P5DModInstaller.vpk`、`使用说明_全PSV一键.md`
  - 原 PC 小方案文件保留作备用

### 14.6 结论
D 方案（"插件拦截"）本身受运行时写代码页限制不可行，但**替代路线已完全打通**：
玩家无需电脑，只需 FAGDec 一次 + 运行一次安装器 + 放入 mod.cpk，即可在 PSV 上完成全部模组安装。
---

## 15. 单 VPK 全自动方案（最终成功，2026-11-27 实机验证）

### 15.1 成果
玩家**只安装一个 `P5DModInstaller.vpk`** 并运行一次，即可完成全部安装：
解密游戏 eboot → 打 3 CPK 挂载补丁 → 重建 decrypted SELF → 写入 rePatch → 复制 mod.cpk。
实机验证：安装器显示 DONE，进入游戏模型替换成功。

### 15.2 关键突破（按发现顺序）
1. **FAGDec 全流程可复用**：其内核模块 `kentente.skprx`（SceSblAuthMgr 解密）+ 用户层
   `userAllied.suprx` 均来自开源项目 PSVita-RE-Tools，用本机 VitaSDK 可重新编译；
   syscall 存根可由 `vita-elf-export` + `vita-libs-gen` 自动生成。
2. **taiHEN unsafe 权限**：`taiLoadKernelModule` 的调用者必须是 shell 权限；
   eboot 用 **UNSAFE SELF + authid 0x2808000000000000** 打包即可通过
   `ksceSblACMgrIsShell` 检查（与 FAGDec 相同做法）。
3. **物理内存分配失败**：kentente 默认把解密缓冲放在内核根分区/CDRAM，3.1MB 段会
   `NO_FREE_PHYSICAL_PAGE`；改为**依次尝试多个内核内存分区**
   （TMP_RW → ROOT_RW → UMAIN_RW → GAME_RW → TOOL_RW → TMP_FS_GAME_RW），
   实测 seg0 落到 `KERNEL_ROOT_GAME_RW`、其余落 TMP_RW 后成功。
4. **appmgr 挂载视图不是解密视图**：`sceAppMgrGameDataMount` 返回 `gp:` 挂载里的
   eboot 仍是加密 SELF（enc=1），不能绕过解密；必须走 kentente 真解密。
5. **段重建**：kentente 输出 `<out>.segN`（解密后的 zlib 流）；用 zlib `uncompress`
   逐个解压到 ELF 段偏移，再复用之前的补丁器与注入器生成 rePatch eboot。

### 15.3 安装器流程
```
mount game (sceAppMgrGameDataMount)
 → 找 rif (license/app/PCSH10109/*.rif)
 → taiLoadStartKernelModule(kentente) + sceKernelLoadStartModule(userAllied)
 → userAlliedDecryptSelf(path, outpath, rif)   // kentente 解密 5 个段
 → uncompress 各 .segN → 组装 ELF（header/phdrs/segments）
 → p5d_patch_elf（cmp r4,#3 / reloc→0x1aecd0 / 3 条表）
 → p5d_inject（zlib 重压缩 + ELF SHA256 + 清 NPDRM + FAGDec 头修改）
 → 写 ux0:rePatch/PCSH10109/eboot.bin
 → 复制内置 mod.cpk 到 rePatch data
```

### 15.4 产物
- `C:\dev\p5d-vita-installer\`：完整源码 + 构建脚本
  - `src/main.c`（Vita 端主流程）、`src/p5d_patch.c`、`src/p5d_inject.c`
  - `fagdec/`（PSVita-RE-Tools 的 kentente/userAllied 源码与构建，含内存分配修复）
  - `assets/`（kentente.skprx / userAllied.suprx / mod.cpk）
  - `build/P5DModInstaller.vpk`（1,080,408 字节，SHA256 28C006A3…）
- 发布目录已更新：`P5DModInstaller.vpk` + `使用说明_单VPK版.md` + 许可/校验

### 15.5 玩家流程（最终）
1. 安装 `P5DModInstaller.vpk`
2. 运行 "P5D Mod Installer" 一次（自动解密/补丁/注入，约几十秒）
3. 启动游戏，模型已替换
4. 换 MOD：替换 `ux0:rePatch/PCSH10109/data/mod.cpk`；还原：删除 rePatch 下两个文件

### 15.6 结论
原先"插件运行时拦截"路线（第 13 章）受代码页保护限制不可行；
**"在主机上现场生成 patched eboot" 路线已完整实现并实机验证成功**，
达到最初目标：玩家无需电脑、无需 FAGDec、拖入 mod.cpk 即可使用。
---

## 16. 中文化（应用名 + 界面）

### 16.1 LiveArea 应用名
- `vita-mksfoex` 在中文 Windows 上按 GBK 写 SFO，PSV 需要 UTF-8；
- 解决：先用 mksfoex 生成英文 SFO，再**原地替换** TITLE / STITLE 字符串
  （UTF-8 写入原 data 区，更新 data_len，保持 SFO 结构/偏移不变），
  安装正常、桌面显示中文名「P5D 模组安装器」。
  （整体重建 SFO 会导致安装报 0x80105A03，不可取。）

### 16.2 界面中文
- 现有 debugScreen 8x8 字体仅支持 ASCII；
- 方案：离线用 Windows 字体（微软雅黑）把所需中文提示渲染为 **1bpp 位图**，
  生成 `zh_font.h` 嵌入程序（11 行，约 30KB）；程序自绘到 framebuffer，
  ASCII 动态信息用 8x8 字体放大绘制，完全不依赖系统字体/PGF。
- 实测：中文标题、进度提示、完成/失败提示均正常显示。

### 16.3 最终包
`P5DModInstaller.vpk` 1,085,310 字节（中文名 + 中文界面 + 全自动解密补丁注入），
实机验证：安装成功、中文显示正常、游戏模型替换成功。