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
