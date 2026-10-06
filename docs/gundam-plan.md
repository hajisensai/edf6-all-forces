# 沙扎比（MSN-04）：实现计划（待确认）

目标：一台玩家能呼叫、能驾驶的全新机动战士。外形是沙扎比，能在地面走和跑，能推进冲刺和持续飞行，
武器有光束步枪、光束战斧（格斗）、盾牌（减伤加盾载导弹）、浮游炮、胸部拡散メガ粒子砲；单眼和推进器口自发光。
只支持单机（和仓库里其它插件载具一样，见 `online-re.md`）。

依据（都是静态分析，阶段 1 先进游戏把关键假设测掉）：
- 底层类型：`player-jet-re.md`、`jet-plan.md`、`primer-plan.md`（506 机体由插件驾驶、逐帧摆骨骼）；
  类工厂表的反汇编见本文第 0 节末尾。
- 发光材质：本文第 2 节（复古巴拉姆 `light` 材质的 `snd_BRDF_Common_Light_NoOcc` 着色器）。
- 模型管线：`pylib/obj_model.py`、`pylib/procmesh.py`、`pylib/mdb.py`、`pylib/texfile.py`。

## 0. 为什么借 506 直升机机体

- 原生步行机（Proteus `VehicleBigBegaruta`、Nix、深渊爬行者）的脚步是 Havok 角色胶囊，走路手感最好，
  但没有飞行；要飞就得重写它的角色控制器速度、对抗跳跃/落地状态机，这条路没有任何现成代码。
- 506 机体上已经跑通的：插件直接写刚体线速度/角速度（`0x11B18F0` / `0x11B1760`）、HP、伤害消息钩子、坠毁、
  开火字节 `+0x2020/+0x2021`、其余 holder 直拉（`0x62C000`）、玩家驾驶与 HUD（`playerjet.cpp`）、
  悬停推力预算（`hover_lift.h`）、地图边界（`airbound.h`）、离地/天花板（`body506.h`）、
  逐帧写骨骼（`primer_pose.h`，百足龙虫已经靠它在地面爬行）。
- 缺的只有「走路」：射线量离地高度贴地 + 重力 + 插件摆出的步行姿态。
- 识别：SGO 的速度增益标记取新段 **7301**（`player-jet-re.md` 的 7001–7299 已被喷气机等占用）。

**新增类型（用户允许魔改加类型，放在阶段 6）**：游戏用一张「类名 → 工厂」表建对象。
`CreateObject 0x11945E0` 读 SGO 的 `xgs_scene_object_class`，用 `0x1195C50` 在 `mgr+0x2C8` 的
`unordered_map<wstring, shared_ptr<Factory>>` 里查，再调工厂 vtable 第 2 槽创建。
表由 `0x1191360` 遍历 `0x2136F78` 处的单链表建起来（各类在静态初始化里用 `0x118AF80` 挂链）。
插件可以登记一个 `Vehicle9xx_Sazabi` 工厂：分配 `0x2030`、调 506 的构造 `0x64E3D0`、写入复制过的 506 vtable
（连 `vt[-1]` 的 RTTI 一起抄，原版的 dynamic_cast 仍认它是 506）。它只换来干净的类名和钩子隔离，
物理全是 506 的，所以等阶段 1–5 稳定后再做；做的时候 `crew.cpp kClasses`、上车第 49 槽、HUD、
炮塔镜头这些按 vtable 认车的地方要一起登记。

## 1. 模型资产

来源：用户提供的 Sketchfab「P-Japran color ver」（`p-japran-color-ver.zip` → 内层 `sazabi_msn-04_gundam.zip`
→ `scene.gltf` + `scene.bin`）。

**授权**：glTF 里写的原模型（kunnatee「SAZABI MSN-04 Gundam」）是 **CC BY-NC-SA 4.0**，比页面写的
CC BY 更严：禁止商用、衍生作品须同协议。所以：
- 模型文件和由它生成的 MRAB **不进本仓库**，和钻地车的 OBJ 一样走 `obj_model.model_dir('sazabi')`
  （`$EDF6VC_MODELS`、发布包的 `models/`、开发者目录），安装时在玩家机器上生成；
- 发布包附 `models/sazabi/LICENSE.txt`：原作者、原链接、配色来源、CC BY-NC-SA 4.0 全文链接、
  「本 mod 免费、非商用」声明；
- 高达 IP 属于 Sunrise/万代，属同人 mod 常规风险，不在 mod 里使用官方 logo 或名称商标。

