# 无畏舰（Dreadnought）：计划与实现记录

用户原话（2026-10-21）：
- （附原作游戏文件的压缩包，本机路径略）「我想把里面的无畏舰拿出来放到edf里面。那艘可以放飞船的无畏舰。飞船可以玩家上去的那种。并且玩家也可以操纵无畏舰」
- 选型：「重型无畏舰+机库」；尺寸「原作多大他就多大」；舰载机「Bomber / Interceptor, Defender Fighter, Fighter」全要。
- 「船的炮塔是可以上人的」

用户原话的完整记录（含两次试玩反馈）：`docs/feedback-2026-10-21-dreadnought.md`。

可信度：**H** = 代码 / 数据直接确认；**M** = 有旁证的推断；**L** = 猜测，必须进游戏验证。

## 1. 资产来源（H）

- 游戏：Yager / Six Foot《Dreadnought》，UE 4.14（包文件版本 505，licensee 6）。30 个 `custompakchunk*.pak`，V3，索引未加密。
- 解包：CUE4Parse 1.2.2（.NET 10），**必须用 `EGame.GAME_UE4_13`**：以 4.14 解析时 `FSkeletalMaterial` 读到 `FMeshUVChannelInfo`（4.16 才有），`Invalid bool value (-2)`。
  导出工具 `dnexport` 是开发者本机的 CUE4Parse 小程序（不入库）；`tools/prep_dreadnought.py` 读它的导出目录（命令行参数）。
- 选用的网格（`DreadGame/Content/Generic/...`）：
  - 舰体 `Ships/Dreadnought/Heavy/VH_DreadH_SK`（Jupiter Arms 重型无畏舰，1.57 万顶点，两个材质）；
  - 机库 `Abilities/Dreadnought/Hangar/AB_DN_Pri_Hangar_SK`（「Fighters」主技能模块，6 根骨骼：两部升降机、上下舱门各两扇），装 4 个；
  - 炮塔 `Weapons/Dreadnought/SecShort|SecMid|SecLong`（副炮，各有 `b_base` 旋转 / `b_barrel` 俯仰骨骼）；
  - 舰载机 `Ships/RespawnJet/Fighter/VH_Fighter_SK`、`Defender/DN_Fighter_SK`、`Bomber/VH_Bomber_SK`、`Booster/VH_Interceptor_SK`。
- 挂点：模块在原作里按能力的 `m_meshSocketTag` 挂到舰体骨架（`VH_DreadH_SKEL`，`dnexport -json`）上同标签的插座：
  - 机库：能力 `AB_DN_Pri_Jet_Fighter_Ability_BP` 的标签 `AB_primary` → 两舷各两个插座 `WP_broadside_left01/03`、`right01/03`
    （UE −176.5 / −550, ±270, 120.5 cm，右舷 yaw 180），每个插座一个模块（`prep_dreadnought.HANGAR_SOCKETS`），开口朝舷外（H）。
    2026-10-21 之前的版本没找到插座，把一个模块放在前甲板中线（`HANGAR_AT`），开口正对舰体中脊，已改掉；之后前甲板放过一架待命战机
    （`dn_ready`），2026-10-24 按用户决定去掉（§9）。每个插座装不装模块由 ini 选（§9）。
  - 炮塔装在舰体自带的炮座上：舰体里 88 个三角形的独立圆顶 / 平台（`prep_dreadnought.turret_mounts`），只取上甲板、不在前甲板中脊
    （`FOREDECK`，即旧机库位置）的 8 个。
- 材质：`_D` 是以 0.5 为中性的细节叠加图，颜色来自材质参数（`_MC` 的 R/G/B 选 `CustomColor01/02/03`，否则 `BaseColor`），
  实例的参数继承阵营母材质 `VH_YPawn_JupA_MASTERINST`（导出的 JSON 不写 Parent，用 `dnexport -info` 查到）；`_AO` 的 G 是 AO，
  Alpha 是舷窗 / 引擎灯（舰体用；舰载机的 Alpha 全是 1，不当灯）。PIL 缩放 RGBA 会按 Alpha 预乘，必须逐通道缩放。
