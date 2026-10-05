# EDF6 视距逆向（合并稿，2026-10-04）

两份纯静态逆向的合并稿。全部地址都是 EDF.dll 的 RVA（TimeDateStamp 0x678CCB46），**没有在实机上验证过**。

## 2026-10-05 追加：全局视距（src/view.cpp，ViewDistance）

- 用户要求把视距拉高。插件每帧把 `env+0x1A0`（FarClipZ）抬到 `ViewDistance`（默认 3000 m，范围 1000~10000，0 = 不动）。`env = *(*(base+0x20B2990)+0x258)`。
- 远景相机的起点 `env+0x1A4` 抬到 `ViewDistance-500`，和原版一样两个相机重叠 500 m；它的终点 `env+0x1A8` 不小于 `ViewDistance`。
- 每关加载时 env 被写回原版值，插件下一帧再抬上去。日志：`VIEW far clip 1000 -> 3000 m`。
- 未验证：帧率；深度精度（远近比从 10000 变成 30000）；雾的终点（FogEnd ≥ 5000，应该不挡）。

## 采用的方案（src/jet.cpp `FarRender`）

- 每架固定翼在 JetFrame（游戏线程）里每帧检查一次：
  1. 先校验模型组件 `vehicle+0xE40` 的 vtable 是 `base+0x176B9A8`。
  2. 节点 mask（`+0x20`）缺 bit26（0x04000000）时，调用游戏自己的 `setFarRender(node,true)`（0x11B3020）。这也是 SGO 的 FarRender / use_far_render 走的那个开关。
  3. 调用后再读一次 mask，确认 bit26 置上了。
- 失败处理：vtable 不符、置位没生效或调用时出错，这架机就关掉 far render，并记一条日志。
- 不改全场景的 LightEnv（FarClipZ 等）：不影响深度精度、性能和雾，也不碰别的物体。

**为什么是 bit26，不是放大 far plane：**
- 近景相机只画 0.1–FarClipZ（每个任务都是 1000 m），只渲染 mask bit25|bit27 的节点。
- 远景相机画 500–20000 m，只渲染 bit26 的节点。
- 载具节点创建时 mask 是 0x12000000，没有 bit26，所以飞出 1000 m 就不画了。
- 剔除距离 node+0x60 默认是 FLT_MAX；FogEnd ≥ 5000。这两项都不是原因。

**未验证 / 风险：**
- 实机效果：飞到 1000 m 以外能不能看见。
- 500–1000 m 两个 pass 会重叠，可能双重绘制。
- 远景 pass 里的光照和阴影可能不同（远景相机的 mask 不含 bit30）。
- 机体是否还挂着别的子渲染节点，需要分别置 bit26。如果实机看到机体缺块，就查这一条。

**备选（未实现）：** 每帧写 `env+0x1a0`，其中 `env = *(*(base+0x20B2990)+0x258)`。不要直接写 cam+0x2c，它每帧都会被 0x1230a0 覆盖。

---

# 报告一：bitmask 双相机（采用方案的依据）

目标：EDF.dll（TimeDateStamp 0x678CCB46），地址全为 RVA（基址 + RVA）。
置信度：H = 直接读反汇编/数据；M = 读法确定、语义推断；L = 猜测。
方法：纯静态（edfre.py + capstone + MAE/DSGO 解包），未启动游戏，未改任何文件。

## 一句话结论
喷气机不可见的主因很可能不是 far plane 或雾，而是 **Umbra bitmask 双相机分段**：
近景相机只画 0.1–1000 m（mask 0xa000000），远景相机画 500–20000 m（mask 0x4000000 = bit26）。
普通渲染节点创建时 mask = 0x12000000（bit25+bit28），**没有 bit26 → 超过 1000 m 就不画**（H 读法 / M 效果）。
SGO 里的 FarRender / use_far_render 正是给节点加 bit26 的开关，载具默认没开。

