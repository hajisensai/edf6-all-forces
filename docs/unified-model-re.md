# 统一模型文件：战斗与菜单渲染器读同一个 SGO（逆向笔记）

EDF.dll TimeDateStamp 0x678CCB46，地址都是 RVA。证据等级：H = 反汇编 / 数据直接看到；M = 由调用关系与数据推断、未实机；L = 猜测。

## 1. 结论

- 菜单渲染器（UiHQ_3DModelView 的模型槽加载 0x93AA00）与战斗加载器**都接受两种容器**：0x93ABDC 调 0x53FA0 认 DSGO 魔数，DSGO 包成 tag 2 的 doc variant（0x93AC12），否则 0x52720 解析经典 SGO（tag 0），variant 存在 model+0x70。（H）
  所以「战斗模板塞进菜单不显示 / 崩」不是容器问题，是**键**的问题：缺菜单要的键，或同名键两边要的值不同。
- 两边读的键（宽字符串 xref，H）：

  | 键 | 战斗读者 | 菜单读者 |
  |---|---|---|
  | `animation_model` | 各构造（士兵 / 载具） | 0x93AD1E，0x93AD30 调 0x6BF380 |
  | `change_color` | 无（ASCII "change_color" 0x185020 是 shader 参数名，另一命名空间） | Draw 0x93C370（0x93C87C） |
  | `soldier_color` | 0x5914C0 | 无 |
  | `soldier_weapon_slot` | 0x592FA0 | 0x93FBC4（0x93F910 内）、0x94019C（0x940100 内） |
  | `soldier_weapon_grab_adjust` | 0x58D020 | 0x93AD88 |
  | `random_animation` | 只有 0x3AFC10（FlyingDyno 类，与士兵 / 载具无关，M） | 0x93D640（0x93D6B4） |
  | `human_type` | 0x56A5C0 | 无 |

- **唯一的值冲突是 `animation_model`**（H，数据）：菜单要 MenuObject 的 CAS（`app:/MenuObject/EDF6ArmySoldier.cas`，anmgroups `weapon` / `main`，20 个 clip，含 `_menu1..3`、`assault_menu*`、`t_menu_*` 等菜单姿势）；战斗模板是 `app:/Object/EDF6ArmySoldier.cas`（anmgroups `weapon` / `appeal` / `main`，638 个 clip，一个菜单姿势都没有）。两边的 `soldier_weapon_slot` / `soldier_weapon_grab_adjust` 形状相同（`[[[0,1],"weapon",0,0]]` / `[[0,0,0],[0,0,0]]`），可共用。
- 因此一份文件 = 战斗文件 + `menu_animation_model`（菜单版 `[[archive, model], 菜单 CAS, MAB]`）+ `change_color` + `random_animation`（+ 载具补 `soldier_weapon_slot` / `soldier_weapon_grab_adjust`）。新增的键战斗一个都不读，所以战斗读的每个键逐字节不变（tests/menu_preview_test.py 校验）。
- 冲突在代码侧解决：插件把 0x93AD30 的 `call 0x6BF380` 改指到自己的函数（`edf::RedirectCall`，经 near thunk），文档里**有** `menu_animation_model` 就把键名换成它，没有（原版 MENUOBJECT 文件）就照旧读 `animation_model`。两套文件不再存在。

## 2. 0x6BF380（通用「按键名载模型」）

- 签名（H）：`bool __fastcall (model* rcx, ? rdx, docVariant* r8, const wchar_t* key r9, int flag [rsp+0x20])`。0x93AD16 起：`mov dword [rsp+0x20],0; lea r9,L"animation_model"; mov r8,r13; mov rdx,[rsp+0x40]; mov rcx,rax; call 0x6BF380; test al,al`。
- 找成员：`tag = u16 [variant+0x10]`（0xFFFF = 空），`table 0x179EBA8[tag](variant, &{const wchar_t* key; int32 index /* +8 */})`，index == -1 = 没有这个键。表项（H，镜像里是 0x180000000 基址的绝对地址，运行时随重定位）：tag 0 → 0x238F90（经典 SGO，调 0x52790），tag 1 → 0x238DF0（恒 -1），tag 2 → 0x239060（DSGO，0x55A30），tag 3 → 0x1C50F60（未查）。
- 键不存在时 0x6BF380 仍带空 variant 调 0x6BB890（不报错、不返回 false），所以钩子必须**先查键在不在**再选键名，不能「先试新键、失败再读旧键」。钩子只对 tag 0 / 2 查（H）。
- 钩子跑在 worker（0x93F910 异步构建内），只读文档，不碰共享状态。

## 3. 换色与 `main` 动画组