- 尺寸：原作世界是缩小的（舰长 19.4、战机 0.68 个 UE 米），没有官方米数。按舰载机取正常战机大小，舰长 **500 m**（`SHIP_LENGTH`，
  1 个 UE 米 = 25.8 m），舰载机 17.5–30.2 m 长、17.5–30.1 m 翼展（原作约 28 : 1）。中间一度改成 200 m：用户第一次试玩说「这个模型比实际的模型大了很多」，
  当时理解成舰太大；其实那时网格碰撞体从未生效（§3 碰撞体），整艘船按包围盒碰撞，「模型」更可能指碰撞比画出来的大。第二次试玩用户说
  「无畏舰太小了，你找参照物找的不对。飞机也小了」，改回 500 m。随尺寸变的常量：`vcobjects.DREADNOUGHT_KEEL` 55.25、`DREADNOUGHT_CAMERA` /
  `_SEAT_CAMERA`、`src/dreadnought.cpp` `kLength`（`kReach` = 0.44 kLength、`kBeam` = 0.2 kLength）、`dreadnought_hangar.inc` `kSize`
  （机库进近距离按 200 m 时的数等比放大）与 `kOverDeck`（舰载机 SGO `heli_rigid_body[0][1]`）；selftest `dreadnought_agrees`、
  `make_dreadnought.check` 钉住 keel（标记贴碰撞顶的容差按该高度半精度浮点的步长：79 m 处 0.0625 m）。
- 素材来源记录（与沙扎比 `models/sazabi/LICENSE.txt`、驾驶舱 `models/cockpits/CREDITS_*.txt` 同一做法：随模型目录走的文本文件）：
  `prep_dreadnought.py` 在模型目录写 `SOURCE.txt`，写明出处（Yager Development / Six Foot《Dreadnought》，2017–2023）、
  取了哪些资产（舰体、4 个机库模块、8 座副炮、4 种舰载机，`DreadGame/Content/Generic/...`）、做了什么改动（缩放到 500 m、重新绑骨、
  涂装烘进贴图、转成 EDF 格式），以及「非官方同人作品，与原作版权方无关，模型版权归原作者」。原作资产没有开放授权；
  随发布包分发（`tools/build_release.py RELEASE_MODELS` 含 `dreadnought`）由仓库所有者决定（2026-10-24 已同意）。
  模型目录和生成物不进仓库，开发者本机的模型目录由 `pylib/obj_model.py DEV_MODELS` 给出，发布包里是 `models/dreadnought`。

## 2. 管线（H，离线验证）

| 步骤 | 文件 |
|---|---|
| 导出 → 模型目录（OBJ/MTL、每材质 `_df/_nm/_rml.png`、`dreadnought.json` 骨骼、枢轴与转过的骨骼的 `frame`；标记骨骼按碰撞格算） | `tools/prep_dreadnought.py` |
| 模型目录 → 5 个 MRAB（V505 车身材质；有灯的材质用复古巴拉姆 light 材质） | `pylib/dreadnought_model.py` |
| MRAB → 网格复合碰撞体（24×32 凸格高度场，甲板可站人） | `pylib/aircraft_collision.py DN_FILES` |
| SGO（V506 机体壳）、炮手座 ×8、舰载机 NPC 机体 ×4；按 ini 装的机库模块生成模型与碰撞；检查标记骨骼（舰桥、眼位、已装机库的机库位）与装配 | `tools/make_dreadnought.py`、`pylib/vcobjects.py`、`pylib/dreadnought_model.py` |
| 呼叫行 `EDF6VC_CALL_DREADNOUGHT`（第 49 行，空袭兵载具请求） | `tools/calls.py`、`tools/call_weapons.py MODEL_VEHICLES` |

没有模型目录时：`EDF6VC_DREADNOUGHT.SGO` 写原版 V506_HELI，请求叫来普通 N9 Eros，不写舰载机（与沙扎比同一套退回逻辑，已泛化为 `MODEL_VEHICLES`）。

## 3. 插件（2026-10-21 第二轮按用户反馈重写，`docs/feedback-2026-10-21-dreadnought.md`）

- 标记：无畏舰 7601–7699（`PluginBody::dreadnought`）；舰载机 7041–7044（`Body::dnFighter..dnInterceptor`，`BodyRow::crewless`）。
- `src/dreadnought.cpp` 舰桥驾驶（座位 0，原作手感）：W/S（左摇杆前后）推拉油门、停在推到的位置（可倒车到 -35%）；A/D 转向（转向有惯性、
  侧倾 ≤4°、加速时舰首微沉 ≤1.5°）；Q/E 横移；空格 / LT 上升、Ctrl / LB 下降；Shift / A 加速（能量 8 秒、16 秒回满）；舰底离地 ≥
  `DreadnoughtFloor`。视角与舰首分离（鼠标 / 右摇杆），准星 = 所有空炮塔的瞄准点，主射击 = 它们一起开火。原版 506 的开火字节每帧清零
  （「原版的导弹和机炮」不再开火）。指挥官去了炮塔（舰上还有玩家）：保持航向、油门、高度巡航；最后一个人下舰：水平速度立即清零，
  之后落地停住。舰从出现那一帧就由插件托住（落到地面）。