## 1. 远裁剪 far plane
| 项 | 值 | 置信 |
|---|---|---|
| LightEnv 默认初始化 | 0x1648d0 | H |
| env+0x1a0 FarClipZ | 默认 1000.0（全部 MAE 也都是 1000） | H |
| env+0x1a4 DistantViewNearClipZ | 默认 500（MAE 约 500） | H |
| env+0x1a8 DistantViewFarClipZ | 默认 20000（MAE 20000 / 35000） | H |
| 解析器 | DSGO 版 0x161830；旧键值版 0x15fe20 | H |
| env 指针 | env = *(*(base+0x20B2990) + 0x258) | H |
| 每帧拷贝 | 0x1230a0（0x1232b5 起）把 env+0x1a0/1a4/1a8 写进 4 个相机的 cam+0x2c/+0x30/+0x34；相机槽 = *(base+0x20B2958)+0x4d8+i*0x188（shared_ptr） | 拷贝 H / 每帧 M |
| 相机初始化 | 0x118afc0：+0x20/+0x24 FOV=π/4，+0x28 near=0.1，+0x2c far=1000，+0x30=500，+0x34=20000，+0x38=0x100 | H |
| 投影构建 | 0x118dff0（唯一调用者 0x1197b50）：近景 Umbra 相机 cam+0x70 用 (+0x28,+0x2c)；远景 Umbra 相机 cam+0xf0 用 (+0x30,+0x34) | H |
| 相机 mask | 设置函数 0x11d4600（包装对象 [+0x40] = Umbra::Camera*）：近景 0x0a000000（bit25\|bit27），远景 0x04000000（bit26），阴影/其他 0x40000000（bit30） | H |

最安全的写入点：env+0x1a0（源头，每帧被拷到相机）。**不要写 cam+0x2c**，会被 0x1230a0 每帧覆盖（M）。

## 2. 物体距离剔除 / LOD / 可见阈值
渲染节点（Umbra Object 包装）：
| 项 | 值 | 置信 |
|---|---|---|
| 构造 | 0x11b2400：vtable = base+0x176b9a8；+0x10 = Umbra::Object*；+0x20 = mask（构造时 0x10000000）；+0x60 = 剔除距离²，默认 FLT_MAX（不剔除） | H |
| 创建 | 0x11b27d0 把 mask 设为 0x12000000（bit25+bit28）→ 近景可见、远景不可见 | H |
| setFarRender(node,bool) | **0x11b3020**：置/清 bit26 后调 Umbra Object::setBitmask | H |
| 其他 setter | 0x11b3490 bit30（阴影）；0x11b34c0 bit29；0x11b2640 bit28（脚本绑定）；0x11b24a0 低位 mask（脚本） | H 读法 / 语义 M |
| setCullDistance 调用者 | 0x10ad30、0x11b2560（脚本绑定）；0x16ee60（sqrt+5）；0x965db0（far=200000）；都同步写 node+0x60 = far² | H |
| Umbra 导入 thunk | Object::setBitmask 0x12d60c8；setCullDistance 0x12d609e；setRenderCost 0x12d6098；set(Property,bool) 0x12d60b6；Camera::setBitmask 0x12d6140；Camera::setFrustum 0x12d6134 | H |
| 谁开 bit26 | 子弹/特效 SGO `use_far_render`：0x240c00（节点 obj+0xe90，标志 obj+0xf88）、0x239210、0x23dda0；generator 0x3ff8a0（标志 rdi+0xe23）。角色 SGO `RenderOptions.FarRender`：解析器 0x314ad0，0x315060 调 0x11b3020(obj+0x8a0)。还有 UfoCarryer（节点 +0x650）、TimeShip 等 | H |
| ReductionDistance | 0x315193 读取，含义未查 | 未查清 |
| LOD | 0x126ba0 / 0x151240 疑似，未查清 | L |

默认剔除距离 FLT_MAX，不是本问题原因（H）。

## 3. 雾
| 项 | 值 | 置信 |
|---|---|---|
| env+0x150 FogBegin | 默认 300 | H |
| env+0x154 FogEnd | 默认 8000；MAE 在 5000–25000 | H |
| env+0x158 FogCurve | 曲线参数 | H（语义 M） |
| env+0x130 | 雾色 | M |
| SceneEffect_Fog 脚本函数 | 字符串存在，写入路径未查清 | 未查清 |

FogEnd 通常 ≥ 5000，对 1–2.5 km 的物体只是部分淡化，不会完全隐藏（M）。

## 4. 喷气机（Vehicle506_Helicopter）渲染节点定位
- 工厂 0x61b570：分配 0x2030 字节 → 调 0x64e3d0 → 0x64e41a 调 VehicleBase 构造 0x629450 → 写 vtable 0x17DB238（H）。
- VehicleBase 构造 0x62958a：`lea rcx,[r14+0xe40]; call 0x6b8740`，即模型组件位于 **vehicle+0xe40**（H）。
- 0x6b8740 是模型组件构造；角色 FarRender 路径直接把模型组件地址（obj+0x8a0）传给 0x11b3020，说明模型组件起始处就是渲染节点（M）。
- 运行时自检：*(vehicle+0xe40) 应等于 base+0x176b9a8（节点 vtable）。如果不等，就在对象内扫描等于该值的 qword 来定位（M/L：派生节点的 vtable 可能不同）。
- 未确认：模型组件是否还挂有子节点（多 mesh / 部件），子节点需要单独开 bit26（L）。

