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

## 4. 消息 4（玩家武器）

- OnMessage 0x93B320 跳转表 0x93C2E4：msg3 0x93B376，msg4 0x93B895，msg5 0x93C202，msg6 0x93BCA5，msg7 0x93C1C7，msg8 0x93C24B，msg9 0x93C255，msg10 0x93C25F（H）。
- msg 4 负载 `{int32 player, int32 slot, int32 row, int32 ?}`：player != view+0x2AC 时忽略；否则 view+0x368（Desc+0x50，选中槽）= slot，view+0x36C+slot*4（Desc 武器）= row，然后复制 Desc 投递重建。不写 GameStatus（H）。`ShowPlayerWeapon` 就走这条。

## 5. 绘制顺序

- 世界 / HUD 与 UI 共用一条渲染队列，UI 总在本帧 3D 场景之后画（docs/worldmap-select-re.md，H）。模型视图画在 3D 场景里（M），所以整备界面盖在 HQ 上时，皮肤要给视口**留洞**（不画不透明底），视图本身继续更新、继续画。

## 6. 未做 / 未验证

- DSGO `0.0` 选择子走「全部材质」：M，需实机看颜色。
- 钩子改的是菜单唯一的 `animation_model` 读点；其他 UI（若有）读 `animation_model` 仍拿战斗 CAS（未发现其他菜单读者，M）。
