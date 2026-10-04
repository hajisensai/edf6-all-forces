# 载具状态 HUD 逆向笔记（`src/hud.cpp`）

EDF.dll TimeDateStamp `0x678CCB46`，地址均为 RVA。可信度：H = 反汇编逐条确认；M = 由调用方式推断、未进游戏验证；L = 猜测。

## 0. 挂点

不新挂钩。复用 `subcarrier.cpp` 已有的 `GaugeHook`（把 `HudPlayer_FollowerDurability` 绘制 `0x8040E0` 里唯一的
`call 0x804300` 改指过来，见 `docs/subcarrier-re.md` §4）。`GaugeHook` 先画原版跟随者血条、再画潜舰替身血条，最后调
`HudDraw(viewProj, ctx=r9, viewport=第5参数, panels, n)`。

| 项 | 内容 | 可信度 |
|---|---|---|
| `0x804300` 的 r9 | 即 `0x8040E0` 的 rdx（HUD 绘制上下文 ctx），原样传给 `0xC2FB0` 的 rdx | H |
| 第 5 参数 | 视口：`+8` 宽、`+0xC` 高（int） | H |
| rdx（viewProj） | 4 行 × vec4；`clip[k] = x*vp[k] + y*vp[4+k] + z*vp[8+k] + vp[12+k]` | H |
| 屏幕坐标 | `sx = W/2 + W/2·ndc.x`，`sy = H/2 − H/2·ndc.y`（左上原点、y 向下）；剔除 `w==0`、`|ndc.x|>1`、`|ndc.y|>1`、`z∉[0,1]` | H |
| 原版血条尺寸 | 半宽 `31·uiW/1920`、半高 `3·uiH/1080`（`ui` = `*(*(0x2137090)+0x10)` 的 `+0x20/+0x24`）；位置在对象 `+0x90` 上方 2 m | H |

## 1. 四边形 `0xC2FB0`（血条的底框和填充都用它）

`0xC2FB0(drawer = *(EDF+0x2139A78), ctx, const float m[16], const float rgba[4], int topology = 5, const float* xyz, int count = 4, void* tex = 0)`

| 项 | 内容 | 可信度 |
|---|---|---|
| m | 行主序 4×4，原版传单位阵 + 第 4 行平移 `(sx, sy, ndc.z, 1)` | H |
| 顶点 | 4 个 float3，顺序 `(-x,-y) (x,-y) (-x,y) (x,y)`，三角带（D3D11 topology 5） | H |
| 颜色 | RGBA float：底框 `0x17F42E0 = (0,0,0,0.5)`；填充按 `hp/max·3` 取 `0x17F6C40` 表（红、黄、浅蓝、浅蓝） | H |
| 第 8 参 | 0 = 用 drawer 自带的白纹理 | M |

插件用同一调用画任意矩形：平移 = 左上角，顶点 = `(0,0)(w,0)(0,h)(w,h)`，z 用 0（文字也用 0）。M：z=0 是否受深度测试影响未验证，原版文字用 0。

`InstallHud` 核对：`0xC2FB0` 函数头、`0x804582 mov rcx,[EDF+0x2139A78]`、`0x8045B3 mov [rsp+20h],5`。

## 2. 文字（救援提示 `HudPlayer_RescueMessage` slot 3 = `0x808410` 的调用序列）

```
mgr = *(EDF+0x20B29C0)                         // 字体管理器
fd  (0x38 字节): +0x00..+0x14 = mgr+0x80..+0x94 的 6 个 float；+0x18 = 1；+0x1C = (0,0,0,1)；
                 +0x2C = 描边? (救援 2.5，多人名牌 1.7)；+0x30/+0x34 = mgr+0xB0/+0xB4
0x113A3F0(&fd, sx, sy)                         // 只写 fd+4 / fd+8：缩放（救援 1.33，名牌 0.666）
0x11517A0(mgr+8, &r)                           // 造渲染器 r（0x38 字节：+0x18/+0x20 是 shared_ptr）
0x113B4A0(&r, ctx, &fd)                        // begin：按 ctx+0x3A0/0x3A4 建正交投影
0x1139790(&r, float out[2], str, -1, false)    // 量尺寸：out = (宽, 高)，救援用 x -= 宽·0.5 居中
0x113C520(&r, ctx, m[16], rgba, str, -1)       // 画：m 第 4 行平移 = 文字左上角 (x, y, 0, 1)
0x113C6F0(&r, ctx)                             // end
0x1138630(&r)                                  // 释放 r（release shared_ptr）
```

| 项 | 可信度 |
|---|---|
| 调用顺序和参数（`0x808611/22/3F/A9/B5/BF`，`InstallHud` 逐个核对 call 目标） | H |
| 多人名牌 `0x804C90` 用同样序列，同一个 r 多次 begin→measure→draw→end | H |
| str 是 `const wchar_t*`，`-1` = 以 0 结尾 | H |
| 文字坐标与 §0 的视口像素同一空间 | M（正交投影取的是 ctx 的渲染目标尺寸，通常等于视口） |
| fd 各字段含义（字号 / 描边） | L，只按原版照抄 |
| 字体含 ASCII 字形 | M（所以插件只画英文和数字，不画中文/日文以免缺字） |

插件每次绘制：造一个 r，每行字 begin→measure→end 一次（排版）、begin→draw→end 一次，最后释放；先画全部四边形再画全部文字。
文字路径在 SEH 里，出异常就关掉文字（只剩血条），日志 `HUD the game's text path faulted`。

## 3. 数据来源（只在游戏线程读载具）

`HudSee`（`crew.cpp` 每台载具的输入钩子，游戏线程）只处理：驾驶座是 NPC（dummy rider）、队伍 = 玩家队 / 友军 2 / 无主 5、
离玩家 `VehicleHudRange` 内、不是潜舰。拷贝到 48 项定长表（每项一个 seqlock），绘制端只读表，不碰载具、不分配内存、不调 VirtualQuery。

| 字段 | 来源 | 可信度 |
|---|---|---|
| HP / 最大 HP | `+0x2F8` / `+0x2F4`（跟随者血条读的同一对字段） | H |
| 机炮 / 导弹余数 | 驾驶座武器（座位 `+0xC8` 列表、`+0xD8` 个数，持有者 `+0x10` → 武器）：`+0x6B0 == 1`（追踪）记导弹，其余记机炮，余数 `+0xBE8` | H（同 jet.cpp `ReadArms`） |
| 喷气机机种 / 燃料 / 无人机出击 / 撤离 | `jet.cpp JetHud`：`fuelMs − (now − bornAt)`；母舰的无人机无燃料；`sorties`；`mode == withdraw` | H |
| 呼叫直升机燃料 | `heli.cpp HeliFuel`：`leaveAt − now`，`leaving` 时 0 → 显示 RTB | H |
| 潜舰面板 | `subcarrier.cpp` 替身对象（船体、4 子系统的 HP）+ 每帧算好的修复剩余秒数 | H |

## 4. 未验证（需进游戏看）

- 文字大小（缩放 0.6 / 标题 0.75）是否合适、是否随分辨率缩放（M/L）。
- 面板位置：屏幕右侧、从 28% 高度往下；是否与原版 HUD（雷达、武器栏）重叠（L）。
- 每帧 ≤ 40 行字 + ≤ 60 个四边形的开销（原版名牌同量级，M）。
