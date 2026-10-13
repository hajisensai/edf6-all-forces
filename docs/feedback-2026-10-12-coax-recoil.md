# 同轴机枪开火时主炮伸缩（2026-10-12）

用户原话：「同轴机枪开火的时候主炮会伸缩」。

## 根因（H，真实 EDF.dll 执行证实）

- 同轴机枪是 PR #137 / 2026-10-11 重做时给布莱克 505（5 种呼叫）、瓦利乌斯 601、格雷普 401 **多加的一个挂点**
  （`tools/make_stock_stores.py` `COAX`），挂在俯仰骨骼 `cannon` 上、后坐力 `BodyRecoil [0, 0]`。挂点本身没问题。
- 每帧 VehicleBase slot 5 `0x630250` 对「这一帧开过火的挂点」调用类的 slot 48。505 的 slot 48 `0x61AEA0`、601 的
  `0x620960` 先调 VehicleBase 部分 `0x62EA10`（按武器 `+0xBD8` 抬镜头抖动 `veh+0x670`），然后**不看是哪个挂点**就在
  `0x61AEE6` / `0x6209A6` 调 `0x116BFC0(veh+0x1170, [veh+0x2AC0], [veh+0x2AC8], 1)`——播放该车的主炮开火动作
  （带动 `cannon_slide`，即炮管后坐行程），最后才按挂点的 BodyRecoil 推车体（`0x5F9FB0`）。
- 所以同轴机枪每发一颗，主炮都跟着播一次后坐动作：机枪 720–900 发/分，看起来就是主炮在不停伸缩。
- 格雷普 401 的 slot 48（Car `0x65A740`）不播动作，不受影响。
- 证据：`tests/coax_fire_motion_native_audit.py` 在真实 EDF.dll（TimeDateStamp 0x678CCB46，字节哈希与
  `stock_store_recoil_native_audit.py` 同一组）上直接执行这三个 slot 48，动作服务换成记录桩：零后坐挂点开一发，
  505 / 601 各播 1 次 `(veh+0x1170, [0x2AC0], [0x2AC8], 1)`，Car 0 次；单独执行 `0x62EA10`：不播动作、不写车体速度，
  只把 `+0x670` 抬到 `2 × +0xBD8`。

## 修复

`src/payload.cpp` `InstallCoaxMotion`（由 `InstallPayload` 调用，在 `InstallGunnerRecoil` 之后）链接 505 / 601 vtable 的
slot 48：挂点武器是同轴机枪（`StoreRole::coax`）时只执行 VehicleBase 部分 `0x62EA10`，其它挂点照旧走类自己的 slot 48。
跳过的车体后坐本来就是同轴挂点的 `[0, 0]`，所以同轴机枪除了不再带动主炮动作外，镜头抖动、弹道、射速都不变；
主炮、炮射导弹等其它挂点的开火动作与车体后坐完全不变。

安装前核对：vtable slot 48 仍是 stock 地址、`fired+0x10` 是 `call 0x62EA10`、动作调用点是 `call 0x116BFC0`；
任一不符则该车保持原样并写日志 `COAX MOTION <车>: slot 48 is not the one read`。成功时日志
`HOOK coax motion=2/2`。

`src/gunnerrecoil.cpp`（远程炮手后坐在权威机上补算）原来按 stock 地址直接调 slot 48，改为经该类 vtable 调用，
这样补算的一发也经过同轴过滤（同轴属于座位 0、其操作者就是权威机，实际不会走到这条路径，改动是为了不留绕过口）。

## 验证

- `tests/coax_fire_motion_native_audit.py`（ctest `coax_fire_motion_native`，需 `-DEDF6_NATIVE_DLL=<EDF.dll>`）：4 项通过。
- `tests/payload_runtime_test.cpp` 新增：假映像上按真实布局放 vtable / call 指令，安装后同轴挂点只走 VehicleBase
  部分、主炮挂点与无武器挂点走类自己的 slot 48、601 同样生效；call 点对不上的车保持 stock slot。全部通过。
- 全量 ctest（带原生 DLL）228 项：除 `support_soldier_create_native` / `create_jet_native`（测试自身矩阵缓冲区
  未 16 字节对齐，与本改动无关，不含 payload / gunnerrecoil）外全部通过；`mission_watch` 并行时偶发失败、串行通过。

## 未验证

- **未真机验证**：没有进游戏看同轴机枪连射时主炮是否静止、主炮开火时是否仍正常后坐（autopilot 按不了开火键）。
  进游戏后先看日志有 `HOOK coax motion=2/2`。
