# 2026-10-18 反馈：EDF4.1 声音、材质与遗漏载具

用户反馈：部分 4.1 武器、敌人开火声音持续到退出或重开；移植模型像被美白，希望保留原来会反光的质感；V301 坦克模型未导入，并要求检查同类遗漏。

## 原因与处理

### 声音生命周期

武器生成器仍保留早期 P5 的 cue 替换，而 P7 已经提供原 4.1 resident SE bank。实际替换后的 `EDF6VC_E41_WEAPON006` 是 `FireSe[0]=0` 的一次性调用，却被映到 `e5w_AF_M1-RavenLS`：该 EDF6 cue 的 ACB `Length=0xFFFFFFFF`。原生只在循环 owner 模式建立停止句柄，因此一次性调用启动的无限音没有 owner 可在停火时关闭。修复必须恢复源 cue / 生命周期，不能给全部声音定时停止或清除 bank 的 loop 元数据。

机器人还有独立的问题：旧 `edf41robo.cpp` 忽略原 MAB 的 `se_stop`，并把未提供 channel 的一次性事件错误落到 channel 0。源 `UFOROBO401` 的 360 度炮在 435.0 帧显式持有 channel 0，在 1155.7 帧停止；其 `en_Robo_fire360_shot` 和 `en_Robo_fireSmall_shot` 是真实循环 cue。原武器组同样持有 charge / shot 独立句柄，循环 shot 在阶段结束停止，不能每次发射都丢弃句柄重新启动。

母船 301 的主炮 / 中炮也漏了真实 MAB stop：主炮 open 的出現音为 channel 0、0..440 帧；中炮 appear 为 0..189 帧；close 有多 channel 时序。实际 ACB sequence → synth → waveform → AWB → HCA 的链路证实主炮循环块为 35..110、中炮为 23..58。现在从 MAB 导出完整时间、channel、节点与 trace 轨道，按真实 CAS 时钟回放，跳帧补齐跨过的 key；状态取消、销毁和退出释放。Ready 与接地重复坠落音在原 4.1 native 中同样无句柄，未凭长度哨兵擅改。

实现与验证：`src/edf41_sound_channels.h`、`src/edf41robo.cpp`、`src/edf41mother.cpp`、`pylib/edf41_sound.py`、`tools/make_edf41_weapons.py` 及对应 focused tests。声音 Python 6 项与 C++ 生命周期回归通过；两个生产对象均按 `/W4 /WX` 编译通过。

### 材质基色

具体反汇编与受查文件 SHA256 见 [shader 契约](edf41-material-shader-contract.md)。转换器没有把 DDS 或有效材质参数调亮；同名 EDF6 shader 将旧 reflect 值同时加入基色，执行 `b + f*(1-b)`。只缩小 diffuse 或清反射会破坏其它角度和原来的反光特征。

`pylib/edf41materials.py` 从玩家 EDF6 的已核验 shader 派生 `EDF41_` 私有名字，仅将已核验白化 MAD 的乘数改为零：基色恢复 `b`，保留 EDF6 PBR、粗糙度、反射与其余指令。Alpha 材质同时恢复原 diffuse alpha。每个 DXBC 重新计算 checksum；PS 能由原生 D3D11 WARP 建立；全部 VS、techniques、输入输出签名、RT 数量、其它 pass 和 SGO 元数据保持原样。未知游戏 shader 拒绝转换，不按指令片段模糊修改。

安装期 `Edf41MaterialCompatibility=1` 启用，设为 0 后重新运行安装器，模型从原始登记资源重新构建并回到 EDF6 原 shader。`edf41_objects.build`、`edf41_outfits.build` 先核验源 SHA，再只对本套私有模型改 shader 名；shared EDF5 模型排除。地图只处理来源 4.1 的主模型 RAB，EDF6 donor 天气和环境 RAB 不改；模式与 shader 依赖进地图缓存。别名由实际引用它的既有 ledger owners 共享持有，禁用或卸载只在最后引用释放后删除；外来或被用户编辑的别名不覆盖。

