# 无畏舰（Dreadnought）：计划与实现记录

用户原话（2026-10-21）：
- 「"D:\QuarkCloudDrive\无畏战舰\Dreadnought.zip" 我想把里面的无畏舰拿出来放到edf里面。那艘可以放飞船的无畏舰。飞船可以玩家上去的那种。并且玩家也可以操纵无畏舰」
- 选型：「重型无畏舰+机库」；尺寸「原作多大他就多大」；舰载机「Bomber / Interceptor, Defender Fighter, Fighter」全要。
- 「船的炮塔是可以上人的」

可信度：**H** = 代码 / 数据直接确认；**M** = 有旁证的推断；**L** = 猜测，必须进游戏验证。

## 1. 资产来源（开发者机器，H）

- 游戏：Yager / Six Foot《Dreadnought》，UE 4.14（包文件版本 505，licensee 6）。30 个 `custompakchunk*.pak`，V3，索引未加密。
- 解包：CUE4Parse 1.2.2（.NET 10），**必须用 `EGame.GAME_UE4_13`**：以 4.14 解析时 `FSkeletalMaterial` 读到 `FMeshUVChannelInfo`（4.16 才有），`Invalid bool value (-2)`。
  工具在 `D:\APP\edf6-assets\tools\dnexport`（不入库），导出到 `D:\APP\edf6-assets\export`。
- 选用的网格（`DreadGame/Content/Generic/...`）：
  - 舰体 `Ships/Dreadnought/Heavy/VH_DreadH_SK`（Jupiter Arms 重型无畏舰，1.57 万顶点，两个材质）；
  - 机库 `Abilities/Dreadnought/Hangar/AB_DN_Pri_Hangar_SK`（「Fighters」主技能模块，6 根骨骼：两部升降机、上下舱门各两扇）；
  - 炮塔 `Weapons/Dreadnought/SecShort|SecMid|SecLong`（副炮，各有 `b_base` 旋转 / `b_barrel` 俯仰骨骼）；
  - 舰载机 `Ships/RespawnJet/Fighter/VH_Fighter_SK`、`Defender/DN_Fighter_SK`、`Bomber/VH_Bomber_SK`、`Booster/VH_Interceptor_SK`。
- 挂点：机库和武器在原作里靠运行时标签（`m_meshSocketTag`）挂到舰体，资产里**没有坐标**（M）。所以：
  - 机库嵌进舰体前甲板的平台（`HANGAR_AT`，轨道面朝上）；
  - 炮塔装在舰体自带的炮座上：舰体里 88 个三角形的独立圆顶 / 平台（`prep_dreadnought.turret_mounts`），只取上甲板、不在机库下面的 8 个。
- 材质：`_D` 是以 0.5 为中性的细节叠加图，颜色来自材质参数（`_MC` 的 R/G/B 选 `CustomColor01/02/03`，否则 `BaseColor`），
  实例的参数继承阵营母材质 `VH_YPawn_JupA_MASTERINST`（导出的 JSON 不写 Parent，用 `dnexport -info` 查到）；`_AO` 的 G 是 AO，
  Alpha 是舷窗 / 引擎灯（舰体用；舰载机的 Alpha 全是 1，不当灯）。PIL 缩放 RGBA 会按 Alpha 预乘，必须逐通道缩放。
- 尺寸：原作世界是缩小的（舰长 19.4、战机 0.68 个 UE 米），没有官方米数。按原作舰 : 机比例（约 28 : 1）、战机按正常战机大小，
  舰长定为 **500 m**（`SHIP_LENGTH`），战机 17.7–30 m。
- 素材授权：用户 2026-10 起的规则「选3D素材不管授权来源」。模型目录和生成物不进仓库（`D:\APP\edf6-models\dreadnought`，发布包 `models/dreadnought`）。

## 2. 管线（H，离线验证）