## 5. 推荐插件方案
**(a) 首选**：喷气机生成后（以及模型重建后）调用
`((void(*)(void*,bool))(base+0x11b3020))(vehicle+0xe40, true)`
给节点加 bit26，让远景相机（500–20000 m）也画它，近景行为不变。
- 读法 H，效果 M。
- 副作用：500–1000 m 段可能被近景和远景各画一次；远景 pass 的深度精度、光照或阴影可能不同（远景相机 mask 不含 bit30）；节点重建（换模型、重生）后需要重新设置；必须在游戏线程调用（Umbra 非线程安全，M）。
- 校验：先比对 *(vehicle+0xe40) == base+0x176b9a8，再调用；调用后 *(uint32*)(vehicle+0xe40+0x20) & 0x04000000 应为非零。

**(b) 备选**：每帧写 env+0x1a0（FarClipZ），比如改成 3000。
- env = *(*(base+0x20B2990)+0x258)，空指针要跳过。
- 副作用：近景 z 精度下降，可能出现 z-fighting；绘制开销增加；和远景段（500–20000）重叠变多；天气或环境切换会重新加载 env，需要持续重写。影响全场景，不只喷气机。

**(c) 不要**直接写 cam+0x2c / +0x34，它们每帧被 0x1230a0 覆盖。

**(d)** 不必改剔除距离 node+0x60，默认 FLT_MAX。

**(e) 雾（可选）**：喷气机发灰或发淡时调大 env+0x154 FogEnd，副作用是全场景雾变淡。

## 6. 未查清 / 找不到
- ReductionDistance（0x315193）的语义。
- LOD 切换函数（0x126ba0 / 0x151240 只是候选）。
- PLAYER_CAMERA_FAR、SceneEffect_Fog 的写入路径；0xbb5b0 附近那组常量。
- 载具模型组件内部是否还有多个子渲染节点。
- 所有"效果"层面的结论都没有实机验证（按约束未启动游戏）。

---

# 报告二（附录）：far plane 链路与逐对象剔除

- 目标：EDF.dll（TimeDateStamp 0x678CCB46），文中地址均为 **RVA**。
- 方法：纯静态（tools/edfre.py：capstone + pefile，按 .pdata 取函数边界）。未运行游戏。
- 结论可信度标注：**已核实** = 反汇编直接可见；**未核实** = 推断，需运行时确认。

## 0. 先更正上一会话的结论

上一会话说「0x1230a0 写相机裁剪参数」，随后我又把这条线索否了，**否掉是错的**。
当时只看了函数开头。写入在函数后半段（0x12329d 之后），见第 2 节，已核实。

## 1. 总体链路（已核实）

```
LightEnv JSON 的 FarClipZ / DistantViewNearClipZ / DistantViewFarClipZ
   └─ 0x15fe20 解析 → LightEnv +0x1a0 / +0x1a4 / +0x1a8   (默认值由 0x1648d0 构造器写入：1000 / 500 / 20000)
        └─ 0x1230a0 每次应用环境时复制到 4 个渲染相机 → cam +0x2c / +0x30 / +0x34
             └─ 0x118dff0 每帧构建视锥：
                  near=cam+0x28, far=cam+0x2c → 投影矩阵 (0x71f70) + 近景 Umbra 视锥 (wrapper cam+0x70)
                  near=cam+0x30, far=cam+0x34 → 远景 Umbra 视锥 (wrapper cam+0xf0)
```

另有**独立的逐对象剔除距离**：Umbra `Object::setCullDistance`（thunk 0x12d609e），far² 缓存在对象包装 +0x60。见第 4 节。这和投影 far 是两套东西。

## 2. 结构与字段

### 2.1 LightEnv（光照环境，与雾参数在同一对象中）

