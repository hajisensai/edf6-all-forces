# 载具白热 / 黑热渲染

当前实现是把实际敌人网格的可见片元写进热遮罩，再对完整世界画面做灰度与冷热映射。不是矩形 HUD 遮罩、绿色覆盖层、坐标框或 CPU 视线筛选。
游戏没有已核实的物理温度场；热源强度由敌人网格身份确定，环境亮度来自场景颜色，因此不声称模拟现实装甲或发动机温度。

## 原生入口与身份

以下 RVA 仅支持 Steam EDF.dll TimeDateStamp `0x678CCB46`；安装前逐字节校验调用点、参数准备和目标函数头，失配保留光学画面。

| 入口 | ABI / 意义 |
| --- | --- |
| `0x11E223D -> 0x11B6340` | `void(manager,nativeCtx,view)`，Commander QUERY_BEGIN；`view+0` 是视口编号，`view+4 == 0` 才是主视图 |
| `0x11E255C -> 0x11E1630` | `void(system,model,pass)`，固定 `pass == 1` 的不透明对象提交 |
| `0x11E3AE2 -> 0x11B63A0` | `void(manager,nativeCtx,view)`，完整世界结束；调原函数后合成，下一条 `0x11E3AF2 -> 0x112C970` 会解绑 RTV / DSV |
| `nativeCtx+8` | 原生 `ID3D11DeviceContext*`；`0x113E7D3` 读取后在 `0x113E7F1` 调 `OMSetRenderTargets` |

现阶段只为视口 0 的本地玩家发布身份和模式，不把玩家 1 的模式套给分屏其它视口。独立 QUERY_BEGIN / END / Submit 调用点与第一人称已有补丁不冲突。

`thermal_models.cpp` 用经过构造函数审计的原生基类布局和 RTTI PMD 关联物体与 AnimationModel。涵盖昆虫、士兵、载具、BasicAnimationCharacter、Humanoid 全部模型数组及已确认的大型敌人部件；同时支持 EDF4.1 自定义类保留原生 COL 的克隆 vtable。未知布局不靠扫描内存猜偏移。部分大型 Boss 根容器本身没有已确认的 AnimationModel，其独立部件能否覆盖全身尚需游戏验收。

原生 Submit 在 `model+0x18` 非空时会把对象排进批次，绕开模型 slot 1。热源在这一次 Submit 中直接调它原有的 slot 1，并只在调用内设置 TLS 标记；其它对象保留原生 Submit。没有改 `model+0x18`、批次所有权、物理矩阵、生命周期或网络数据。

## GPU 契约

第一人称模块统一保留原生 mesh helper 的 `Prepare 0x10FFAC0 -> Pass 0x1151330 -> Flush 0x11597E0 -> DrawIndexed` 顺序。热源在实际 DrawIndexed **之前**采集；正常网格只把 pass 1 的深度表面通知给热像模块。

采集复用原生 IA、VS、蒙皮常量、HS / DS / GS、视口和光栅化状态，独立 PS 仅输出硬件深度；原生深度比较函数保留，深度写入关闭。私有 mask 通过 MIN / MAX 混合保留最近的热片元，原生 DSV 不被写入。这样原生 LESS 已拒绝的同深度片元不会发热。

世界完成后，将场景颜色与原生深度复制到私有 SRV。只有 mask 深度与最终深度码**精确相同**的像素能发热：D16 / D24 双方量化为对应整数深度码；D32 直接比较 float，不用远处会放大成数米的深度 epsilon。后绘制的墙会改变最终深度，因此遮住旧热源。白热与黑热只改变灰度映射极性。

原生 MRT 和 DSV 在采集后逐项恢复；合成用 D3D11.1 context state 交换完整隔离管线。私有清理会暂时关闭并恢复 predication，实际网格采集继续遵从原生 predication，避免被跳过的清理留下上一帧遮罩。

官方契约：资源复制要求尺寸与 DXGI 类型组兼容（[CopyResource](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource)）；SRV 与输出资源不能同时冲突绑定（[OMSetRenderTargets](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets)）；插件可通过 [SwapDeviceContextState](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-swapdevicecontextstate) 保存和恢复完整上下文状态。

当前支持单采样、单全尺寸视口、二维单 mip / 单 array 表面及 D16 / D24S8 / D32 / D32S8 深度。多采样、混用深度方向、stream output、存在单 draw 内条件 stencil 写依赖、资源或格式失配时，整帧保留原画面并报告热像不可用。透明 pass 不伪造不透明热源；alpha discard / 自定义像素深度与普通不透明网格的覆盖差异仍需实机检查。

Commander 的遮挡查询只在 cmd5 成对 Begin / End，正常 cmd1 / pass1 不处于 Umbra 活动查询中。没有全局证明外层性能统计 query 完全不存在；追加采集 draw 会计入这类外层统计。

## 用户模式与验收边界

默认光学，配置 `ThermalKey`（默认 N）按下时光学 -> 白热 -> 黑热 -> 光学。只允许明确有热像设备且当前处于实际观瞄的座位；坦克 / AA / 攻击直升机和炮舰炮手使用 `VehicleHasThermal` 的同一份保守设备策略，历史 M163 保留光学。

地图、暂停与其它菜单拥有输入时不触发模式变化；下车、换座位或任务重置清空旧身份。离线与联机都以真实本地座位作为有效条件，联机 `InSession()` 不作为功能门禁。HUD 展示实际生效模式、真实按键和不可用时的光学回退，等待 GPU 确认期间不宣称已经启用。

世界结束的合成仍会经过原生后级 screen compute；原版屏幕 HUD 在主循环稍后才录制。白热点在中间表面的值不等于最终显示器绝对亮度，需在游戏中确认最终对比度和性能。

本轮未安装 DLL、写游戏目录、启动游戏或驱动用户游戏。已经通过的验证：

- `thermal_gpu_test`：152 项真实 D3D11 WARP 检查，生产 HLSL / 网格 / MRT / 深度 / 四种深度格式 / 两种极性 / 同深度墙 / 远处微小深度差 / predication / 状态与生命周期。
- `thermal_models_test`：13 项生产解析器运行夹具，包括 cloneCOL、Humanoid 多模型、同地址变更 vtable、未知和伪 COL 拒绝。
- `thermal_runtime_test`：21 项生产控制与发布路径，包括离线模式循环、实际按键、地图、暂停、换座位、离开观瞄和任务重置。
- `thermal_native_audit.py` 与 `thermal_model_native_audit.py`：只读本机真实 EDF.dll 的调用点、ABI、构造布局、批次和世界先于 HUD 的证据。

离线验证不能代替实际观瞄画面、所有 Boss 部件覆盖、游戏 GPU 性能、分屏和双机验收。
