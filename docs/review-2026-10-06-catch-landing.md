# 接机与落地修复（仅离线验证）

## 根因与改动

- `Catch` 曾把登机门目标设到玩家一秒后的位置；近距 `AutoFly` 同时叠加玩家速度前馈。玩家以 6 m/s 伞降时形成约 6 m 的稳定门偏差，超出 fixture 中的 3 m 原生登机范围。
- 接机以 `SeatPoint` 返回的真实门位置和范围为准，通过双方平移速度及门的旋转速度计算一帧相对预测。伞降速度先夹紧，再更新接机目标。门读取失败不再按机体中心假定可以登机。
- 近距编队直接控制相对速度，不再先执行普通固定翼的失速速度下限和触地判定；误差为零时仍写入玩家漂移速度。保留底部离地高度限制。
- `Touch` 落地曾保留空中巡航油门、STALL、瞄准及升力状态。地面松开油门不主动回零，残留俯仰输入还能触发起飞。落地统一交接地面状态，只有当前明确加油时保留油门；地面怠速不能抬头起飞，部分动力手动起飞与原自动起飞门槛仍保留。
- 固定翼与旋翼都使用机体底部净空。原生向上接触标记与近地净空共同确认落地，覆盖碰撞后带微小上升速度的接触；水面和高处非地面接触不会被当作安全着陆。地面扫掠也按同一底部坐标进行。

## 已执行验证

- Release DLL 构建：`cmake --build build-recovery-review --config Release --target EDF6VehicleCrew pjet_kinds pjet_turn_sim --parallel 4`。
- `python tools/playerjet_recovery_check.py --build-dir build-recovery-review --negative-controls`：22 条生产路径断言通过；6 个独立负对照均触发预期断言失败。
- 负对照逐项恢复一秒玩家前导、落地巡航残留、怠速抬头起飞、机体中心净空、丢失接触修正、零误差保留旧速度。
- CTest 定向 `pjet_kinds`、`pjet_turn_check`、`playerjet_catch`：3/3 通过。

fixture 直接包含生产 `playerjet.cpp`，运行 `Touch`、`Ground`、`Lever`、`ReconcileGround`、`FloorClear`、`Catch`、`AutoFly`，其余外部游戏依赖由明确的替身提供。接机场景连续运行 600 帧，验证真实门偏移下误差收敛及原生登机按钮请求；它不模拟游戏本体的最终上机处理。负对照只修改临时复制的源文件，不改工作区。

## 集成接入与限制

本分支不改共享 CMake/selftest。集成者可在 CTest 中注册：

```cmake
add_test(NAME playerjet_recovery COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tools/playerjet_recovery_check.py"
    --build-dir "${CMAKE_BINARY_DIR}" --negative-controls)
```

runner 需要已构建的 `edf6common.lib`、MSVC 与 Windows SDK。fixture 使用 `/W4 /WX`，任何编译失败、崩溃或非预期退出均不算负对照成功。当前 runner 要求构建目录只有一个配置的 `edf6common.lib`。

未进行实机验证，未启动/操控游戏、游戏窗口或游戏进程，未读取或写入游戏安装目录。真实地图碰撞、模型骨骼的动态姿态及原生登机完成仍未验证；离线断言不代表游戏端 E2E 通过。构建产物仅在 worktree 的 `build-recovery-review/`，不提交、不安装。