| 偏移 | 字段 | 来源键 | 默认（0x1648d0） | 证据 |
|---|---|---|---|---|
| +0x150 | FogBegin | `FogBegin` | — | 0x160d44 |
| +0x1a0 | 近景 far clip | `FarClipZ` | **1000.0** (0x447a0000) | 写 0x160bc1；默认 0x1649ff |
| +0x1a4 | 远景 near clip | `DistantViewNearClipZ` | **500.0** (0x43fa0000) | 写 0x160cc3；默认 0x164a09 |
| +0x1a8 | 远景 far clip | `DistantViewFarClipZ` | **20000.0** (0x469c4000) | 写 0x160c42；默认 0x164a13 |
| +0x1ac | 未知，0x1230a0 紧接着读它 | ? | ? | 0x12335d，**未核实** |

- 0x1648d0 是 LightEnv 的默认初始化，调用点在 0x15fdfb（就在解析函数 0x15fe20 前面）、0x160381、0x161d48，**已核实**。
- 0x15fe20 的写入模式：`call 0x52790` 按名字查键，返回 -1 表示键不存在，跳过不写，保留默认值。节点类型 3（double）走 `cvtsd2ss`；类型 2 原样复制 dword；其他类型走 `cvtdq2ps`。解析函数里没有任何钳位或 min/max。
- 0x161830 中的 `_farClipZ` / `_distantFarClipZ` / `_distantNearClipZ`（写入点 0x16251c / 0x162646 / 0x162770）写同样的偏移，推测是过渡/淡变事件的目标值；0x163a50 同时读两个对象的 +0x1a0..+0x1a8，推测做插值。**未核实**。
- 地图数据可能把近景 FarClipZ 设得比 1000 大，具体值随地图而定。默认值本身是 1000 m。

> 0xbb5b0 里还有一组常量 50000 / 49000 / 90000 写到 +0x1a0/+0x1a4/+0x1a8，这个函数**没有直接调用者**（可能经虚表调用），所属对象**未核实**，不要拿它当 LightEnv 的默认值。

### 2.2 渲染相机（renderer 内的 4 个相机槽）

- renderer 全局指针：`[rip+0x1f8f67e]`（相对 0x1232eb 一带的 RIP 计算），**按指令地址换算，未单独标注绝对 RVA**。
- `0x1195be0(renderer, i)` 的实现是 `return renderer + 0x4d8 + i*0x188`，得到一个 weak_ptr 槽，解引用后就是相机对象，i 取 0..3。

| 偏移 | 含义 | 证据 |
|---|---|---|
| +0x28 | 近景 near | 0x118e06a |
| +0x2c | 近景 far（= 投影矩阵 far） | 0x12330e 写；0x118e06a 读 |
| +0x30 | 远景 near | 0x123318 写；0x118e0dc 读 |
| +0x34 | 远景 far | 0x123313 写；0x118e0dc 读 |
| +0x38 | 非 0 时走阴影级联循环 | 0x118e1xx |
| +0x70 | 近景 Umbra Camera 包装 | 0x118e0c6 |
| +0xf0 | 远景 Umbra Camera 包装 | 0x118e0fa |
| +0x178 | 级联用的包装数组 | 级联循环 |
| +0x1a0 | 投影矩阵 | 0x71f70 输出 |
| +0x220 | view / cameraToWorld 矩阵 | 0x11d46d0 |

Umbra 相机包装（0x11d44d0 创建，调用 setProperties(0xb)）的布局：+0x00..0x30 是 cameraToWorld，+0x40 是 `Umbra::Camera*`，+0x58..+0x70 是视锥（left, right, top, bottom, near, far, type）。

### 2.3 0x1230a0 关键片段（已核实）

```
0x12329d: mov   rsi, [r14+0x258]        ; LightEnv*
0x1232a4: movss xmm6, [0x1c36994]       ; 1.0（rsi==0 时的值，但随后直接跳过整段）
0x1232af: je    0x1233f5
0x1232b5: movss xmm6, [rsi+0x1a0]       ; FarClipZ
0x1232bd: movss xmm7, [rsi+0x1a8]       ; DistantViewFarClipZ
0x1232c5: movss xmm8, [rsi+0x1a4]       ; DistantViewNearClipZ
          ; for edi in 0..3:
          ;   rcx = [rip+0x1f8f67e] (renderer); edx = edi; call 0x1195be0
0x12330e: movss [cam+0x2c], xmm6
0x123313: movss [cam+0x34], xmm7
0x123318: movss [cam+0x30], xmm8
0x12335d: ... [rsi+0x1ac]               ; 未核实
          ; [rsi+0x2d8]!=0 时把 [rsi+0x2e0..0x328] 拷到 [[rip+0x1f8f5df]+0x280..]（未核实语义）
```