| 步骤 | 文件 |
|---|---|
| 导出 → 模型目录（OBJ/MTL、每材质 `_df/_nm/_rml.png`、`dreadnought.json` 骨骼与枢轴） | `tools/prep_dreadnought.py` |
| 模型目录 → 5 个 MRAB（V505 车身材质；有灯的材质用复古巴拉姆 light 材质） | `pylib/dreadnought_model.py` |
| MRAB → 网格复合碰撞体（24×32 凸格高度场，甲板可站人） | `pylib/aircraft_collision.py DN_FILES` |
| SGO（V506 机体壳）、炮手座 ×8、舰载机 NPC 机体 ×4 | `tools/make_dreadnought.py`、`pylib/vcobjects.py` |
| 呼叫行 `EDF6VC_CALL_DREADNOUGHT`（第 49 行，空袭兵载具请求） | `tools/calls.py`、`tools/call_weapons.py MODEL_VEHICLES` |

没有模型目录时：`EDF6VC_DREADNOUGHT.SGO` 写原版 V506_HELI，请求叫来普通 N9 Eros，不写舰载机（与沙扎比同一套退回逻辑，已泛化为 `MODEL_VEHICLES`）。

## 3. 插件

- 标记：无畏舰 7601–7699（`PluginBody::dreadnought`）；舰载机 7041–7044（`Body::dnFighter..dnInterceptor`，`BodyRow::crewless`）。
- `src/dreadnought.cpp`：玩家在座位 0 时插件驾驶（慢加速、慢转向，舰底离地 ≥ `DreadnoughtFloor`，5 条地图射线）；
  没人驾驶且开过之后降落到地面停住；原版 506 的开火键打舰首主炮和导弹；水的消息整条吞掉。
- `src/dreadnought_turrets.inc`：座位 1–8 = 炮塔 0–7。本机玩家坐炮手座：视角由插件维护（右摇杆 / 鼠标），镜头在炮塔后上方，
  炮塔按限速跟随视角、对准后主射击键开火（`jet_bay.cpp TurretRound`：炮舰侧炮 CANNON 的弹体 ×1.5 伤害），HUD 用炮舰炮手的瞄准框。
- `src/dreadnought_hangar.inc`：
  - 驾驶员按机库键（H / 手柄 Y）放出护航编队（4 种各一架，无人驾驶）；
  - 在空中（舰底离地 > 25 m）离开任何座位 = 驾机出击：机库放出下一种舰载机，登机枪路径把你送进去；
  - 驾驶舰载机时在母舰 800 m 内下机 = 返舰（不跳伞、不派接机），回到母舰的第一个空座位，舰载机交回无人驾驶。

## 4. 未验证 / 风险（L，需实机）

1. 500 m 的复合碰撞体（466 个凸格）在 Havok 里的表现：站人、降落时压到建筑、被写速度时甲板上的人是否被带走（同潜水母舰 §6 第 1 项）。
2. 舱门在右舷外侧地面（`move_door`）；舰体 500 m，玩家要走到右舷中段。
3. 炮手座：410 门炮手姿势套在 506 机体上，所有炮手座共用驾驶座的定位点；炮塔骨骼局部矩阵每帧写入后引擎是否按它画（沙扎比同法，H for sazabi）。
4. 炮塔开火的瞄准点取屏幕中心射线；`kOnAim` 2° 内才开火。
5. 出击：离座后玩家处于步行（下落）状态，`BoardingRequest` 能否在空中把人送进刚放出的战机（登机枪路径有距离 / 速度豁免，M）。
6. 返舰：`DreadnoughtTakesBack` 拦掉跳伞和接机；`HandBack` 把无人机交回自飞（M）。
7. 模型左右是否镜像：CUE4Parse glTF 的手性按 UE 左手系 → glTF 右手系推断（M），舰体基本对称，看不出差别。

## 5. 联机（未完成，如实记录）

- 无畏舰本体：只有联机权威写速度，其他机器看原版 506 的位置同步（与沙扎比相同）。
- 炮手：每个炮手由自己的机器驱动；**远端炮手的炮塔转向和炮弹现在不同步**（插件弹体只在开火那台机器上，docs/online-re.md）。
- 舰载机：插件飞机没有网络身份（docs/online-re.md「插件飞机 / 无人机的网络身份：没做」），出击 / 护航的战机是本机的。
- 下一步：经 `plugin_net` 通道同步炮塔角度和开火事件（远端只画不判伤害），舰载机走 `SubmitCarrierSupportDrone` 那条已有的联机放飞路径。