- `src/dreadnought_turrets.inc`：座位 1–8 = 炮塔 0–7，换座位键（F / 数字键 / 手柄 RB `DreadnoughtSeatButton`，`seatswitch.cpp`
  放行无畏舰）。每个本机玩家的视角（`View`）：舰桥和炮手座都在舰后 2.2×kReach、舰上方绕整艘船（炮塔附近的镜头试过座位后 18 m、瞄具点
  上方 2–8 m 后方 5–22 m，都落在舰体几何里面）；车辆第一人称（V）从 `vc_eye_<seat>`（舰桥窗口 / 炮塔瞄具）看（未实机验证）。HUD：`hud.cpp DreadnoughtHud`（航速、油门、离地、航向、舰体、加速能量、8 座炮塔谁在操纵、
  机库）+ 按键提示；原版仪表让位。
- `src/dreadnought_hangar.inc`：两舷最多 4 个机库（装了哪几个模块看模型里有哪些 `dn_hangar_<m>`，§9）、每个 2 个机库位（位 i 停 i % 4 种战机的静态副本 `dn_bay_<i>`）：
  - 出：副本沿机头（舷外）滑到 `dn_bay_<i>_out`（2.5 s），在那里缩成 0、同一位姿生成真正的战机，由机库托住（`jet.cpp JetHangarHold`：
    速度与角速度由机库写，`body506.cpp JetHeldStep` 排在玩家与 NPC 飞行之前）再放出；
  - 回：护航机燃料 / 血量 / 弹药不足（`JetLeaving`）或被召回时，机库把它飞到自己机库位的开口外 60 m、对正后机头朝外倒进开口，
    等游戏删掉它（`JetHangarStow`），再在开口显示副本、滑回；
  - 舰桥按机库键（H / Y）：护航编队（已装机库里有的每种一架）或召回；
  - 驾机出击（§9）：玩家站在某个已装机库上方按上车键 → 这个机库的一个机库位把战机滑到开口、把玩家放进去（登机枪那条路径），
    从这个开口往舷外弹射（2 s 加速到舰速 +90 m/s，`playerjet.cpp PlayerJetLaunched` 接手）；开着它在 1500 m 内按机库键 → 飞回原机库位的开口，
    在那里把玩家放到舰桥（`dn_bridge`）再入库；
  - 下舰站舰桥 `dn_bridge`、返舰后也放到舰桥 `dn_bridge`（都在标记上方 `kPutOver` 1.5 m 放下，落到碰撞格顶上；§8）。
- 碰撞体：`body506.cpp MakeAirframe` 用 SGO 成员 `edf6vc_airframe`（`pylib/vcobjects.py AIRFRAME_MEMBER`）认出插件生成的网格复合体；
  原来的 shape userData 标记在游戏加载紧凑 SHKT 时会丢失（实测 +0x28 一直是 0），历史日志里这条路一次都没生效过，所有插件飞机都是包围盒。
  目前只给无畏舰和它的 4 种舰载机打了这个成员（其他飞机的改动需要另外实机验证）。

## 4. 未验证 / 风险（L，需实机）

1. 500 m 的复合碰撞体（640 个凸格，已装的两舷机库计在内）在 Havok 里的表现：站人、降落时压到建筑、被写速度时甲板上的人是否被带走（同潜水母舰 §6 第 1 项）。
2. 舱门在右舷外侧地面（`move_door`）；舰体 500 m，玩家要走到右舷中段。
3. 炮手座：410 门炮手姿势套在 506 机体上，所有炮手座共用驾驶座的定位点；炮塔骨骼局部矩阵每帧写入后引擎是否按它画（沙扎比同法，H for sazabi）。
4. 炮塔开火的瞄准点取屏幕中心射线；`kOnAim` 2° 内才开火。
5. 出击：站在已装机库上方按上车键（`crew.cpp FindSeatHook` 对无畏舰本身的那次调用转给 `DreadnoughtHangarBoard`），战机滑到开口后用
   `BoardingRequest`（登机枪的路径，按住期间每帧按一次上车键）把玩家放进去，再从开口弹射 2 s 后交给 `PlayerJetLaunched`（M，未实机）。
   上车键的「访问」不看距离（边三轮摩托同一路径，`sidecar.cpp`），所以站位只按 `OverHangar` 判（模块原点为中心，横向 ±28 m、纵向 ±48 m、
   原点下 3 m 到上 15 m）。