### 2.4 0x118dff0 视锥构建（已核实）

```
aspect = cvt([rdx+8]) / cvt([rdx+0xc])
0x118e06a: near=[r14+0x28], far=[r14+0x2c] → call 0x71f70      ; 投影矩阵 → r14+0x1a0
0x118e089: xmm3=[r14+0x28], [rsp+0x20]=[r14+0x2c]; call [vtbl+0x50]   ; 生成 Frustum
0x118e0c6: call 0x11d46b0 (wrapper=r14+0x70)                    ; 近景 Umbra setFrustum (thunk 0x12d6134)
0x118e0dc: xmm3=[r14+0x30], [rsp+0x20]=[r14+0x34]; call [vtbl+0x50]
0x118e0fa: call 0x11d46b0 (wrapper=r14+0xf0)                    ; 远景 Umbra setFrustum
0x118e10a / 0x118e11d: call 0x11d46d0                          ; setCameraToWorld (thunk 0x12d6128)
[r14+0x38]!=0: 级联循环，表 0x1feee80/0x1feee90/0x1feeea0，call 0x118bb40，0x11d4610 setCullPlanes
```

Umbra 包装函数 0x11d4610 / 0x11d46b0 / 0x11d46d0 都只有直接调用，不在虚表里。

## 3. 两套 pass 的含义（部分推断）

- 近景：投影 far = FarClipZ（默认 1000 m）。几何超过这个距离会被深度裁掉，近景 Umbra 视锥也随之截断。
- 远景：near 500 / far 20000，有独立的 Umbra 视锥，渲染进入远景 pass 的物体（地形/远景层）。
- `use_far_render`（xref：0x239210、0x23dda0、0x240c00、0x3ff8a0）、`FarRender` / `ReductionDistance`（0x314ad0，引用 0x314f1a、0x315193）、`UfoCarryer_EnableFarRender`（0x4f5a50）表明：**有些物体需要显式打开 far render 才会进远景 pass**，例如飞船母舰（UfoCarryer）。普通载具、敌人、NPC 默认只进近景 pass，这条**未核实**。
- 推论：**固定翼飞到 1000 m 之外就看不见，最直接的原因是近景投影 far = FarClipZ（默认 1000）**。远景 pass 能画到 20 km，但固定翼大概率不在远景 pass 里。这是与现象最吻合的解释，但需要运行时确认。

## 4. 逐对象剔除（Umbra cull distance）

Umbra 对象包装：+0x10 是 `Umbra::Object*`，+0x60 是 far²。`setCullDistance(obj, near, far)` 的 thunk 是 **0x12d609e**。调用者：

| 调用者 | far 来源 | 状态 |
|---|---|---|
| 0x10ad30 | 从参数流读 near/far，写 `[rbx+0x60]=far²` | 已核实 |
| 0x11b2560 | vec4 的 .z 当 near、.w 当 far | 已核实 |
| 0x16ee60（调用点 0x16fc58） | 只在 `[rbp+0x248]!=0` 且 r²>0 时设置。r² = max(`[model+0x60]`, 第 3 个参数)，far = sqrt(r²) + **5.0**（常量 0x1765a38），near=0，然后写 `[wrap+0x60]=far²` | 公式已核实；`[model+0x60]` 的语义**未核实**（像模型数据自带的剔除距离平方） |
| 0x965db0（"Model" 路径） | far = **200000.0**（0x1812010），`[rbx+0x60]=4e10` | 已核实 |

0x16ee60 片段：

```
0x16f45a: movss  xmm9, [rbx+0x60]
0x16f460: movaps xmm14, xmm9
0x16f464: maxss  xmm14, xmm15          ; xmm15 = 入参 xmm2
0x16f469: xorps  xmm15, xmm15          ; near = 0
...
0x16fbeb: cmp    byte ptr [rbp+0x248], 0
0x16fc1f: comiss xmm14, xmm15 ; jbe skip
0x16fc31: sqrtss xmm6, xmm14
0x16fc44: addss  xmm6, [0x1765a38]     ; +5.0
0x16fc4c: movaps xmm2, xmm6            ; far
0x16fc4f: movaps xmm1, xmm15           ; near = 0
0x16fc53: mov    rcx, [r12+0x10]       ; Umbra::Object*
0x16fc58: call   0x12d609e             ; setCullDistance
0x16fc5d: mulss  xmm6, xmm6
0x16fc61: movss  [r12+0x60], xmm6      ; 缓存 far²
```