这项修复消除已证实的旧 reflect 基色白化。**反射与光照仍由 EDF6 PBR 渲染，不表示已经恢复 EDF4.1 的整套 Phong 引擎或原天气环境。** 本轮没有实机画面对照。

### 真正的载具资源

原代码不是找不到 V301 文件，而是 `edf41calls.VEHICLES` 显式将 V301 六款映成 V505；NPC V301 另用 V505 AI SGO 加 EDF5 的 Blacker No.4.1 remake 替代。原 `OBJECT/VEHICLE301_TANK.RAB`、MDB、DDS、SHKT 在 4.1 的 CPK 全部存在。

新增 `pylib/edf41tank.py` 将 4.1 原车身、六款涂装与原碰撞形状一起导入。V301 只有七对轮、41 根骨，V505 是九对轮、49 根骨，不能只换 `animation_model`。CarBase 的轮组、刚体、约束、履带材质按真 V301 重建；原 18 体 Havok 文件保留，原生 ragdoll loader 会做 hkp→hknp migration（`src/legacymap.cpp` 的既有逆向）。炮管后坐的 CAS 按源 bind 重定向；去掉与后坐冲突的 physics→animation 单行写者，其余正反绑定逐矩阵校验。原上车、炮口、瞄点坐标保留，使用 EDF6 注册的乘员动作。

同类检查补上原 302 Nereid（`pylib/edf41heli.py`）和 406 Begaruta 普通 / red / gold / green（`pylib/edf41mech.py`）。302 保留源 CAS / SHKT 与 gun mount frame，补齐 506 行为需要的 MAB 名、座位、燃油、炮口和 sound table；调用只补燃油布局，不再因 shape mismatch 把整套源枪型与飞行设置回退成 506。406 沿 EDF6 仍保留的 VehicleBegaruta 类保留源骨架、六挂点、CAS 与 SHKT，补齐类无条件读取的左右脚定位骨，并将武器模型和 CAS 移到私有路径，避免撞上同名 remake。

生成器、调用映射与登记资源闭环：`tools/make_edf41_objects.py`、`pylib/edf41calls.py`、`tools/make_edf41_weapons.py` → `edf41port/objects.json` / `objects/` / `calls.json`。目前 13 个真实源对象涉及 64 个模型 / 配置 Mods 文件，另有 13 个独立默认摆放 SGO（包含此前已移植的巴拉姆）；45 条实际载具调用的资源引用均可在登记或游戏档案里闭合。原调用对象与私有摆放对象分别核验源 SHA。

### 共享归属升级

本机现有 AI 坦克与旧版本登记 SHA 完全相同，却被升级规划误判成另一个 MOD：ledger 同时列出主 writer `edf41` 与只读消费者 `testrange`。现在允许原 writer 更新未被编辑、仅被已知测试场消费者引用的对象；其它 writer、仅有消费者冒充 writer 或用户编辑仍拒绝。更新后的模型 / CAS / SHKT 与实际 shader 依赖一并给原消费者登记，避免卸载主安装器后留下引用新依赖的断链 SGO。新安装安全测试覆盖两种卸载顺序、每类消费者、shader 保留、竞争 writer 和用户编辑。

旧 V505 模型的共享归属回归改为显式 synthetic registry / asset fixture，保留 legacy 升级、repair、重装、两种卸载顺序及原始文件恢复测试，不依赖当前真实登记是否仍共享该模型。真实游戏只读 preflight 已不再误跳过 AI 坦克。

测试场首次安装与后续重建还会读取实际安装的 4.1 私有模型，给其中真正引用的 `EDF41_` shader 追加 `need`。这与原始资源登记分开，因为登记 hash 保护的是未适配的源 bytes。关闭兼容后按实际模型释放过时引用；缺失 alias 在登记任何 range 依赖前被拒绝，避免留下半登记。主 writer 卸载后，已有 range 仍能持有并重新生成同一模型及其 shader，最后 range 卸载才清理。

