# 实机录制、离线回放（trace replay）

起因（2026-11-26，用户原话）：「电磁炮音效计数还没测。需要一个有 403 坦克的场景，还没搭。这种东西应该全自动之类的吧，而且每次都要启动一整个游戏。看看有没有办法弄单测方便快捷」，以及「照这个办法写成单测？不能全自动吗，为什么每次要重写」。

手写单测（如 `tests/vehsound_gun_test.cpp`）每次都要手搭一份假游戏内存（座位、武器槽、武器字段的偏移）和一组假函数。录制回放把这一层改成自动：在游戏里跑一次，把插件某一步读到的东西录下来；之后离线用**同一份生产代码**回放，不再开游戏。

## 怎么用

1. 录：ini 设 `TraceFile=<路径>`（`TraceSeconds` 默认 120），从进入的第一个任务开始录，到秒数或 1 GB 为止，日志有 `TRACE recording …` / `TRACE closed …`。perfrun / drive.py 用 `--ini TraceFile=…` 传。
2. 回放：`vehsound_replay_test <trace> [--at-least note.report_rail=1]... [--list]`（要先 `cmake --build build --target vehsound_replay_test`，`build.cmd` 不编测试）。
3. 游戏退出时录制没有关上（退出不经过插件），最后一条记录是半截：回放丢掉最后一次调用，其余照常，并打印「cut short」。

实机录一局的命令（2026-11-26 用过，wtfx3：玩家在泰坦里、NPC 403 电磁炮坦克、狂战士波次）：在 `gamequeue.py run --` 下跑 perfrun，`--ini Debug=1 --ini TestBoardVehicle=404_Tank --ini TraceFile=<scratchpad>/live.trc --ini TraceSeconds=150`。

## 录什么（`src/trace_rec.cpp`）

- 每次调用 `VehicleSound(v)` **之前**的游戏内存，按游戏进程里的原地址记录：车体、vtable 中被比较的那一格、控制器、座位数组、乘员（`SeatRider` 读的那段）、武器槽指针表、武器槽、武器、炮口数组及炮口骨骼记录、音效预设表、机甲的瞄准音效表。同一地址每次只存与上次不同的字节，所以文件不大。
- 不是内存的输入记为「值」：监听者（`SoundAt` 的全部输入）、`SoundListening` / `WeaponStatusOk` / `ClipsReady`、读到了哪些战雷录音、整份 Config；每辆车的类名、`BodyOf` 等；每门炮的 `ReadRound` 结果（字符串另存）。值只在变化时写。
- 输出：这一步里的 `audio::Note`（Debug 计数）、`PlayOnce`、`PlayWt`，按顺序记录（`jetaudio.cpp` 里的钩子，经 `crew::traceOutput` 指针；不录制时它是空的）。
- 任务开始（`mission.cpp` 的 `ResetVehicleSound` 之后）写一个 reset，回放在同一处重置状态。

## 回放（`tests/vehsound_replay_test.cpp`）

- 把录到的每段内存放回**原地址**（`VirtualAlloc` 指定地址），指针原样可用。edf 的读取函数（`SeatCount`、`SeatRider`、`MeanMuzzle`、`Readable`）用的是真实实现，读的就是这些内存。
- 录制头里有生成时那组源文件（`EDF6_TRACE_SOURCE_FILES`：`vehsound.cpp`、`vehmix.h`、`sound_event.h` 等）的哈希。与当前源码相同时，回放必须逐条输出都与游戏一致（增益、距离误差在 1e-3 以内）；不一致说明录制器漏录了这一步读的东西，要改录制器。源码改过以后只检查 `--at-least` 期望。
- 原地址在测试进程里被占用（极少见，ASLR）时退出码 2，并报出地址。

## 不开游戏的闭环（ctest）

`vehsound_trace_fixture` 把录制器和 `vehsound.cpp` 编在一起，在固定地址 `0x500000000000` 上造一辆 403 开炮并写出录制；`vehsound_trace_replay` 在另一个进程回放，逐条核对（`vehsound_trace_record` → `vehsound_trace_replay`）。它检查的是录制器和回放互相对得上，CI 可跑。

变异核对：录制器不录武器内存时，回放的 `report_rail` 为 0，游戏里为 5，16 次调用不一致，测试变红。

## 实机结果（2026-11-26）

- 一局约 75 s、74 688 次调用、约 200 MB（gzip 后 52 MB）：太大，**不入库**；实机录制只在本机用（录一次，改代码前后各回放一遍）。仓库里只有上面的闭环用例。
- 用同一版源码回放，74 688 次调用的输出逐条与游戏一致；`report_rail` 25 次（游戏里 25 次），`once` 808、`wt` 3546 等全部对上。
- 前两次实机录制回放出 25 / 34 次调用「差一帧」，找到的是生产代码里的真问题，不是回放的毛病：
  - `GameMs()` 读的是 `GetTickCount64`，一步里读几次就可能跨过一个 tick：等声速传播的 clip 在第 N 帧还是 N+1 帧放，取决于读到哪一次。改为 `VehicleSoundAt(v, ms)`：调用点（`crew.cpp`）读一次，整步只用这一个「现在」（`vehsound.cpp` 的 `stepMs`）。
  - 录制器自己耗时（复制内存），它读的时间和这一步读的时间会跨 tick。同一个 `ms` 同时交给录制器和 `VehicleSoundAt` 后才完全对上。

## 加新的子系统

照 `trace_rec.cpp` 的做法：在调用点前录内存和值，在输出函数里加 `TraceOutput`，写一个对应的回放测试。录少了的东西，第一次用同一版源码回放时就会以「与游戏不一致」报出来。这一步自己要读时间的，改成由调用点传入（见上），否则回放永远差一帧。

## 限制

- 回放只能覆盖录到的那一局里发生过的事：NPC 没开主炮，就回放不出主炮。
- 源码改了以后，旧录制只能做 `--at-least` 这类粗检查；要逐条核对就重录一局。
- 录制只在游戏主线程、只读游戏内存（读前都先 `Readable`），不改游戏状态。