- Draw 0x93C370 读 `change_color[0]` / `[1]`，第 i 项配颜色 i；选择子经表 0x17A1CE0 取类型，类型 1（字符串）= 那个材质名，其他 = 全部材质（H）。DSGO 里的 `0.0` 走「非字符串」分支（M：DSGO 的 double 0 映射到非字符串类型，未实机）。
- 构建 0x93F910 在 0x93FB85 **区分大小写**查 anmgroup `main`，下标存 model+0xE0；更新 0x93D640 在 0x93EBAC 不检查就用它当下标。CAS 没有 `main` → 下标 -1 → EDF.dll+0x116650D 越界读崩（测试站 #50：v506_heli.cas 只有 `Roter` / `Main`）。（H）
- 统一文件里 `menu_animation_model` 的 CAS 必须有恰好叫 `main` 的组：生成器 `menu_model.menu_problems` 读 CAS 核对，不满足就拒绝写出文件（不靠运行期兜底）。没有 `main` 的载具 CAS 由 `tools/menu_preview.py` 生成一份只把那个组名改成小写的动画资源 `MENUOBJECT/EDF6VC_MENU_<CAS 名>.CAS`（同长度原位改，原 CAS 不动，直升机 / 战机代码照旧按 `Main` 查），`menu_animation_model` 指向它；这是动画资源，不是第二份模型文件。

### 3.1 每个可预览单位的文件由谁写

- 士兵：14 个 AI 模板及 `_LEADER`（`tools/menu_preview.py build`）；换色 / 背包变体 `EDF6VC_NPC_*`（`tools/support_loadout.py soldier_file` 调 `unify_soldier`）。预览的换色走 `change_color0/1` 两个通道，与战斗变体同色。
- 喷气机基础文件：`pylib/vcobjects.py jet_sgo` 生成时即统一；菜单 CAS 一律指向自己战斗 CAS 的改名副本（506 的，运输机 / 无人机 / X-Morph 战机各自的重定向 CAS 也是只有 `Main`）。
- 坦克基础 `V505_TANK_MISSION.SGO`：写它的是 AutoTurret（后坐力），安装器 `install_autoturret` 在交给 AutoTurret 写之前统一（AutoTurret 清单记录的就是统一后的字节，不在它的文件上再叠一层哈希）。
- 机甲基础 `V504_BEGARUTA.SGO`：没有别的写入者，由 `tools/menu_preview.py` 以自己的名义写入统一版（`rels()` 含它，卸载时放回原版）。
- 带配置的载具 `EDF6VC_LO_<BODY>_<hex>.SGO`（坦克换炮 / 挂架、喷气机挂架、机甲的机体 + 核心 + 4 把武器）：`support_loadout._build_one` 生成后调 `unify_vehicle`。`unify_vehicle` 每次从战斗键重新推导菜单键（建在已统一的基础文件上、模型换了的变体不会沿用基础文件的菜单模型）。
- 安装结束 `write_menu_preview` 用 `menu_preview.base_problems` 核对每个支援载具基础文件（Mods 优先，否则 Root.cpk）都是统一文件，否则报出。
- 插件侧（`src/room_preview_logic.h VehicleFileOf`）预览的就是战斗会刷出的那个文件：有配置用 `VehicleVariantFile`，否则基础文件；所有机体都预览，不再有「没有菜单模型」的框。刚改、还没保存生成的配置没有文件，视口显示「这套配置的模型还没生成」。
- **带配置的机甲 / 喷气机：菜单键指向合并模型**（2026-11-24，`pylib/menu_graft.py`）。战斗里机甲的 4 把武器、喷气机挂架上的外挂都是各自独立的模型，由战斗代码按 `vehicle_weapon_setting` 挂到机体骨骼上；菜单渲染器只画 `menu_animation_model` 一个模型（外加 WeaponTable 行的 Desc 武器），所以原来预览的是光机体。现在 `support_loadout._build_one` 为每个 `EDF6VC_LO_MECH_*` / 喷气机 `EDF6VC_LO_*` 另写两个文件：
  - `OBJECT/<同名>.MRAB`：机体 RAB 的拷贝，只留一个 `.mdb`（`<同名小写>.mdb`），里面是机体网格加嫁接上去的武器 / 外挂网格，嫁接件的贴图作为成员一并放入（`graft_pure.texture_members` 只收贴图，不收同名 `.mdb`）。
    - 机甲：每个装了武器的槽，取武器 SGO 自己的 `animation_model` 模型，挂到该槽所在行（`mech_parts.row_of`）的骨骼下，模型原点对齐骨骼（战斗代码的放法）。刚体件作为一根刚体网格骨骼 `edf6vc_slot<s>`（`mdb.graft_rigid`）挂上；蒙皮件（504 的导弹舱）连同自己的骨骼一起挂上（`graft_skinned`，骨骼名前缀 `edf6vc_slot<s>_`）。
    - 喷气机：每个挂架一枚外挂弹体（`vcobjects.STORES[kind].weapon.look`，按 AmmoSize 缩放，和飞行中一样大），作为刚体骨骼 `edf6vc_pylon<i>` 挂在外挂所在行的骨骼下（`hp_store`，没有就是 `body`）。
      - 战斗代码从这一根骨骼发射全部弹药，所以挂架的位置由菜单决定：奇数个时第一个在中线，其余按左右对称成对，从翼根向外排（半翼展的 `PYLON_SPREAD` 0.15–0.75）。
      - 每个挂架的顶部贴在机体该处的下表面（`jet_hardpoints.airframe`，不含起落架），z 取挂点骨骼的 z；那条线没有碰到机体时保持挂点骨骼的高度。
  - `OBJECT/<同名>_CALLOUTS.TXT`：标注点（模型空间、绑定姿势，CRLF，首行 `# EDF6VC callouts 1`，每行 `<tag> x y z`）。机甲是 `weapon0..3`（装了武器的取武器中心，空槽取该行骨骼原点）、`frame`（`koshi`）、`core`（`body`，608 没有 body 时取 `mune`）；喷气机是 `pylon<i>`。插件读法见 `docs/room-3d-preview-re.md` §6.4。
  - 统一文件里**只有菜单键** `menu_animation_model[0]` 换成 `[app:/object/<同名小写>.mrab, <同名小写>.mdb]`，CAS 和 MAB 仍用机体自己的；战斗键逐字节不变（`unify_vehicle(..., menu=menu_graft.menu_model_ref(file))`）。`tests/menu_graft_test.py` 覆盖 8 种机体（满武器）、每把武器装在它能装的每个槽、空槽机甲、每种喷气机挂满（外挂轮换）、FIGHTER 2 挂架和坦克，逐个核对：去掉菜单键后与战斗文件逐字节相同（DSGO 的字符串池是整份文件共用的，按战斗树引用的字符串重写后比较），菜单键指向合并档案，档案只有一个模型、目录顺序正确、贴图都在，嫁接骨骼齐全，标注点在模型包围盒内。
  - 坦克不合并：它的弹药从主炮发射，没有挂架可显示，菜单仍是坦克自己的模型。
  - 安装 / 清理：`support_loadout.menu_files(name)` 给出这两个文件，`missing_names` / `install` / `stale` 把它们和统一文件当作一组处理（缺任何一个就重新生成）。