6. 返舰与下机：`DreadnoughtTakesBack` 只在战机被本机机库托在它自己机库位的开口、正在放驾驶员上舰桥（`BayJob::alight`，距开口 ≤ 3 m、
   相对母舰 ≤ 4 m/s，`src/dreadnought_deck.h`）时拦掉跳伞和接机；其余任何位置（舰体包围盒内飞过、返舰途中、刚从开口弹射、联机非机库机器）
   都照常弹射跳伞（2026-10-24 审查：原先只看舰体包围盒，飞过舰体上空下机会无伞掉下去）。放人发生在开口（舷外半空）：踢出座位后
   `WatchPlayer` 在 1.5 s 内把人放到舰桥（与下舰站舰桥同一套）；开口处下机到放上舰桥之间的几帧未实机（M）。
7. 模型左右是否镜像：CUE4Parse glTF 的手性按 UE 左手系 → glTF 右手系推断（M），舰体基本对称，看不出差别。
8. 预载：只要装了模型，每关 `PreloadJets` 都预载 4 种舰载机；另外按标记预载**所有**已预载插件喷气机的武器 SGO（15 个，§6 第 4 项），
   对所有插件喷气机生效。没法只在「本关可能出现无畏舰」时预载：能叫来无畏舰的是玩家装备的载具请求，预载发生在任务开始时，
   本地玩家的装备可在任务预载时从 GameStatus 读到（`loadout.cpp`、`docs/loadout-re.md`），但机库跑在无畏舰的联机权威那台机器上（是否总是叫来它的那名玩家未实测），测试场又不经武器直接放一艘（`testrange/gen.py`），只看本地装备会漏；按需预载要先实机确认这两点，本轮不改。插件自己生成的其它机体（潜水母舰 `subcarrier.cpp PreloadSub`、插件喷气机）也是「文件在就预载」。内存增量未实测（`LogMemory("mission start")` 可量）。

## 4.1 标记骨骼（H，离线；`prep_dreadnought.ship_markers`，`make_dreadnought.check_markers` 检查）

无几何、水平朝向（行：左、上、前、点），插件按名字读世界矩阵：

- `dn_bridge`（父 `dn_hull`）：舰桥顶（原作插座 `PreviewTarget_Bridge` 上方的平顶，栏杆之前）上 0.2 m。碰撞高度场每格取格内最高点，
  栏杆那一格比顶面高 0.6 m，所以放在栏杆前面那一格（顶面本身的高度）；检查：站点比该格碰撞顶高 0–0.5 m，画出来的顶面与碰撞顶差 ≤ 0.75 m。
- `vc_eye_0`（父 `dn_hull`）：舰桥正面前 0.5 m、顶面下 1.6 m；检查：水平 / 下俯 10°、左右 30° 的视线 30 m 内不碰任何三角形。
- `vc_eye_<1+n>`（父 `dn_turret_<n>_gun`，随炮塔转、随炮俯仰）：炮塔与炮最高点上 0.5 m、炮尾后 0.5 m、炮的中线上，看炮管方向（+z）。
- 机库：模块 `dn_hangar_<m>`（m 0–3 = left01、left03、right01、right03，父 `dn_hull`）；机库位 i = 2m（DroneSocket0）、2m + 1
  （DroneSocket1），各有 `dn_lift_<i>`、`dn_door_u_<i>`、`dn_door_l_<i>`（父 `dn_hangar_<m>`，水平，绑定姿势 = 门开、升降机升起）。
- `dn_bay_<i>`（父 `dn_hull`，**网格骨骼**，带 `BAY_KINDS[i % 4]` 的战机模型，材质与该战机的 MRAB 相同，不上舰体涂装）：原点 =
  该战机 SGO 碰撞盒中心（插件的载具位置），机底贴在升起的升降机顶面上；转过的坐标系，前 = 舷外（0–3 号 +x，4–7 号 −x），左 = 上 × 前。
  检查：骨骼包围盒 = 该机型模型的盒（误差 0.05 m），中心在原点。
- `dn_bay_<i>_out`（父 `dn_hull`，无几何，同一朝向、同高）：沿舷外方向第一个让该机型的盒（四周再加 1 m）不碰任何碰撞格的点（0.25 m 步长）。
- `dn_ready` / `dn_ready_side`（前甲板待命机的位置）：2026-10-24 起不再生成；旧的模型目录里有也不进模型（`dreadnought_model.LEGACY_MARKERS`）。
- 没装的机库模块（§9）：`dn_hangar_<m>` 和它两个机库位的 `dn_lift_*`、`dn_door_*`、`dn_bay_*`、`dn_bay_*_out` 都不在模型里
  （`make_dreadnought.check_fitting` 检查；插件按 `dn_hangar_<m>` 在不在认机库）。
