# 运输机投送载具：原版集装箱链的参数（逆向 + 运行时探针）

分支 `feat/fb1009d-airdrop-vehicle`，接 `docs/feedback-2026-10-09c-bar.md` §3。EDF.dll TimeDateStamp `0x678CCB46`，地址为 RVA。

## 状态（2026-10-10）

- **运行时捕获没有跑成**：要起游戏时发现 `EDF6.exe` 已在运行（PID 75320，00:01:04 启动，`EDF6VehicleCrew.log` 正在写任务内日志），
  是用户在玩。按规则没有关游戏、没有往游戏目录装任何文件。
- 本轮完成：静态逆向补齐了 §1 的调用约定和结构布局（H，代码级）；`tests/autopilot` 加了原版投放探针和一条命令跑完的选项（§3）。
- 未完成：运行时抓取（§2 列出要回答的问题）、插件运输机接集装箱流程、实机验证。游戏关着的时候直接执行 §3 的命令即可。

## 1. 静态逆向（H = 指令级）

### 1.1 链路

| 步骤 | 地址 | 说明 |
|---|---|---|
| 运输机初始化 | Transporter508 vtable `0x17D75C0` 槽 50 = `0x5E5070` | 10 个参数，见 1.2；生成集装箱、配置、把集装箱交给请求者 |
| 生成集装箱 | `0x5E5260` 处 `CreateObject 0x11945E0(mgr=*(EDF+0x20B2958), &运输机+0x60 矩阵, 集装箱SGO, &InitParam)` | InitParam 见 1.3 |
| 运输机抓住集装箱 | `+0x790/+0x798`（shared_ptr） | 运输机更新 `0x5E58B0` 每帧把自己模型控制器（`+0x780` → `+0x700`）里一个定位矩阵（`+0xB0..+0xEF`）**原样写进集装箱 `+0x60..+0x9F`**：集装箱本身不跟随谁，位置由持有者每帧写 |
| 配置集装箱 | `0x5E8B40(container, shared_ptr<请求者>*, const wchar_t* 载具SGO, const VehicleSetup*, float 等级)`，唯一调用点 `0x5E5687` | 请求者 → `+0xB70/+0xB78`；载具 SGO → `+0xB80`（wstring）；设定 → `+0xC38`（`0x5E44F0` 拷贝）；等级 → `+0xBA0` |
| 释放 | 运输机槽 47 `0x5E5CE0`（动画事件，edx=0）调 `0x5E84D0(container)`，清 `+0x790/+0x798`，播音效 | `0x5E84D0` 只是把集装箱状态机（`+0xB40`）切到卸车态 `0x5E8C00` |
| 下落 | `0x5E8C00` 进入（edx=0）：`+0xB68=1`，按当前矩阵建刚体（`+0xBB0`）——**物理下落** | |
| 卸车 | `0x5E8C00` 更新（edx=1）：状态时间（`*(+0xB48)`）过门限且速度够小（或时间更长）→ `CreateObject(集装箱+0xB80 的 SGO)`（`0x5E8FDC`），`SetTeam(veh,5,1)`，`veh+0x67C = +0xBA0`，按设定调载具槽 46（`+0x170`），再给请求者发事件 `0x10000010`（请求者 vfunc `+0x50/+0x48/+0x58`，带载具弱引用），联机时注册网络 ID，删除集装箱 | 请求者为空（`+0xB70 == 0`）时整段事件跳过（`test rbx,rbx`），**请求者可以为空** |

### 1.2 运输机初始化 `0x5E5070` 的参数

序言：8 次 push、`lea rbp,[rsp-108h]`、`sub rsp,208h`。栈参数从入口 rsp `+0x28` 起：

| # | 来源 | 用途 |
|---|---|---|
| 1 rcx | this | 运输机 |
| 2 rdx | `shared_ptr<请求者>*`（存 `[rbp-0x60]`） | `0x118ADF0(请求者, 集装箱)`；传给配置 |
| 5 | `const wchar_t*` | 集装箱 SGO（`CreateObject` 的 r8） |
| 6 | `const wchar_t*` | 载具 SGO（配置的 r8） |
| 7 | `const wchar_t*`，可为空 | **设定的文字形式**：非空时设定 = {wstring = 该文字, value = 空} |
| 8 | 指向一个 SGO 值（variant） | 第 7 个为空时用：设定 = {wstring = 空, value = 该值的拷贝} |
| 9 | int | 透传给基类初始化 `0x5E6870` |
| 10 | float | 等级（配置的第 5 个参数） |