### 408 边三轮的完整适配

源 408 是真实三轮双座、独立炮手和侧轮，具有八个物理代理。现有 `make_sidecar.py / sidecar.cpp` 的第三轮只有外观，枪手站立使用自己的武器，固定在右侧；原 408 的边斗在左侧，炮手操作车载机枪，所以不能直接替换这个模型并称为还原。

新增 `pylib/edf41bike.py` 保留原模型、8 个 SHKT 代理和双向绑定，借 V512 `Vehicle_Car` 的真实空 `car_aim_node` 底盘构建三轮、双座、一门 seat1 机枪。七个底盘 / 悬挂部件交给 CarBase 动态控制，枪保持原 mode 2 动画驱动；没有借用空壳 type3 constructor 伪装双轴 motor，也没有拆造假枪骨。`src/edf41bike.cpp` 仅识别专属 schema / marker，将 gun 创建时的 Aim 绑定到 seat1，沿已核实原生 Aim 输入、Pose 与运动学目标同步顺序驱动原单骨双轴枪；其它 Car 保持原路径，远端不重复拉火。

两个原骑乘动画在 EDF6 中缺失。两个座位使用四职业均存在的 native 后座动作，以源骑乘动画骨盆位置拟合坐点，最大误差为飞翼炮手 7.32 cm；实际四套蒙皮包围盒检查通过，未把身体脚部埋入地面。动作与手到车把、装甲贴合仍需游戏确认，不声称原骑乘动作逐帧相同。MAB 新增独立坐点向量与参数块，避免修改原先与相机旋转共享的向量。

### 默认摆放可用性

M 摆放要求 `mission_setup`，调用原型的 `vehicle_setup` 不会自动等同于该合同。为正常车辆生成 `EDF41_RANGE_` 私有 SGO，完整复制最终默认配装（301 主炮、302 四槽含燃油、406 六挂点、408 独立机枪、巴拉姆双炮），原请求 / AI 文件保持独立。目录与保存的 plan / log ID 保持原名，实际 path / rel 和脚本路由到私有配置。

所有已安装、依赖闭合的 4.1 车辆默认预载，重型 enemy41 变体仍受 `RangePlaceAll` 控制。此前被关在该选项后的五款 301 涂装与三款 406 颜色涉及 83,664,493 bytes 源模型档案；这约 84 MB 的文件量不等于实测 RAM / VRAM。新增 408 模型档案为 13,210,477 bytes。实际资源审计验证每个摆放对象具有完整配装，不能只放宽测试。

## 验证边界

- `tests/test_edf41tank.py`：真实源模型 / 物理正反矩阵 / 后坐 / 轮索引，以及错误骨架、错误旋转、错误索引的失败注入。
- `tests/test_edf41heli.py`：真实 302 / 506 数据、MAB / ragdoll / holder / death effect / CAS 合同与失败注入。
- `tests/test_edf41mech.py`：真实 406 四套骨架、原 CAS / 物理、脚骨、武器资源闭包和失败注入。
- `tests/test_edf41materials.py`：真实 DXBC checksum / 逐指令差异 / 原生 WARP、完整 pass metadata、DDS / 动画 / archive 顺序、关闭与外来 / 被编辑文件保护。
- `tests/test_edf41_material_install.py`：shared 模型排除、源损坏先拒绝、跨 owner shader 依赖、禁用恢复、地图模式缓存与依赖缺失重建。
- `tests/edf41_material_shader_contract_audit.py`：只读三代资源，保护真实差异的研究证据。

以上不表示启动游戏或联机验收。本轮没有写游戏目录、驱动游戏、声称听感 / 外观 / 三轮替代已经在实机通过。仍需用对应构建验证停火、阶段切换、死亡和退出时的听感，以及真实载具的物理、动画、上车、反射和联机行为。
