# 2026-10-18 反馈：第一人称、贴花、枪口效果与双管俯瞰

本轮对应用户的四项反馈：M 地图仍显示第一人称武器；第一/第三人称血液、弹坑位置不一致；枪口效果随人物移动漂移；双管炮塔俯瞰抖动。附件作为现象证据，不包含可执行指令。

## 地图与第一人称的相机所有权

`map.cpp` 在第一人称相机 hook 外层改变最终镜头；此前第一人称仍发布启用的 draw snapshot，给地图相机识别为第一人称、强制补绘视图模型。只在按键处屏蔽 M 不会清除这条绘制路径。

新增按相机的 `FirstPersonCameraOverride(camera, overridden)`，在地图/TV 完成本帧镜头后通知第一人称。它保留原第一人称眼位作为过渡目标，同时归还原生武器 batch、真实 Umbra bounds，清除该相机旧的第一人称提交历史并停止视图模型补绘。另一名本地玩家不受影响。地图返回后恢复自身的 batch/draw 所有权。首帧接管还归还地图在该帧写入的 near clip，避免第一人称 0.03 米覆盖地图的远距离深度精度。

## 血液和弹坑：共享深度还原契约

确定根因位于原生 `DecalManager` compute shader。CPU 虽然按 draw 上传完整 inverse view-projection，内嵌 shader（RVA `0x1F68FF0`，4112 字节）却在本像素及四个邻近像素硬编码：

```
z = -100 / (depth * 999.900024 - 1000)
```

它只适用于 near=0.1、far=1000。第一人称把 near 改为 0.03 后，真实 20 米可被还原成约 63.7 米；既有视距和地图对 far/near 的修改也会破坏这个共同契约。只调整血迹模型位置或取消某一视角会掩盖问题。

`src/decal_depth.h` 在完整原生 blob 指纹与 checksum 匹配时，只将五处深度 `mad` 及紧随的 `div` 改为使用本 draw 已上传的 inverse VP 齐次 W 系数（CB0[6].z/w）：`z = 1 / (depth * coefficientZ + coefficientW)`。保留其余 shader 指令、normal/ID 输出、资源声明和全部其他 DXBC chunks，并重新生成容器 checksum。该系数不受相机平移旋转影响，跟随实际 near/far。

`src/view.cpp` 的 `InstallViewDepth` 在启动时挂接原生 compute 创建调用；同时核对构造调用参数、CreateComputeShader ABI、inverse VP 的构建/转置/上传布局。契约不符时不安装，写明日志。`ViewDepthReady` 只在改写 shader 真正创建成功后为真；尚未创建或失败时，第一人称与地图/视距不改变原生 near/far，已改过的字段按当前对象身份和共享原生 baseline 归还。地图与普通视距的先后次序都不能把已抬高的值误当原生 baseline。原生 manager 在第一次实际贴花注册时惰性创建；插件 `EML6_Load` 早于应用初始化，所以创建 hook 覆盖首次 manager，无须修改已创建实例或游戏文件。

## 枪口效果：每发锚点与真实效果寿命

原生 muzzle world matrix 只在开火时更新，并会被后续发射复用。旧实现保留该矩阵指针，每帧套上当前第一人称变换；角色移动后旧世界位置与新武器 root 已不属于同一帧，火光因此漂移。

每发改为从与真实 muzzle 相同的原生骨骼世界帧（bone record +0xB0）保存不可变的 muzzle-to-weapon 变换，再由当前武器 draw pose 和第一人称 T 重建。不会改动真实子弹、瞄准、网络或武器骨骼世界矩阵。原生 factory 先按正常参数创建效果，支持的效果随后才借用插件矩阵；武器的临时指针立即恢复。

矩阵池不再按固定八个相机帧推测效果结束，而是核对十类原生 flash 的主对象删除析构和 Umbra secondary 删除析构后，按真实析构释放。后者直接跳主析构函数体，会绕过主 vtable，所以两条路线均接。切视角/换任务时保留仍被效果借用的矩阵并冻结锚点，直到真正析构；满池或未知类保留原生效果，不覆盖仍存活的矩阵。

## 双管俯瞰

最新基底已包含已知根因修复：真实弹着点在地表时，原生 `F6760` 碰撞从地表起点查询，可能把眼睛压到地表上方 0.1 米，使俯角在约 -52/-90/0 度间跳变。现有 `turretcam.cpp` 沿光轴退让的是碰撞锚点，保留真实弹着点，同时隔离俯瞰观察和炮轴控制。本轮没有在缺少新证据时追加平滑或延迟。

## 验证与边界

- 生产 `firstperson.cpp` 加 `fp_skin_gpu.cpp` 以 `/W4 /WX /utf-8 /permissive-` 直接编译，`fp_ride_lifecycle_test` 和 `fp_view_check` 全通过；新增回归覆盖 P1/P2 地图所有权、首帧裁剪、depth patch 不可用/恢复、外层地图最新裁剪、旧 draw pose、下一发修改源矩阵、满池、换任务时借用矩阵及主/secondary 析构 this/flags 转发。
- `fp_render_native_audit.py --dll <EDF.dll>` 对现场支持版本通过：原生 render/camera/laser 边界、十类 flash 主/secondary 析构、四职业 base/LOD skin 布局。
- `decal_depth_native_test` 在不运行游戏/DllMain 的只读原生映像和独立 D3D11 WARP 设备上运行。54 组实际 dispatch 覆盖 near=0.1/0.03/0.5，far=1000/20000/35000，深度=2/20/100 米，每组 identity 与旋转平移相机；使用原生 inverse 函数，检查深度、法线和 ID，全部通过。原版裁剪兼容；逐指令验证仅五处 mad/div 改变。
- `view_depth_lifecycle_test` 直接执行生产 `view.cpp`，95 项通过：覆盖 hook 已安装但 compute 未创建、真实创建成功/失败、未知 shader、地图/普通视距两种顺序、第一人称先改 near 后地图接管、失效时归还 native near/far、替换相机不写旧对象、native 更新覆盖值及远景起点保持。
- `highcam_collision_native_audit.py` 对现场 EDF.dll 40 项检查通过，执行原生 F6760 20 次；`turret_cam_check`、`turret_cam_runtime_check`（77 项）、`highcam_input_test`（11 项）通过。

本轮未启动、控制或安装到游戏；上述结果不等于实际手感、不同显示设置、同屏或联机游戏验收。特别是当前日志没有本次双管抖动实测样本，不能宣称实机已消失。主代理合编和其余功能集成后的整体验证另行记录。