**实测数据**（`scene.gltf`）：
- 25.3 万三角形、19.8 万顶点；20 个材质，全是纯色（`baseColorFactor`），**没有贴图、没有骨骼、没有动画**；
- 三个材质带发光：`14___Default`（青，单眼）、`15___Default`（红白，胸口灯）、`13___Default`（黄，推进器口）；
- 68 个网格对象**不是按部位分的**（Sketchfab 按材质合并再切成约 5000 三角形一块），
  所以按「焊接顶点后的连通块」重切：得到 682 块独立零件（胶佩式装甲板）；
- 坐标：Y 向上、+Z 朝前、+X 是机体左侧（与游戏一致），脚底 Y=-84，头顶 V 字天线约 Y=1000；
- **不带武器**：没有光束步枪、盾、光束战斧——这三样用 `procmesh` 自己建模（第 3 节）。

**管线**（新文件 `pylib/sazabi_model.py`，每步一个函数，带离线检查）：
1. 读 glTF（新 `pylib/gltf.py`：accessor / 节点矩阵 / 材质颜色，纯 Python + numpy）；
2. 连通块 → 骨骼：每块按面积加权重心挂到最近的骨段，外加两条规则（最低点 <0 的腿部零件归脚、
   头骨只拿颈环以上的零件）。骨骼（左右对称各一套）：
   `root / pelvis / waist / chest / head / backpack`，
   `shoulder（肩甲）/ upperarm / forearm / hand / thigh / shin / foot / funnelpack（浮游炮舱）/ tube（长推进筒）`，
   另加 506 机体必需的骨骼（`body`、`rotor` 等，按 `jet-model-re.md` 的崩溃规则核对一遍）和武器挂点
   （右手步枪、左臂盾、背后战斧、胸口炮口、浮游炮 ×6）；
3. 站姿对称化：原模型两臂姿势不对称；零件是刚体，对右臂零件做刚体变换摆成左臂的镜像姿势；
4. 减面到 **约 6 万三角形**（参照：复古巴拉姆 5.4 万、Nix 2.7 万），按零件分预算，保轮廓；
   工具 `fast-simplification`（pip，仅安装期依赖），不可用时退回不减面并在日志报出；
5. 颜色：20 种材质颜色烘成一张 64×64 色板贴图，每种颜色占一格，顶点 UV 落到格子中心；
   普通部位用原版机甲的 BRDF 材质（模板 `v515_retrobalam.mdb` 的车身材质），发光部位见第 2 节；
6. 写 MDB/MRAB，读回再写一遍逐字节一致（`procmesh.build_archive` 同款检查），碰撞体按身高放大
   （`heli_rigid_body` 中心 + 半尺寸），布娃娃映射照 `ragdoll_fit.py` 核对。

尺寸：设定身高 23 m，游戏里按 **20 m** 做（比例 ≈ 0.0177 m/单位），实测后可调。

## 2. 发光材质

游戏自带：名字带 `Light` 的着色器都会把「遮罩 × 颜色 × `light_color`」写进延迟渲染的自发光输出，
泛光按亮度阈值触发，不需要新着色器或 D3D11 钩子。证据：`SND_BRDF_COMMON_LIGHT.SGO` 像素着色器反汇编
（`o6.rgb = mask * albedo * diffuse * light_color`）。

做法：克隆复古巴拉姆的 `light` 材质（`V515_RETROBALAM.MRAB / v515_retrobalam.mdb`，着色器
`snd_BRDF_Common_Light_NoOcc`，遮罩 `param_r_m_light_hr` 的 B 通道）：
- `albedo` = 纯色（发光色），`param_r_m_light_hr` = 纯白（整块发光），`normal` = (128,128,255)；
- `light_color`（类型 0x402）：单眼 6、推进器口 4、光束战斧刃 8；原版取值 1–30；
- 推进器口亮度由插件按推力实时改（阶段 3 验证材质参数运行时可写，不行就用两套材质切换）。

光束战斧刃：发光网格 + 挥动时叠原版光束特效（`pale_trap_laser01.efarc` 那类），两样都做。

顺带发现的仓库 bug：`pylib/cpk.py:112` 假设文件数据从目录表偏移开始；`DX11.cpk` 的目录在文件末尾、
数据从 2048 开始，读它会得到乱码。目前没有代码读 DX11.cpk，本计划也不需要它，单独修。

## 3. 武器模型（自己建）