## 4. 消息 4（玩家武器）

- OnMessage 0x93B320 跳转表 0x93C2E4：msg3 0x93B376，msg4 0x93B895，msg5 0x93C202，msg6 0x93BCA5，msg7 0x93C1C7，msg8 0x93C24B，msg9 0x93C255，msg10 0x93C25F（H）。
- msg 4 负载 `{int32 player, int32 slot, int32 row, int32 ?}`：player != view+0x2AC 时忽略；否则 view+0x368（Desc+0x50，选中槽）= slot，view+0x36C+slot*4（Desc 武器）= row，然后复制 Desc 投递重建。不写 GameStatus（H）。`ShowPlayerWeapon` 就走这条。

## 5. 绘制顺序

- 世界 / HUD 与 UI 共用一条渲染队列，UI 总在本帧 3D 场景之后画（docs/worldmap-select-re.md，H）。模型视图画在 3D 场景里（M），所以整备界面盖在 HQ 上时，皮肤要给视口**留洞**（不画不透明底），视图本身继续更新、继续画。具体的可见条件（`+0x304 ≠ 2`、`+0x3EC ≠ 1`）与摆放见 `docs/room-3d-preview-re.md` §6.2。

## 6. 未做 / 未验证

- **合并菜单模型（§3.1，L，全部离线推出，需实机逐项看）**：
  - 菜单渲染器能否从 `Mods` 加载 `OBJECT/*.MRAB`，并接受比 CAS 多出来的骨骼（`edf6vc_slot*` / `edf6vc_pylon*`）。原版菜单只加载过 `.rab`；MRAB 是战斗里用过的格式，菜单路径没有核对。
  - CAS 按骨骼名驱动：嫁接骨骼不在 CAS 里，应该保持绑定姿势跟着父骨骼走。实机看武器是否跟着手臂 / 肩部动，有没有飞到原点。
  - 蒙皮件（504 导弹舱）是否显示，骨骼权重有没有错位。
  - 喷气机外挂的位置是菜单自己选的（不是战斗里的发射点），看有没有穿进机身、左右是否对称。
  - 合并档案 10–19 MB（机甲），首次预览的加载时间。
- DSGO `0.0` 选择子走「全部材质」：M，需实机看颜色。
- 钩子改的是菜单唯一的 `animation_model` 读点；其他 UI（若有）读 `animation_model` 仍拿战斗 CAS（未发现其他菜单读者，M）。