- 机型原点（碰撞盒中心）离脚下面的高度（SGO `heli_rigid_body[0][1]`）：0 战斗机 1.171、1 防御战斗机 1.161、2 轰炸机 2.059、3 截击机 2.189 m。

## 4.2 原作机库（H，资产直接读出；已照此放置）

- **机库不是舰体自带的**：重型无畏舰的两套原作预设配装（`Loadouts/Precast/T4|T5/VH_DreadnoughtHeavy_T*_PrecastLoadout_BP`，`dnexport -json`）
  主技能都是舷侧炮（T5 `AB_DN_Pri_BS_Scatter_Ability_BP`，T4 `AB_DN_Pri_BS_Plasma_Ability_T3_BP`），主炮 `WP_DreadnoughtHPri01`，副炮
  SecShort / SecMid；舰体网格没有机库。机库是可换装的主技能「Fighters」（`AB_DN_Pri_Jet_Fighter_Ability_BP`），装上后游戏把机库模块挂在同一组
  `AB_primary` 插座上。用户 2026-10-21 问「这机库不是额外的机库模块吗…自带的话没关系不是的话修一下」：如实答复是额外模块（原作就这样挂）；
  2026-10-24 用户定了：每个插座单独选装不装（§9）。

- 安装位置：机库能力 `AB_DN_Pri_Jet_Fighter_Ability_BP` 的 `m_meshSocketTag` = `AB_primary`，重型舰骨架上带这个标签的插座是
  **两舷各两个** `WP_broadside_left01/03`、`right01/03`（UE −176.5 / −550, ±270, 120.5 cm；右舷的 yaw 180），
  不是前甲板。模块的开口朝模块局部 −Y，即朝舷外。（旧版的 `HANGAR_AT` 前甲板中线是没找到插座时的猜测：开口正对舰体中脊。）
- 动作：唯一动画 `AB_Pri_Hangar_Preview_AS`（4.3 s，只在改装预览里播，`dnexport -anim`）：起点关门、升降机放低，终点 = 绑定姿势 =
  开门、升降机升起（出击位）。实战里动画蓝图保持绑定姿势，战机在 `DroneSocket0/1`（模块局部 X ±100、Z −50 cm，yaw −90：机头朝舷外）
  生成，以 2000 cm/s 从舷侧开口平飞出去，最多同时 8 架（4 个模块 × 2）。
- 200 m 下的数（现在 500 m：都 ×2.5）：升降机平台 12.7 m（沿舰长）× 9.7 m（横向），行程 2.43 m（放低 = 收纳）；舱内净高 3.2 m（放低后 5.6 m），
  舷侧开口高 3.2 m、长 14.6 m；上舱门关闭 = 向舷外 2.07 m、向下 1.45 m 并绕纵轴转 23°，下舱门关闭 = 上移 2.07 m、外移 0.52 m、转 13°。
  原作舰载机（`AI/Fighter/VH_Fighter_SK`，与 RespawnJet 的 Fighter 同尺寸）站在升起的升降机上（机底比平台高 6 cm）。
- 四种舰载机能否放进一个升降机：机头朝舷外（原作）时战斗机、防御战斗机、轰炸机放得下（长 ≤ 9.7、翼展 ≤ 12.7），截击机长 12.1 m 放不下；
  机头朝前时只有截击机放得下。轰炸机（高 4.1 m）、截击机（4.4 m）高过 3.2 m 的舷侧开口。
- 现在的放法（全部机头朝舷外，接受穿模）：截击机的机尾伸出升降机内侧 3.0 m、轰炸机 1.5 m（伸进模块里）；轰炸机 / 截击机的顶比舱顶高
  0.9 / 1.15 m（在模块的顶板里，从外面看不见）；滑出时这两种前几米会穿过开口上沿。模块底面离下面的舰体 1.66 m（原作插座就是这个位置）。

## 5. 联机（如实记录）

- 无畏舰本体：只有联机权威写速度，其他机器看原版 506 的位置同步（与沙扎比相同）。
- 炮塔：每台机器驱动它自己的玩家操纵的炮塔（炮手座，或舰桥指挥的空炮塔），角度与开火经 tag 15 的 `DNTR` 块同步（`src/dreadnought_net.h`）；
  远端只画无伤害的炮弹，伤害由那台机器结算一次。未两机实测。
