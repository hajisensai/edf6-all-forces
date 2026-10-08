# 高视角与预瞄的帧生命周期

2026-10-08 用户日志在 18:26:27.743 打开高视角后，27.759、27.792、27.826 每隔约 33 ms 关闭，间隔中重新接管同一辆车。这是常规帧率的接管循环；慢帧是另一个独立问题。

## 根因与修复

`payload::FireOf` 将全部 `EDF6VC_` 自定义武器当附加挂载，而钻头、喀秋莎的原生自带武器也使用该前缀；第三方定制原生炮也可能如此。没有可搭载的原生控制时，真实 `PayloadSightWeapons` 返回空。`turretcam::Gun` 首次尚未绑定时回退第一支武器，绑定后却要求有效选中武器，于是每帧交替接管与释放，连带 HighCamServes/提示/俯瞰失效。

现在用实际 stores 目录分类附加挂载；自带定制武器保持原生控制。相机初次接管和持续帧使用完全相同的实际火控查询。没有有效武器时保持不接管，重新有效后只接管一次。

`HighCamOn` 原来同时承担输入模式与 HUD 的 200 ms 新鲜度。生产先运行 TurretCamFrame，后更新 HighCamFrame，持续慢帧下总是先读到 OFF 再发布 ON。模式现在锁存直到当前玩家离席、功能关闭、载具失效或重置；`PlayerHighCam` 的过期提示仍单独清除。同车重新登乘会重建按键边沿，持有按键不会再次切换。

TurretCam 与 Launcher 都核对 `PlayerHuman()`，另一名本地玩家的车辆回调不能覆盖全局相机或预瞄。远端玩家已由共同 SeatRider 排除，不将本项称为新的远端判定修复。

Launcher 与其他武器读取统一支持 16 holders，只跟随实际已选 lofted 武器及活着的 holder control。切到非 lofted、失去武器、本人离席或求解失败立即清自己的读数和抬炮请求；NPC/其他玩家回调不覆盖、不清除当前读数。

## 离线验证

测试直接执行生产 highcam/turretcam/launcher；原有输入和炮口夹具扩展到慢帧、重新登乘、无目标接管循环、本地双玩家与 9-holder 选择。核心回归在旧代码 61 条中 7 条失败，修复后全部通过。联合 payload/stores 分类测试使用真实资源 node 字符串布局，不替换 IsStoreWeapon、PayloadSightWeapons 或选枪查询。

不运行游戏、不写安装目录。日志只能确定接管节奏；夹具证明对应控制流修复，仍不等同本机操作或联机实测。

联合生产回归 `camera_payload_lifecycle`：真实 resource node → stores.cpp 分类 → PayloadSightWeapons → TurretCamFrame/LauncherFrame，覆盖实际钻头/喀秋莎文件名、Root.cpk 存在的 `V_407BIGBEGARUTA_CANNON.SGO` 以及自定义炮边界。原始 main 的 payload/turretcam 在 61 条中 27 条失败，恢复修复后 61/61 通过。Proteus 原版炮未被本轮改模型或替换资源；该夹具验证保留其正常功能，不宣称日志证明它与钻头同一个前缀问题。

最终插件 `/W4 /WX` 并行 3 构建通过，7 项定向 CTest 零失败/零 skip；4 项相关 selftest（含真实 Root.cpk 的喀秋莎生成检查）通过。未重复运行无关全量测试，交由主集成统一验证。

## 挂载瞄具与炮塔镜头互斥

`SightZoomMounted(vehicle)` 由实际挂载瞄具模块提供。返回 true 时，炮塔输入原样交给原生轴与稳定器，停止读取该瞄具自身的上一帧 CameraRay 作为新炮塔目标，并清掉自由观察/返回/第三人称镜头接管状态。Camera 不再写同一相机的第三人称位置。退出开镜后重新初始化普通视野；不会对外部自动炮塔命令做倍率缩放。桥接回归共 55 条炮塔运行时 + 61 条真实 payload 生命周期通过；最终 DLL 需与提供该 API 的瞄具提交一起链接。
