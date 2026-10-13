# 房间 / HQ 菜单的 UI、输入、图标、布局（静态逆向，2026-10-12）

EDF.dll TimeDateStamp 0x678CCB46，RVA。【证实】= 反汇编 / 数据直接读出；【推断】= 未逐条验证。**未实测。**

## 1. 输入阻断

- UI 每帧 `0x119D270(sys, UiInput*, dt)`（调用方 0x7058F7），输入对象 `*(EDF+0x20B28A0)+0x5AE8`。每个顶层对象经 `0x11A45C0` 调槽 1 `OnUpdate(this, ctx)`。【证实】
- ctx：`+0` 原始 UiInput*；`+8` 有效 UiInput*（对象字节 +0x28 非 0 才填，否则空，0x11A4540）；`+0x10` float dt。【证实】
- 读输入的地方都只读 `ctx+8`：房间 OnUpdate 0x8FE5BE、HUiLayout 基类 0x84B490（焦点 / cancel / 按钮回调）、组件树 0x7FAE70、`HUiBase::GetInput` 0x8DAD00。HQ OnUpdate 0x89F740 = 0x84B490 + 刷新描述。【证实】
- **做法**：OnUpdate 包装里拷一份 ctx（0x18 字节），副本 `+8` 置空，用副本调原函数。等于游戏原生的「本画面不是输入拥有者」状态；网络保活、成员重建（+0x940）、状态发布（0x901100）、动画、聊天照常；鼠标一并挡住。
- 关菜单的键：松开之前继续置空，否则被当成 cancel。【推断】
- MainFrame 0x8C12B0 也调 0x84B490；若底栏引导条仍响应鼠标，同样处理。【L】键盘文字聊天是否另走一路未核。

## 2. 子画面检测

- 通用判据：置空之前用原 ctx 调 `0x8DAD00(this, ctx)`，非空 = 显示菜单，空 = 隐藏（ctx+8 空 / 过场 0x7E4E30 / 布局 +0x111==0 / +0x184==0 / +0x178 弱引用存活）。房间按钮是否响应用的就是它。【证实】
- HQ 子窗口经 CallWindow 0x84EEA0（包装 0x89DA40），弱引用存 HUiLayout +0x5E8 / +0x5F0（写入 0x84F3B1）；武器 / 颜色选择是兵种选择的孙窗口。【证实 / 推断】
- 房间状态 +0xAC0：装备 1、任务 2、房间设置 3、选项 5、开打 6；履历写 0，不能单独作判据。Coop 的 +0xAF0 / +0xB00 是消息对话框弱引用。【证实】

## 3. 图标

AI 武器自带 `WeaponIcon`（`app:/weapon/icon/<名>`）【证实】：

| 类 | 图标 |
|---|---|
| rifle | AF14.dds |
| flame | flamethrower.dds |
| rocket | stingray.dds |
| shotgun | buffalo.dds |
| sniper | srMMF.dds |
| lance | lazerlance01.dds |
| laser | LAZR.dds |
| monster | monster01.dds |
| izuna | izuna.dds |
| thunderbow | thunderbow.dds |
| cannon | canon.dds（区分可改 h_cannon_614_heavy01.dds） |
| midcannon | canon.dds |
| pilebanker | pilebunker.dds |
| fshotgun | canon.dds（区分可改 shotcannon.dds） |

重装取主武器，跳过盾与 DASH（`icon/none` 不存在）。

载具武器的 `WeaponIcon` 全是不存在的 `icon/none`【证实】，借玩家武器图标（文件已确认在 Root.cpk）【推断映射】：

| 挂载 | 图标 |
|---|---|
| AP | h_cannon_apfsds01.dds |
| HE | tank01.dds |
| GLM | stingray.dds |
| AAM_S | s_missile_617_falcon01.dds |
| AAM_M | s_missile_emeraude02.dds |
| AAM_L | missile_titania.dds |
| AGM | s_missile_airtortoise02.dds |
| AGM_L | missile_arm.dds |
| MK82 | pd609_drone_bombbig.dds |
| RKT | hornet01.dds（备选 mlra.dds） |

## 4. 文件重定向与皮肤

- EDFModLoader v1.0.10（`winmm.dll`）挂通用文件打开 0x741C0，`app:/` → `/cri_bind/` → `./Mods/<相对路径>`，只看 Mods 是否有文件，新文件名同样加载（本 mod 的 `Mods\OBJECT\EDF6VC_*` 已实证）。【证实】
- 直接引用现成皮肤：`ColorChipBox_skin.sgo`（HQ 与房间资源组预载）、`HUDIconTexture_skin.sgo`（GLOBAL，`Texture`，`0x7F63E0` 换贴图）。不做新皮肤文件。
- 换到未加载的图标时同步还是异步加载：未核。【L】

