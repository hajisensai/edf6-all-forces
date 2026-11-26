# 2026-11-26 崩溃：`EDF.dll+0x673F70` 读 `0x41700008`（单座车的跌落座位越界）

来源：`feat/1126-wt-smoke` 天罚级压测 st22（`D:\APP\edf6-wt-1126-wt-smoke\tmp\st22*`），dump
`EDF6.exe.177052.plugin.dmp`（插件写的小 dump，带间接引用内存）。同一 DLL 的 st23 正常退出。与 gpufx 无关。

## 因果链

1. **数据**：`tools/make_sedan.py`（民用轿车 `EDF6VC_SEDAN`）和 `pylib/car_model.py`（X-Morph 无武器车，
   现为 `EDF6VC_XM_MEDIUM_TRUCK_CARGO`）都以原版轻卡 `V512_KEITRUCK` 为模板，把 `vehicle_riding_position`
   截成只剩驾驶座 `[:1]`，却原样留着 V512 的 `car_base_fall_out [10, 80, 1, [0,0,0], [1, 2, 3, 4]]`
   （`[4]` = 猛烈撞击时甩出的座位号，V512 是车斗里的四个座位，共 5 座）。装在游戏目录的两份 SGO 都是 1 座 + `[1,2,3,4]`。
2. **原版读法**：`Vehicle_Car`（vtable `0x17E01B0`）slot 4 `0x65A850` 先调 CarBase 预更新 `0x673A40`。
   `0x673F1E..0x673F33` 遍历 `veh+0x1B80`（int 数组，`+0x1B90` 个数，即 `car_base_fall_out[4]`），
   `0x673F41..0x673F4E` 直接算 `seat = [veh+0x608] + i*0x340`，**不与座位数 `veh+0x618` 比较**；
   `0x673F5A` 取 `seat+0x268`（乘员 weak_ptr 的控制块），非 0 就在 `0x673F70` 读 `[ctrl+8]`（use count）
   再 `lock cmpxchg` 加一——即 weak_ptr::lock。
3. **dump 实测**（崩溃时 rdi = 车 `0x2C59E987020`，r14 指向下标数组第 2 项）：
   - `veh+0x1B80` 数组内容 `1, 2, 3, 4`，个数 4；
   - 唯一的座位 0 在 `0x2C59D96CAE0`（座位 vtable + 本车指针）；
   - 下标 1 → `CAE0+0x340`，`+0x268` = `0x2C59D96D088` 处为 0，跳过（这就是平时不崩的原因）；
   - 下标 2 → `rsi = CAE0+0x680 = 0x2C59D96D160`，正好是下一个堆块的块头；`D170` 起是**另一辆车 `510_Maser`
     （`0x2C582B61D70`）的座位 0**。于是 `rsi+0x268 = D3C8` = 那个座位 0 的 `+0x258` 字段，值 `0x41700000`
     （15.0f，原版座位构造写入的合法数据）被当成控制块指针 → 读 `0x41700008` 访问违例。
4. **触发**：跌落逻辑只在剧烈冲击时走（日志里该车被天罚炸飞，`vel=(11.9,12.7,-28.3)`、离地 5 m）；越界读到的
   是哪块内存取决于堆布局：为 0 就无事，为可读地址则 `lock cmpxchg` **静默改写别的对象**，为不可读地址就崩。所以偶发。

谁写的 `0x41700000`：原版给 510 Maser 构造座位时写的它自己的字段，不是插件写坏，也不是释放后使用；错在我们的
SGO 让原版越界读到了那里。

## 修复

- `pylib/vehicle_check.py`：`keep_seats(members, n)` 截座位时一并去掉越界的跌落座位；`fall_out_problems` 作为
  `vehicle_problems` 的一条 error 规则（原版整数或整数浮点均可，V613_BIKE 写的是 `[0.0]`），`tools/check_shkt.py`、
  `car_model.check` 都经它检查。轿车生成器另在 `build` 里直接校验。
- 两个生成器改用 `keep_seats(m, 1)`：跌落座位变为 `[]`（封闭车，驾驶员不被甩出）。缓存按生成器源码哈希，下次安装自动重建。
- 先前 408 边车摩托（`docs/edf41-bike-re.md`，10-21）是同一个坑，当时只在 `pylib/edf41bike.py` 局部修了。
