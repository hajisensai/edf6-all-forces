# 载具手的模型素材（2026-11-28）

载具手（新兵种 type 4）第 2 阶段要用的外部模型：三辆车已转成游戏资源，手枪和装甲兵只下载了原件，还没有能用的转换管线。设计见 [plan-2026-11-28-zero-worldline-gear.md](plan-2026-11-28-zero-worldline-gear.md) §1.1、§1.3，接线方案见 [newclass-re/vehicler.md](newclass-re/vehicler.md) §1.2。

原件放在 `D:\APP\edf6-models\downloads\<目录>\`（不入库），每个目录里有 `SOURCE.txt`，记着页面、作者、uid 和授权。

## 素材来源

| 用途 | 原件目录 | Sketchfab 页面 | 作者 | 授权 | 面数 |
|---|---|---|---|---|---|
| V1 HSTV-L | `zw_hstvl` | [HSTV-L (War Thunder](https://sketchfab.com/3d-models/hstv-l-war-thunder-b030a4928dc04736acc71a31f2d1fd37) | KojfDiscord | CC BY 4.0 | 120,438 |
| V2 M247 约克中士 | `zw_m247` | [M247 "Sergeant York" (War Thunder)](https://sketchfab.com/3d-models/m247-sergeant-york-war-thunder-d5dea5cc896648fbb2e55a6670c3c712) | KojfDiscord | CC BY 4.0 | 142,008 |
| V3 MBT-70 | `zw_mbt70` | [MBT-70 (War Thunder)](https://sketchfab.com/3d-models/mbt-70-war-thunder-3cc1dbc72f724d1887e52998c86b9fa6) | KojfDiscord | CC BY 4.0 | 166,855 |
| 陀螺喷射手枪 | `zw_gyrojet` | [Gyrojet Family](https://sketchfab.com/3d-models/gyrojet-family-e5f4214f44bd407dbe1491a1a4078734) | Vasily（3D-Models-Geek） | CC BY 4.0 | 47,036（手枪 14,360） |
| 装甲兵外观 | `zw_tank_crew` | [Russian tank crewman](https://sketchfab.com/3d-models/russian-tank-crewman-1031d84d6abd4724acca854b1017e28e) | 42manako | CC BY 4.0 | 42,686 |

- 三辆车和 M163 / BMPT 出自同一个上传者，都是战争雷霆模型的导出件。CC BY 是上传者自己标的，战争雷霆模型本身的版权风险和 [wt-tracked-models.md](wt-tracked-models.md) 里那几辆车一样，发版前由维护者决定。
- Sketchfab 上搜不到可下载的 1980 年代解放军坦克乘员。最接近的是这个苏式坦克乘员：软坦克帽、连体服，和 80 年代解放军装甲兵的样子相近。但左袖有苏军第 4 近卫坦克师的臂章贴图（`Sleeve_patch_of_the_4th_Guards_Tank_Divisi.png`），做成解放军之前要重画贴图。
- 没下载的候选：Low-Poly Gyrojet（TastyTony，`e005ea58267a4627acacd42079019293`，3.5k 面）、Russian tank crew（Chernov-Egor，`7d447e0a09bd414895e4ae37f85e8ab2`，12.7k 面）。

## 三辆车：已转成游戏资源

用 `tools/prep_wt_tracked.py` 转换（新加了 `HSTVL` / `M247` / `MBT70` 三条规则），输出入库到 `models/wt_hstvl/`、`models/wt_m247/`、`models/wt_mbt70/`，并登记进 `tools/build_release.py` 的 `RELEASE_MODELS`。安装后的路径和其它战雷模型一样，由 `obj_model.model_dir` 找到。

```
python tools/prep_wt_tracked.py hstvl D:/APP/edf6-models/downloads/zw_hstvl/obj/HSTV-L.obj --license D:/APP/edf6-models/downloads/zw_hstvl/lic/license.txt
python tools/prep_wt_tracked.py m247  D:/APP/edf6-models/downloads/zw_m247/obj/M247.obj   --license D:/APP/edf6-models/downloads/zw_m247/lic/license.txt
python tools/prep_wt_tracked.py mbt70 D:/APP/edf6-models/downloads/zw_mbt70/obj/MBT-70.obj --license D:/APP/edf6-models/downloads/zw_mbt70/lic/license.txt
```

OBJ 是从下载包里的 `source.zip` 解出来的，`license.txt` 是从 `gltf.zip` 解出来的。

- 输出是 V603 骨骼格式（`body`、`cannon_main`、`cannon_l/r`、`cannon_slide_l/r`、两根雷达骨、`tire_moveA..H_l/r`、`catapi_body`），和 M163 / BMPT 一样，由 `make_tracked_combat` 读。
- 三个 kind（`hstvl` / `m247` / `mbt70`）还没加进 `pylib/wt_tracked.py` 的 `FOLDERS`：`make_tracked_combat` 还不认识它们，加进去会让 `tests/test_wt_tracked.py` 红。等车辆生成器（M1 的 `tools/make_zw_vehicles.py`）接上时再加，`pylib/buildcache.py` 的 kind → 目录映射也要同时补。
- HSTV-L 的导出里，瞄具玻璃材质 `dd9583da-…` 没有贴图（黑色，`d 0.17`）。新加的 `Vehicle.drop_materials` 把它去掉了。
- 主炮配置：HSTV-L 和 MBT-70 都是单管，左右两侧都用同一根炮管（和 PzH 2000 一样）。M247 是双管，`gun_barrel_02` 是左管，`gun_barrel_01` 是右管。M247 的炮管节点包括炮塔里的炮尾，所以炮口位置用 `mouths` 写死在炮管前端截面的中心。MBT-70 炮塔右侧的 20 mm 炮做成了 store 槽 `mg`，挂在 `cannon_main` 上。
- 炮塔座圈的位置是对炮塔篮做圆拟合得出的；炮耳位置是看节点包围盒和截面估的，没有像 M163 / BMPT 那样对着侧视图核对。进游戏要看一眼。

### 缺口：V1 / V3 的底盘

[vehicler.md](newclass-re/vehicler.md) §1.2 定的是 V1 HSTV-L、V3 MBT-70 用 `V505_TANK` 底盘。但现有的外部模型管线（`prep_wt_tracked` → `wt_tracked` → `make_tracked_combat`）只能产出 V603 骨骼，仓库里还没有给 V505 用的外部模型管线。有两条路：

1. 两辆车也走 V603（`make_tracked_combat`）；
2. 新写一条 V505 管线，把 model.npz 的骨骼映射到 V505 的炮塔 / 炮身 / 履带骨骼上。

由维护者选。这些模型目录两条路都能用：骨骼映射只是一张名字表。V3 的液压悬挂照 vehicler.md §6 第 4 条推迟。

## 手枪和装甲兵：只有原件，缺管线

- **陀螺喷射手枪**：glb 里有三支枪（手枪、卡宾枪、步枪），各自蒙皮到 15 根骨（`Gun`、`Safety`、`Trigger`、`Hammer`、`Slide`、`Slide Lock`、`Magazine`、`Bullet`），贴图是 PBR（BaseColor / Metallic / Normal / Roughness）。手枪长约 0.28 m。
  - 现在 `tools/zw_weapons.py` 的 `ZW_Gyrojet01` 借用的是 aWeapon109 的模型。
  - 仓库里没有「glb → 手持武器 MDB / SGO」的管线：`prep_glb_model.py` 只做炮台，`pylib/gltf.py` 只读静态网格。
  - 要用这个模型，需要新写一条管线：取出手枪网格，对齐 aWeapon109 的握把和枪口定位点，转换材质，生成 `EDF6VC_ZW_GYROJET` 的武器 SGO。
- **装甲兵**：glb 已蒙皮，骨骼是 3ds Max Biped（`Bip01_*`，193 根，含面部和手指），单位是厘米，身高约 1.83 m，绑定姿势像是 T-pose，没有动画。
  - EDF6 的兵种模型要用 EDF6 自己的骨骼（AssultSoldier 一类，配 `EDF6ArmySoldier.cas`）。仓库里唯一的换骨管线 `pylib/outfit41.py` 只吃 EDF4.1 的 MDB：按名字配对骨骼、Kabsch 摆姿势、重新绑定。它没有 glb / fbx 的读入，也没有 Biped → EDF6 的骨骼名对照表。
  - 要套上 AssultSoldier 的骨骼和 CAS，需要两样：
    1. 一个带蒙皮的 glb 读入器，读出骨骼、逆绑定矩阵、JOINTS / WEIGHTS，转成 `mdb.Mdb` 的形状；
    2. 一份 `Bip01_*` → EDF6 骨骼的名字对照表，给 outfit41 的 `Spec.exceptions` 用。
  - 另外还要处理三件事：厘米换成米；T-pose 和 EDF6 绑定姿势的差（outfit41 的摆姿势步骤能处理）；材质换成 `snd_Chara_*` 着色器。
  - 在这之前，载具手只能继续用 AssultSoldier 的原版模型。

## 实机检查（不开游戏没法确认）

- 三辆车接进生成器以后，在测试场里分别检查：
  - 炮塔转动时，有没有车体配件跟着转、或者炮塔配件留在车体上；
  - 炮身俯仰的轴是不是在炮耳上，炮管会不会穿进炮塔；
  - 炮口火焰和弹道起点是不是在炮口；
  - 履带贴图滚动，负重轮转动；HSTV-L 的主动轮拆成了 G / H 两个站位，要看转得对不对；
  - 贴图、法线、玻璃有没有问题。
- M247 两根炮管交替开火时，左右是否对应 `cannon_slide_l` / `cannon_slide_r`。
- MBT-70 的 20 mm store 槽的枪口点（1.10, 2.56, 2.18）是否在炮管前端。