## 5. 布局元素 `[type, skin, pos, area, flags, hcoord, vcoord, props]`

- 两个整数是 `hcoord` / `vcoord`（0x84CD56 / 0x84CD90，经 0x8468B0；存 +0xC8 / +0xCC）；0 左 / 上、1 居中、2 右 / 下。【证实 / 推断】
- flags 按位或（0x840430）【证实】：
  - 1 水平居中；2 垂直居中；
  - 4 接前一兄弟右边；8 接前一兄弟下边；
  - 0x10 / 0x20 x / y 与前一兄弟对齐；
  - 0x10000 右锚定；0x4000 填满父元素。
- flags【推断】：0x100 纵向列表控制器（0x83C960，按钮组 `[256,2048]`）；0x80 横向 / 网格；0x8000 不挂滚动条；0x800 疑似裁剪。
- 建议：新元素挂在 BaseFrame 下，绝对 pos，flags `[]`，两整数 0,0。显隐用 +0x1F8。tree 顺序 = 绘制顺序。【推断】

## 6. 布局对象

`0x839FD0` 第一个参数直接传 HUiRoom / HUiHQMain 的 `this`（房间 0x8FEF88；HQ 0x89F21C 结果存 +0x7F0，OnUpdate 0x89F7B5 取用）。补丁元素必须在 `layout_tree` 里才会实例化。【证实】

## 7. 阶段 4 补充（2026-10-12，静态反汇编，`src/room_ui.cpp` 用到的部分）

- **手柄表** `EDF+0x2136530`：两个槽相隔 0x4A0，每个按键一个通道指针；写入 `0x1182A00`（+0x48 模拟量、+0x4C 按住、+0x4D=0），
  循环 `0x1188270`（基址 `lea r13` 在 0x1188315），从 XInput wButtons 取位。【证实】
  通道：0x50 / 0x58 / 0x60 / 0x68 上下左右，**0x70 A**（位 0x1000，0x1188A4D）、**0x78 B**（0x2000），0x80 X、0x88 Y、0x90 Back、
  0x98 Start、0xA0 LB、0xA8 RB、0xB0 L3、0xB8 R3，0x40 / 0x48 扳机；A 是下方键（XInput 布局，无 A/B 互换）。【证实】
  +0x4C 是「按住」（每帧按当前位重写），+0x4D 非 0 = 被抑制（`0x1183D60` 为假时全部置 1），读取方当作没按。【证实】
- **按名取组件**：`0x839600(layout, const wstring*)` → 第一个下标或 −1；`0x839560(layout, shared_ptr<Component>* out, idx)` 不转型；
  `0x8859C0(layout, shared_ptr<HUiTextField>* out, idx)` 动态转型，不是 TextField 时 out 为空；`0x839FD0(layout, out, const wstring*)`
  = 0x839600 + 0x839560。游戏的标准写法 0x886800：0x839600 → 0x8859C0 → `0x863690 SetText(tf, 字面文字)`（[tf+0x228]==0 时直接返回）。【证实】
- **色块**：组件的皮肤是 weak_ptr，在组件 +0x98（指针）/ +0xA0（控制块）。HQ 调色板 0x88014D：复制它、`lock inc [ctrl+0xC]`（弱引用），
  调 `0x824D40(&weak副本, L"ColorChipBox", const std::string* "color"（窄字符串）, const float4*)`，被调方释放这份弱引用；
  内部转型 Skin_Model、按名找模型与参数、拷最多 16 字节、置脏 +0x48。浮点 RGBA 0..1【推断】。【证实（调用约定）】
- **贴图**：`0x953D80(shared_ptr<Skin_Texture>* out, shared_ptr<Component> 按值)`（被调方释放传入的副本：调用前 uses+1），
  `0x7F63E0(Skin_Texture*, const std::wstring* path) → bool`（先拷贝路径，替换 `%LOCALE%`；失败返回 0）。路径 `app:/...`。
  游戏写法 0x8C4256（`ImgBoard`）；HUD 武器图标 0x836B20 先判文件存在。加载后立刻读宽高：同步绑定【证实】，GPU 上传完成【推断】。
  染色：+0xF0..+0xFC 与 +0x100..+0x10C 按 +0xE4 插值，要两组都写。