其他 Umbra thunk：processVisibility 是 0x12d6116（调用者 0x11b5a70、0x11b5b30），Object::create 是 0x12d60b0（调用者 0x11b27d0，0x16fc10 也调了它）。

**固定翼走哪条注册路径、它的对象有没有设 cull distance，目前未核实。** 如果固定翼走 0x965db0 的 Model 路径，cull distance 是 200 km，不构成限制，瓶颈就只剩投影 far。如果走 0x16ee60 且模型数据里有剔除距离，那么 sqrt(值)+5 可能就是它消失的距离。

另外 shader 常量名里有 `g_culling_distance` / `g_fadeout_distance` / `g_bg_fade_distance`，说明可能还有 shader 侧的距离淡出。`m_show_distance`（0x5c92d0）和 `_lod`（0x126ba0、0x151240）都没看，**未核实**。

## 5. 推荐修改点

### A（首选，最小改动）：放大 LightEnv 的近景 FarClipZ

- 改法：每次环境应用后（或每帧），把**当前 LightEnv 的 +0x1a0** 从 1000 改成例如 3000。LightEnv 指针取 `[r14+0x258]`，r14 是 0x1230a0 的 this（第一个参数，函数入口处 rcx 被保存进 r14，**入口寄存器搬运未逐条核实**）。
- 实现方式 1：detour **0x1230a0**，先把 `[[rcx]+0x258]+0x1a0`（取址方式见上）改大，再调原函数。原函数会把新值复制进 4 个相机。
- 实现方式 2：不挂函数，直接每帧对 4 个相机写 `cam+0x2c = max(cam+0x2c, X)`，相机地址是 `renderer + 0x4d8 + i*0x188` 解引用 weak_ptr。缺点是 0x1230a0 每次重跑都会覆盖回去，所以必须每帧写。
- 0x161830 / 0x163a50 的环境过渡会改写 +0x1a0，因此**每帧取 max 比一次性写更稳**。
- 建议值：2000–4000，不要直接拉到 20000。
- 副作用：
  1. 深度精度：近景 near 较小时，far/near 比值增大，远处更容易 z-fighting。需要先确认 EDF6 是否使用 reversed-Z（**未核实**）。
  2. 与远景 pass 重叠：远景从 500 m 开始画，近景 far 拉大以后，500–X 区间两个 pass 都会画，可能出现双重绘制和地形闪烁。可以把 +0x1a4（远景 near）一起调到接近新的 far，**但远景 near 改动的视觉效果未核实**。
  3. 性能：近景 Umbra 视锥加长，绘制的物体变多。
  4. 雾：FogEnd 通常和 FarClipZ 配套。far 拉大后，如果雾没有盖住，裁剪边界会露出来。

### B（如果 A 之后固定翼仍消失）：处理逐对象剔除

- hook **0x12d609e（setCullDistance thunk）**：当调用方是固定翼对象时，把 far 改大，或者统一取 `far = max(far, X)`。同时要同步把包装对象 +0x60 改成 X²，否则引擎侧用 +0x60 做的二次判断（如果有）会和 Umbra 的值不一致。
- 更窄的改法：只在 0x16fc44 那条路径（+5.0 那一行）动手，影响面最小。
- 风险：thunk 是全局的，所有对象都会受影响。需要用对象指针或调用方返回地址过滤，否则性能开销会全局上升。

### C（备选）：让固定翼进入远景 pass

照 `UfoCarryer_EnableFarRender`（0x4f5a50）的写法，给固定翼打开 far render 标志，让它在 20 km 的远景 pass 里被画出来。需要先看懂 0x4f5a50 写的是哪个对象的哪个标志位，**未核实**，留作后续。

## 6. 尚未完成 / 未核实清单

- 固定翼对象的实际注册路径（Model 路径 0x965db0，还是 0x16ee60，还是其他）。
- 0x16ee60 中 `[model+0x60]` 的语义。
- `use_far_render` / `FarRender` / `ReductionDistance` 的具体字段位置。
- shader 侧的 `g_culling_distance` / `g_fadeout_distance` 由哪里赋值。
- LightEnv +0x1ac、+0x2d8 / +0x2e0 段的语义。
- 是否使用 reversed-Z（决定拉大 far 时的深度精度风险）。
- 0xbb5b0（50000/49000/90000）属于哪个对象。