用 `procmesh`（管体、椭球、盒）按沙扎比配色做三件，都挂在对应骨骼上：
- 光束步枪（右手）：枪身 + 枪管 + 能量匣；
- 盾（左前臂）：外框 + 盾面 + 三个导弹口；
- 光束战斧（不用时挂背后，格斗时在右手）：柄 + 发光刃。

## 阶段 1：可行性（不过关就停下来报告）

进测试场（`testrange/gen.py`，M045）：
1. 呼叫行（武器表追加一行 + 说明文字，模板 `eWeapon394` 系）能叫出沙扎比，不崩；
2. 模型显示正常、配色对、单眼和推进器口发光并泛光；
3. 能上车（上车提示）、HUD 出来；
4. 20 m 的碰撞体：被子弹打中会掉血，撞楼的表现，站在地上不抖；
5. 506 接触位 1（落地时清水平输入，`0x651DF2`）对插件写速度的影响。

## 阶段 2：移动

- 地面：重力 + 射线贴地（`GroundClearance`），坡度限制，卡墙判定沿用「实际位移小于下达速度一半」；
  左摇杆走/跑，镜头方向转身；
- 推进：A 冲刺（短时高速，消耗推进剂），LT 持续上升/飞行，悬停推力预算用 `hover_lift.h`；
  推进剂槽：飞行和冲刺消耗、落地回复（HUD 显示）；
- 姿态（`primer_pose.h` 同款逐帧写骨骼 +0x70）：待机、走、跑、冲刺前倾、飞行、落地屈膝、转身；
  离线工具：`tools/sazabi_pose_sim.cpp` + 渲染预览（照 `primer_pose_sim.cpp` / `primer_pose_view.py`），
  在离线渲染里先看动作，再进游戏。

## 阶段 3：武器

手柄映射（键鼠全部可在 ini 改，写法同 `PlayerJet*Key`）：

| 操作 | 手柄 | 实现 |
|---|---|---|
| 光束步枪 | RT | holder 0，光束弹（`BeamBullet01` / 卫星激光加粗那条，参照 `make_emc.py`）；举枪姿态 |
| 光束战斧 | X | 近距离装药（钻头同款，可破坏建筑）+ 挥砍姿态 + 发光刃 + 光束特效 |
| 盾 | LB 按住 | 正面角度减伤（Proteus 第 113–115 行同款）+ 举盾姿态；LB+RT 盾载导弹 ×3 |
| 浮游炮 | Y | 6 架插件驾驶的小 506 机体（蜂群无人机先例），绕敌开火后回收；冷却 |
| 拡散メガ粒子砲 | LB+X | 胸口扇形多束光束，蓄力 1 s，长冷却 |
| 上升 / 推进 | LT | 见阶段 2 |
| 冲刺 | A | 见阶段 2 |

Y、A 在车上的原版行为、上下车用的键，阶段 1 先实测再定。

## 阶段 4：HUD、声音、NPC

- HUD：推进剂槽、各武器冷却、浮游炮数量（沿用 `hud.cpp`）；
- 声音：推进器、光束步枪、战斧挥砍用原版声音（`sound-re.md`）；
- NPC 驾驶：玩家下车后按 crew 流程派 NPC，跟随玩家（`heli.cpp` 跟随 + 地面行走），射程内开枪。

## 阶段 5：安装器与发布

- `tools/make_sazabi.py`：从玩家游戏数据 + `models/sazabi/` 生成 `Mods/OBJECT`、`Mods/WEAPON` 文件；
  `pylib/vcobjects.py` 登记，`tools/installer.py` 调用；武器表只追加自己的行、不覆盖别人的；
- 模型缺失（玩家没放 glTF）时跳过沙扎比，其它功能照装，日志说明。

## 阶段 6：新增类型 `Vehicle9xx_Sazabi`

见第 0 节。先实测「EDFModLoader 加载插件在管理器构造 `0x11A3410` 之前还是之后」，
决定是挂链表还是直接插表；再实测复制 vtable 的对象析构释放是否正常。

## 测试

- 离线：`tests/` 单元测试（姿态、推进剂、减伤角度、按键映射）、`tools/sazabi_check.cpp`；
  模型读回逐字节一致、骨骼 / 布娃娃映射核对；
- 实机：游戏没在运行时才装 DLL 和数据（装前备份 `Mods`、装后核 sha）；绝不关闭用户正在玩的游戏；
- 测试站 `edf6.fushi.moe`：每阶段加测试用例，分支构建发 test 通道。