- 换座、下舰站舰桥、HUD、视角：各自在自己的机器上。
- 机库与舰载机：插件飞机没有网络身份（`docs/online-re.md`「插件飞机 / 无人机的网络身份：没做」），机库只在驾驶无畏舰（联机权威）的机器上运行，
  舰载机是那台机器的；其他机器上的机库位副本一直显示停着。驾机出击也只对那台机器上的玩家有效（别的机器上的玩家站在机库上按上车键只记一行日志）。
  下一步：舰载机走 `SubmitCarrierSupportDrone` 那条已有的联机放飞路径。
- 装哪几个机库模块（§9）是安装时的设置，不同步：每台机器按自己安装的模型画模块、算碰撞（碰撞是安装时烘焙的，运行中无法按别人的设置改）。
  没有新增要同步的状态。

## 6. 实机记录（2026-10-21，测试场 M045，drive.py 后台 + 虚拟手柄）

后台驱动的键盘键到不了座位输入（座位轴全 0），手柄（`stick_*` / `pad_*`）可以：前进、转向、上升（LT）、机库（Y）、下船（B）都按手柄测。

第一轮发现并修掉：

1. 上船后无输入直冲到 189 m：`Floor` 把垂直速度直接设成 (30−5)×1.5 = 37.7 m/s，`Approach` 只能按 4 m/s² 刹。改为不超过 √(2ad)，并把高度保持并入目标速度。
2. 悬停每秒掉 0.15 m：写入的速度在求解器积分时被扣一帧重力。改为松杆时锁定高度（锁在按当前速度刹停的位置）闭环保持。
3. 落地后一直以 −6 m/s 压地面：用 506 的接触位（+0x1580 bit 2），着地即不再下压。
4. 第一次按机库卡 1.9 s（轰炸机 1.6 s）：喷气机 SGO 预载不含其武器 SGO，挂架武器在第一次生成时同步加载。`PreloadJets` 现在按标记预载已预载机体的武器（stores.inc `kJetWeapons`，15 个），对所有插件喷气机都生效（内存影响未实测，见 §4 第 8 项）。

第三轮复测：离地爬到地板 30 m 只过冲 0.5 m；松开上升在 64 m 停住无回弹；悬停高度不变；前进 45 m/s 加速 4 m/s²；机库一次放 4 架无卡顿；空中下船直接换乘战机（出击）；无人时降到地面后停住。

未在实机测到：炮手座（手柄换座键未找到对应，B 是下船）、800 m 内弃机返舰（战机已飞远，走了原有的弹射接机）、联机。

## 7. 第二轮实机记录（2026-10-21 下午，测试场 M045，drive.py 后台 + 虚拟手柄，200 m）

- 舰身被一直往下压 12 m/s：待命机从舱口直接竖直上升，顶进舰体外伸部分，被机库「无限质量」地托着把整艘船压下去。改为先横移到舷外
  `kSideClear` 再上升。
- 待命机停在甲板上方 12 m 落不下去：停在的高度恰好是舰体包围盒顶（中心 +22.1 m）。查到网格复合碰撞体从未生效（见 §3 碰撞体），改用 SGO 成员识别后
  日志出现 `BODY506 airframe compound`，待命机正常停上前甲板。
- 复合体生效后，舰在交付时略陷入地面，留给原版步进时穿过地形掉了 270 m：改为从第一帧就由插件托住（落地停住）。
- 护航编队：4 架依次滑出机库、弹射起飞；召回后 4 架都飞回各自的开口、入库（`at its mouth: in` → `docked`）。
- 换座位（手柄 RB）正常；手柄 B 是下舰（所以无畏舰用自己的 `DreadnoughtSeatButton`）。
- 炮塔第三人称镜头放在炮塔附近（座位后 18 m；瞄具点上方 2–8 m、后方 5–22 m）都落在舰体几何里（满屏灰色板 / 漆黑）；改为和舰桥一样绕整艘船，
  准星照样操纵本炮塔。截图确认正常。
- **士兵站不上舰体**（第三轮已找到根因并修好，见 §8）。下舰被放到舰桥上（`0.0 m off`），随后直接穿过舰体掉到地面；放到前甲板也一样。已排除：
  - 层：静态逆向 `CollisionFilter` 0x105510（循环出口 rbx=30，141 对），士兵步行刚体在层 1，载具 ragdoll 在层 6，1↔6 本来就开；飞行刚体层 3，3↔6 关
    （`tmp`/报告：layers-re.md，结论写在这里）。ragdoll 8 个部件都在世界里、keyframed（模式 2）、跟着船走（偏差 1–3 m）。
  - 把装舰体复合体的部件改到 17（建筑 / EAO 的层）：整艘船被弹到每秒数千米（17↔3，与自己的飞行刚体重叠）；把飞行刚体放进 ragdoll 的组（sub 0）后
    不再弹飞，但士兵照样穿过，且层 22 的地图射线也打不到它——`0x11B1840(body, info, 1)` 改了 filterInfo 似乎没进到世界的宽相；改到 18 同样穿过。
  - 形状：层 15 的射线能打到舰体（甲板高度 9.9 / 舰桥 21.9），命中的标志位与地形相同（`& 2`），复合体本身可被查询。
  剩下的方向（未做）：士兵的「脚下」判定具体怎么选刚体（keyframed 的载具 ragdoll 是否被排除）、层在世界里怎么真正生效。在弄清之前 `kStandsOnHull` 关着，
  下舰走原版舱门。
