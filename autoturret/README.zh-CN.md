# EDF6：防空车一定要能防空

[English](README.md)

原版 KG6 克卜勒是 EDF6 的防空车，却打着又慢又不会爆炸的实心弹，对空中目标大多打空，耐久还不到同级坦克的一半。
这个 mod 让它真正能防空，并把 DLC 的 KG7 玻尔斯改成会自己瞄准的对地榴弹车。

由两部分组成：

- **EDF6AutoTurret.dll**：[EDFModLoader](https://github.com/BlueAmulet/EDFModLoader) 插件。炮塔自己瞄准：
  直接从游戏的敌人列表里选目标，计算提前量，按炮弹的抛物线解算仰角，并用前馈控制炮塔，炮弹不再拖在横穿目标的身后。
  高射炮弹带定时引信（目标距离处空爆）、近炸引信和触发引信。按住瞄准摇杆可以手动瞄准，松开后炮塔立即接管。
  还会给**泰坦和游骑兵带炮手座的坦克的副炮**配上炮手：没有玩家的炮手座（空座或坐着 NPC）自己瞄准、自己开火，玩家开的和 NPC 开的坦克都一样；玩家坐炮手座时只帮你瞄准，扳机还是你的。这部分不需要武器文件，原版载具直接生效。
- **武器数据**：直接覆盖原版载具自己的文件，不新增武器行。`WEAPONTEXT` 里这几辆载具的说明会改成新数值；只改它们自己的行，并叠加在 `Mods` 里已有的表上，改表的 mod 的内容会保留。
  数据可以单独工作：这些炮是原版游戏照常会开火的普通炮，只带一个只有插件读取的标记。没有插件时（删掉、关掉，或游戏更新后插件拒绝加载），克卜勒和玻尔斯照样能开火、打新炮弹，只是不自瞄，高射炮弹在最大射程处空爆。

## 改了什么

| 载具 | 改动 |
|---|---|
| KG6 克卜勒、E、F、YE、YF | 改为高射炮：爆炸弹（爆炸半径 8m），近炸／定时／触发引信，射程 480m。射速减半、单发伤害 ×2（账面 DPS 不变，同屏爆炸减半）。耐久 ×2。炮塔转速用 DLC YF-HV 的。优先打空中目标。 |
| 关卡里放的克卜勒（NPC 驾驶的、可搭乘的关卡载具） | 换成 mod 版 KG6 克卜勒：用它的高射炮（替换 NPC 克卜勒自带的 1 伤害机炮），耐久在关卡自身倍率上再 ×2，快速炮塔。 |
| KG6 克卜勒 YF-HV（DLC） | 只加自瞄；保留它的高速实心弹、耐久和炮塔。 |
| KG7 玻尔斯、玻尔斯 B（DLC） | 对地模式自瞄：优先打地面目标，按抛物线瞄准，保持原版触地爆炸。耐久 ×2，爆炸半径 4m → 6m，爆炸可以破坏建筑。 |
| 泰坦（全部，含 DLC 副炮） | 仅插件：两门副炮自动瞄准；炮手座没有玩家时还会自动开火，算驾驶员（玩家或 NPC）开的。主炮不动。 |
| NPC 泰坦（如第 64 关「殿军」） | 数据：原版 NPC 泰坦的两个副炮位是空的；`build.py` 把玩家泰坦的两门副炮装上，再由插件瞄准、开火。 |
| 游骑兵带炮手座的坦克（Vehicle403） | 仅插件：两挺副机枪，同上。单座坦克（空爆兵的、Vehicle601）没有副炮。 |

原因：原版克卜勒的 DPS 只有同级坦克、直升机的 1/3～1/2，耐久不到一半，射程还是同期最短。玻尔斯的 DPS 已经高于同级的霸里亚斯 TZ4，但耐久远不到对方的一半。

## 安装

需要 Steam 版 EDF6，并已安装 [EDFModLoader](https://github.com/BlueAmulet/EDFModLoader)。

1. 把 release（或 CI 构建产物）里的 `EDF6AutoTurret.dll` 和 `EDF6AutoTurret.ini` 放进 `<EDF6>\Mods\Plugins\`。
2. 用你自己的游戏数据生成武器文件（它们派生自游戏数据，所以不随包分发），需要 Python 3.10+，直接装进游戏的 `Mods` 文件夹（在仓库根目录运行；游戏目录取 `EDF6_DIR`，没有就在 Steam 库里找）：

   ```
   set EDF6_DIR=C:\Program Files (x86)\Steam\steamapps\common\EARTH DEFENSE FORCE 6
   python autoturret\tools\build.py install
   ```

   它在 `Mods\WEAPON\` 下写这几辆载具自己的 call 和炮文件，在 `Mods\OBJECT\` 下写关卡克卜勒和 NPC 泰坦，以及 `WEAPONTEXT.*.SGO` 里它们的 8 行说明；只读取游戏的 `Root.cpk`，不修改它。游戏运行时它会拒绝执行；`Mods` 里已有别的 mod 放的同名文件时不会覆盖（`--force` 先备份再覆盖）。写了什么、替换了什么记在 `Mods\.edf6at_data.json`（被替换的文件备份在 `Mods\.edf6at_backup\`）。装了别的会整份替换 `WEAPONTEXT` 的 mod 之后要再运行一次。`--no-text` 不动文本表；`check` 查看安装状态。

卸载：游戏关闭时运行 `python autoturret\tools\build.py uninstall`，再删掉插件。它恢复被替换的文件、删除自己新建的文件，并把 `WEAPONTEXT` 里那 8 行说明恢复成安装前的原文；别的 mod 的文件和行保持不动（安装后被别人改过的会原样保留，加 `--force` 才恢复）。旧版 `build.py` 装的（没有记录）也能卸，只删除和生成结果逐字节相同的部分。只删插件也安全：数据没有插件照样能用。设置在 `EDF6AutoTurret.ini`，游戏运行中保存即生效；`Debug=1` 会把炮塔的行为写进 `EDF6AutoTurret.log`。

## 构建插件

需要 Visual Studio 2022 及 C++ x64 工具（自带 CMake 和 Ninja）：

```
build.cmd
```

DLL 输出到 `build\Mods\Plugins\`（构建产物，不进仓库）。每次 push 都会由 CI 构建。

## 兼容性

针对 TimeDateStamp 为 `0x678CCB46` 的 EDF.dll。插件会校验要打补丁的代码，游戏更新后对不上就自动停用（武器数据没有插件照样能用）。0.3.0 之前的 `build.py` 生成的武器数据在这个插件下仍然能用（插件会像旧版一样改游戏的开火检查）；重新运行 `build.py install` 即可换成新数据。

坦克副炮已在泰坦上实测（NPC 驾驶、两个炮手座空着）：两门副炮打蚂蚁，弹药 40 → 28。游骑兵坦克的副机枪还没在有敌人的局里测过；副炮表现异常时请附上 `Debug=1` 的日志。原版空炮手座的炮打不响，是因为炮会问载具「谁在操作我」，空座位回答「没人」；插件让它回答驾驶员（见逆向笔记）。输入挂钩会串在其他插件（如 EDF6VehicleCrew）挂在同一槽的函数上，加载顺序无所谓。联机合作时，你这边可能把坐在炮手座的远程玩家看成空座，联机请设 `GunnerAI=0`。

联机未经测试。这个 mod 不新增武器行，没装的玩家不会遇到自己没有的行；但每台机器都按自己的文件模拟载具，混装房间里克卜勒在各人眼里的表现不会一致。建议所有玩家都装。逆向笔记见 [docs/re-notes.md](docs/re-notes.md)。

## 许可

MIT，见 [LICENSE](LICENSE)。随附：仓库根目录的 `third_party/EDFModLoader/PluginAPI.h`（MIT），以及仓库根目录 `pylib/` 里的 `cpk.py` / `crilayla.py`（来自 momotori01 的 EDF6MultiSlot 的 CPK / CRILAYLA 读取器，公有领域，见 `pylib/LICENSE.edf6-cpk`）。