### 1.3 集装箱的 InitParam（`0x5E51F1..0x5E5245` 原地构造，0x58 字节）

| 偏移 | 值 |
|---|---|
| +0x00 | 虚表 `Transporter_Container::InitParam` = EDF+`0x17D7818` |
| +0x08..+0x27 | 0（+0x28 字节 0） |
| +0x30..+0x4F | 派生网络 ID：`0x776790(&id, 运输机网络对象, 运输机+0x778 计数)`，运输机网络对象 = `运输机+0x130` 弱引用锁出的对象的 `+8` |
| +0x50 | 运输机 `+0x778` 计数 |

离线时（`0x7748F0(obj+0x120)` 为假）集装箱和载具都不注册网络 ID；联机时集装箱用 `0x781950` 注册（房主为拥有者）。

### 1.4 「载具设定」结构 `VehicleSetup`（0x38 字节，`0x5E44F0` 拷贝，析构表 `0x1765220`）

| 偏移 | 类型 | 证据 |
|---|---|---|
| +0x00 | `std::wstring` 文字形式（size 在 +0x10，cap 在 +0x18；空串 = {0, 7}） | `0x5E54EA..0x5E54FA` 初始化；卸车 `cmp [rdi+0xC48],0`（= +0xC38 的 size） |
| +0x20 | SGO 值 variant 的 16 字节存储 | |
| +0x30 | u16 类型下标，`0xFFFF` = 空 | `cmp word [rdi+0xC68],0xFFFF` |

卸车时：文字非空 → `0x62D890(veh, &tmp, &setup)` 把文字解析成值再交 `veh->vfunc[+0x170]`；否则值非空 → 直接 `veh->vfunc[+0x170](&setup.value)`；都空 → 不调。

## 2. 运行时要回答的问题（探针会打出来）

1. 原版请求走的是设定的哪一种形式（第 7 个参数的文字，还是第 8 个参数的值）；值的类型下标和存储内容（Grape 的 `Ammo_CustomParameter[4][3]`）。
2. 第 2 个参数「请求者」是谁（玩家士兵？），集装箱 `+0xB70` 在离线下是否为空也能卸车。
3. InitParam 在离线下 +0x30..+0x57 的实际值（派生 ID 是否全 0）。
4. 集装箱被抓着时的矩阵相对运输机的偏移（运输机位置 vs 集装箱位置），释放高度，落地到出车的时间，出车位置。

## 3. 用 CLI 跑原版投放（游戏关着时）

```
cmake --build build --target EDF6Autopilot
python tests/autopilot/drive.py run RM015 1 120 %TEMP%\airdrop_probe.log ^
    --loadout tests/autopilot/loadouts/airraider_grape.ini --cmd "probe airdrop" --shots %TEMP%\airdrop_shots
```

- `--loadout`：把 `airraider_grape.ini`（空降兵、载具槽 = 原版 Grape 第 1308 行、开局补满）临时放成 `Mods/Plugins/EDF6TestRange.loadout.ini`，
  由已安装的 EDF6VehicleCrew 在建人时读；文件已存在则拒绝（不覆盖测试场的），跑完只删自己写的那份。
- `--cmd "probe airdrop"`：探针（`tests/autopilot/airdrop_probe.cpp`）在 4 个直接调用点改道到记录函数再调原函数，不改原版行为：
  生成集装箱（`0x5E5260`，同时从运输机初始化的栈帧读出 1.2 的全部参数）、配置（`0x5E5687`）、释放（`0x5E5D3D`）、出车（`0x5E8FDC`）；
  出车后 1/3/6/10 秒读载具位置。
- 触发：游戏按键表在存档里，不知道「呼叫载具」绑在哪个键，探针在任务开始 25 秒后依次按住候选键（鼠标中键 / 侧键、字母、数字、
  Shift、Ctrl、空格、右键；不含 Esc、Tab、Alt、F 键），每个 1.5 秒，看到运输机就停，并在日志里写出当时按着的键。按键只经 autopilot
  对 EDF.dll 的键盘导入生效，不发到桌面。
- 日志里找 `PROBE` 行。