- **显隐** +0x1F8：0x839FD0 返回的就是布局代码写 +0x1F8 的那个 Component（武器栏经 0x83A140 同一路径）；构造函数置 1（0x7E999C）。【证实】
- **TextField 字色** +0x2B0..+0x2BC（float4 RGBA，布局属性 `font_color`，默认 1,1,1,1）；绘制每帧乘上（0x7F890C），直接写即生效；
  游戏高亮行就是这么做的（0x8BC68C）。【证实】
- **GetInput 0x8DAD00**：返回 ctx+8；过场、[this+0xD8]+0x111==0、[this+0x184]==0、+0x178 弱引用存活、`0x7EBFD0(this+0x160)` 假时返回空。
  ctx+0x10 = dt（0x84B716）；**ctx+0 不一定是原始 UiInput**（树分发 0x7FB0DD 给子节点的 ctx 是 {&this+0x170, 输入或空, dt}），不要依赖它。
- **布局属性名**（0x83E000–0x852000 引用的宽字符串）：flag_layout、scroll_bar_info、callback、init_args、…、pos、area、hcoord、vcoord、
  font_size、line_space、font_border_width、font_border_color、font_color、font_flag、sgo_path、result_callback、hide、disable 等。
  **没有「初始隐藏」属性**：工厂侧对 +0x1F8 的唯一写入是构造函数的 =1；`disable`=1 让构建函数跳过该元素（不创建，而不是隐藏）。
- `text` 是文字表键（0x846E30 → 0x7A3500），`text_dr` 是字面文字（0x8471CC）；SetText 不查表。
- **EDF6Coop 对房间槽的要求**：Coop 安装时要求 0x180AC48 指向 0x8FDFE0，否则整个 Coop 拒绝（multislot `plugin.cpp` SlotTargets）。
  EDFModLoader 按文件名顺序加载插件（ModLoader.log：EDF6Coop 先于 EDF6VehicleCrew），所以本插件转调 Coop 的钩子。

## 8. 阶段 4b 补充（2026-10-12，静态反汇编 + Root.cpk 数据）

- **Skin_Texture**（vtable 0x17F6228）：构造 0x7F5C20 置 +0xF0..+0xFC、+0x100..+0x10C 为 1.0（0x7F5CD2）；槽 3 绘制 0x7F7040：
  颜色 = lerp(+0x100, +0xF0, +0xE4) × 组件颜色（rsi+0xB0..），`rax=[rdi+0A0h]` 为空或 `[rax]` 为空时直接收尾（0x7F7190 → 0x7F725A），
  即**没绑定贴图就什么都不画**；槽 4 状态切换 0x7F7270：状态路径为空不调 0x7F63E0（0x7F73E3），并置 +0xE4=1.0、+0xE8=0。【证实】
- `HUDIconTexture_skin.sgo` / `LoadingImage_skin.sgo` / `SoldierTypeTexture_skin.sgo` 的 default 路径都是空；`MissionStoryTextBG_skin`
  是 alpha 0 的 Solid（备选：没用）。【证实（数据）】
- Root.cpk / DX11.cpk / Chunk01 / Chunk02 里没有纯色独立 DDS；`UI/CHATSHORTCUT_CURSOR.DDS` 是 32 位无压缩（flags 0x81007、
  像素格式 0x41 / 32 位 / BGRA 掩码、caps 0x1000、无 mip），本 mod 的白贴图用同一格式。【证实（数据）】
- `SetText(tf, nullptr)` 用空串（0x86369F `lea rdx, L""`）；原版布局 60 个 TextField 是 `text_dr ''`。【证实】
- **HQ 调色板**：`DEFAULTPACKAGE/CONFIG.SGO` `SoldierInit[class][5][0]` → `App:/DefaultPackage/CustomColor<Class>.sgo`（0xE07D0）；
  `CustomColor` = 12 × {主, 副} × RGBA；加载器 0xDF1B0（`L"CustomColor"` 0xDF3D5）拷到 GS +0x6FB0 + p*0x3E60 + ((class*4+outfit)*2+ch)*0xC4，
  选中下标 +0x7070 / +0x7134，初值 = outfit；`HUiHQColorSelect`（vtable 0x1800C40）槽 1 0x881060 拷 12 项到 this+0x790+ch*0xD0，
  槽 2 0x880F50 每通道 12 个色块（0x880FFE `call 0x880010` → 0x824D40，`cmp rbx,0Ch` 0x88100A）。第 4..11 项各兵种相同。
  自由调色 `HUiHQColorMake`（vtable 0x1800908）改写 12 个槽之一（0x87D900）。【证实；颜色按 sRGB 换算为推断】