- 舰体看起来偏白：舰体材质约一半是裸金属（原作配色 `m_baseMetalic` 1），在 EDF 明亮的天空下像镜子；舰的金属度已减半（`SHIP_METAL`），
  仍偏银白，待用户看。

## 8. 第三轮（2026-10-21 晚，用户第二次试玩反馈 `docs/feedback-2026-10-21-dreadnought.md` 第二段）

- **加农炮打到自己**：舰的 ragdoll 部件刚体的对象不是舰本身，原版子弹收集器只按指针排除开火者，所以炮塔炮弹在炮口（就在舰体复合体的格子里）
  打中自己的舰体。`jet_hooks.cpp` 的子弹候选钩子加了「舰体」表（`HullBodies`：每艘无畏舰的飞行刚体 + ragdoll 部件 id，每帧发布）：舰本身
  （离线）或坐在舰上的玩家（联机 `ShooterIsAttacker`）的炮弹不碰它。实机日志 `DREADNOUGHT own round kept off its hull` 出现。
- **站不住、会跳**：用户那次的「跳」发生在船无人后以 8 m/s 下沉落地的过程中（日志：470 m 处下舰，人在甲板上方 14.7–20.5 m 来回）。改为：玩家开过的船
  无人后**原地悬停**（`Ship::flown`，`HOLDING`），只有从没被开过的船才落地。
- **下舰在侧面 / 站不住（已修，实机确认）**：`kStandsOnHull` 打开，下舰放到舰桥。先实机诊断（Debug 的 `stand report`：舰体部件 hknpBody 的 flags /
  filterInfo / 是否在世界里，士兵的支撑状态 `walk+0x91` 与接触表）：部件全部 keyframed、在世界里、层 6，士兵只和部件 0 有接触且法线全是水平的。
  静态逆向（tmp 报告 walk-re.md：步行控制器是 hknpCharacterRigidBody 定制版，靠求解器接触，keyframed 体可站，下车放人无忽略表）与逐字段比对
  （hkshape-fix.md：插件 compound 与原版同模式）排除了过滤和数据格式。真正的原因有三处，都在插件自己：
  1. `aircraft_collision.build` 给 `RagDollProxys.body` 以外的 7 个替身也用了 compound（1 cm 小盒）。`AirframeShape` 按机身骨骼名找部件，
     无畏舰模型没有 506 的骨骼，找到的是替身——**飞行刚体一直是 1 cm 的小盒**：这才是复合体生效那轮穿过地形掉 270 m 的原因（此后被插件按射线托着）。
     改为替身用 convex，`AirframeShape` 扫部件取唯一的 compound。
  2. `RagDollProxys.body` 保留了原版直升机刚体相对机体原点的位置 (0, 1.637, 0)，整个碰撞体比模型高 1.6 m。改为 (0, 0, 0)。
  3. 舰桥标记是画出来的顶面，碰撞格取格内最高点、可高出 0.5 m；士兵放在标记上方 0.2 m，实际在格内 0.3 m，又正好在中线两格的接缝上，
     两侧推力抵消，顺着接缝掉下去。改为标记上方 1.5 m（`kPutOver`）。
  实机（19:06）：落到舰桥顶，支撑 2，接触法线 (0, 1, 0)，9 s 内相对高度不变；舰空着悬停在 72 m 高。
- 甲板上的人不随舰移动（游戏的地面不带走站在移动刚体上的士兵）：单机时没人开船舰就不动；联机时别人开船，站在甲板上的人会被甩下——未做。
- **鼠标转炮塔**：视角改用原版输入之前的瞄准摇杆快照（`DreadnoughtInput`，与观察键读的同一时刻）；Debug 日志每秒打出「之前 / 之后」两个值。
  后台驱动只能当手柄（游戏不从可拦截的键盘接口读按键），键鼠下未实机验证。
- **尺寸**：改回 500 m（§1）。**机库**：原作预设配装里没有，是可换装的主技能模块（§4.2），等用户定。

## 9. 机库按插座装配、从机库出击（2026-10-24 用户决定，`docs/feedback-2026-10-21-dreadnought.md` 末段）

用户原话：「模块能装啥，肯定是装了模块才能有对应的功能啊，比如两个机库模块，就应该两个机库，而不是统一在一个位置」；选定每个插座单独开关、
站到机库上方按键出击、回原机库。

- **装配是安装时决定的**（理由）：碰撞体是 24×32 凸格高度场，整艘船一个复合体，安装时从模型量出来烘焙进 `*_AIRFRAME.SHKT`。离线对比真模型：
  去掉一个模块会改掉 22–28 个格子（舷侧那几列的顶从模块顶 31.6 m 降到舰体的高度，有的列整列消失）。运行时只能隐藏网格骨骼，碰撞体里的
  格子去不掉（要逆向 Havok 复合体子形状的开关，且无法离线验证），模块不在却还撞得到、站得上去，正是用户说的「没装就不该有」的反面。
  所以安装器读插件 ini 的同一组键（`DreadnoughtHangarLeftFore` / `LeftAft` / `RightFore` / `RightAft`，`dreadnought_model.hangars_wanted`
  与插件的 `GetPrivateProfileInt` 同样解析），只把装上的模块、它们的机库位副本和标记放进模型，碰撞体从这个模型量（`aircraft_collision.airframe`，
  仍以整舰包围盒中心为原点，和 SGO 的盒一致）；安装缓存按装配加键（`buildcache.Cache.current(settings=...)`），改了键再装就会重建。
- **运行时以模型为准**：插件看模型里有哪些 `dn_hangar_<m>` 骨骼来认机库（`RigHangar`），不看 ini——ini 改了没重装时，模型、碰撞和机库仍然
  一致，只在日志里提示「ini 与已安装的不一致，要重新运行安装器」。插件的 `Config::dreadnoughtHangars` 只用来做这条提示和 CONFIG 日志。
- 模块 m 的机库位是 2m、2m+1，种类 i % 4 不变（前面的模块：战斗机、防御战斗机；后面的：轰炸机、截击机）。
- 驾机出击：上车键 → `crew.cpp FindSeatHook` 在访问无畏舰本身时先问 `DreadnoughtHangarBoard`：本机玩家步行、站在某个已装模块上方
  （`dreadnought_deck.h OverHangar`）→ 这个模块第一个停着战机的机库位（`BoardBay`）出库，按住键的重复访问 0.5 s 内算同一次；
  战机滑到开口生成后由机库托住（`BayJob::board`），每 0.5 s 发一次 `BoardingRequest` 直到玩家坐进去，5 s 内没坐进去就收回入库；坐进去后
  从这个开口弹射（`BayJob::run`）。`BoardingRequest` 之前在 ini `BoardingGun=0` 时会被 `BoardingTick` 直接取消（它把「玩家」和登机枪开关绑在一起），
  已改为只有登机枪的子弹看这个开关。
- 返舰：`CallPlayerHome` 找到这架战机所属的机库位，按护航机入库的同一条进近路线飞回它自己的开口（`BayJob::recover` + `home`），到位后
  `BayJob::alight`：托在开口、踢出座位、放上舰桥，1 s 后入库；人若还在机内（踢出失败）就把战机交还给玩家，不硬收。
- 前甲板待命机整套去掉：`dn_ready`、`Hangar::nextKind` 轮换、舰首滑跑 `kDeckRunSec`、`DreadnoughtParked`（登机许可）、`DeckSpot` / `ToDeck`。
- 测试：`tests/dreadnought_deck_test.cpp`（装配 → 机库位、护航种类、出击机库位只来自所站的机库、站位判定、不跳伞的契约）、
  `tests/dreadnought_fitting_test.py`（没装的模块不在骨架、几何和量出的碰撞格里）、`tests/dreadnought_slot_guard.py`（不跳伞只看 alight、
  出击 / 返舰绑定自己的机库位、待命机的残留不复存在）、`tools/selftest.py dreadnought_agrees`（站位常量、ini 键与插件一致）、
  `make_dreadnought.check_fitting`（真模型：装配与骨骼一致、每个模块的网格在站位范围内）。
- 未实机：少装后的模型与碰撞、站在机库上方按上车键出击（含 `FindSeat` 访问顺序：附近若有别的可上的载具，上车键可能先上了它，此时机库送出的战机
  在开口等 5 s 后收回）、开口处放人到舰桥的那几帧、返舰原机库位入库。
