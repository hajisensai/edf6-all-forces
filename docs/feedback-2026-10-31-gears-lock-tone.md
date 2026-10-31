# 2026-10-31 反馈：载具挡位难用、飞弹锁定音吵

用户原话（2026-10-31）：

> 1.载具的档位制有些难用 在打巷战的情况下要优先考虑挂挡 有时候没停下来容易撞墙 建议改成战雷高度相似 因为战雷档位是单独的按键，同时优先级低于正常WASD 会被直接覆盖
>
> 7.载具飞弹锁定的声音有点吵耳朵 改成刚开始响几次然后就停下来了比较好

## A. 战雷式挡位（`src/drive_gear.h`、`src/drivegear.cpp`）

- 此前 W / S 本身换挡（`seat+0x2C4` 的键盘值当换挡键）：想停就得先降到 N，巷战里容易撞。
- 现在挡位由两个单独的键设定（`VehicleCruiseUpKey` 默认 Tab、`VehicleCruiseDownKey` 默认 F1，按键设置页
  「插件：地面载具」第 9、10 行）；W / S 恢复原版手感并优先于挡位：一按下立即取消定速（回 N），键盘原值直通；
  按着时挡位键无效；松开后保持 N、不写摇杆；再按挡位键才恢复定速（从 N 升到 1 挡）。一个在 W / S 按下前就按着的
  挡位键不算新按键（`State::blocked`）。
- 手柄不受影响（仍只作用于键盘座位）。
- 重置（上车、换车、状态超时）与键盘让位（暂停、地图、窗口失焦）恢复时，若挡位键正按着不算新按键，必须松开再按
  （`drivegear::Fresh` / `Hold`），否则边冲刺边上车会被当成按下而自动挂 1 挡。
- 默认键为什么是 Tab / F1，而不是左 Shift / 左 Ctrl：Shift 连按 5 次（R 到 4 挡正好 5 次）会弹 Windows 粘滞键对话框抢前台，
  步兵冲刺是左 Shift，老玩家 COMMON.CFG 里原版 Chat 仍是左 Ctrl（`docs/keyconfig-re.md` §5.2，Enter 只改了新建的表）。
  按 §5.2 / §7.4 与 `common/edf/keybind.h` 逐键核对：地面载具驾驶页 / 坦克页只用 W S Space 与鼠标键；插件第 14 页用了
  R G B X Y H 与鼠标侧键；左手够得着的字母全被同在地面车里读的键占了（Q 是标记键，在载具里也读；E 上车、T 副武器切换、
  R 挂载切换、F 换座、Z 瞄具放大、C 高视角、V 第一人称、X 开火键 3、G/B 同页的 Proteus 键），数字 1~3 是换座键
  （`SeatNumberKeys`）。Tab 与 F1 在原版各页、插件各页、`tests/keybind_test.cpp` 的冲突矩阵里都没有绑定。

## B. 锁定音只在开头响几声（`src/lock_cue.h`、`src/jetaudio.cpp`）

- 此前锁定中滴声随进度加快，锁上后 1.6 倍音高的长鸣无限持续；调用方 `payload.cpp Lock()`（载具，每帧）、
  `playerjet.cpp`（战机）。
- 现在每个（武器, 目标）的锁定过程只响 `LockToneBeeps`（默认 3）声滴声就静音；锁上后长鸣只响
  `LockToneLockedMs`（默认 450 ms）就静音，同一目标保持锁着就一直静音；武器切换 / 目标变化 / 状态掉回 0 后
  再锁，重新计数。`LockToneBeeps=0` 为一直响（旧行为）。原版武器自带的锁定 SE（`jetsound.cpp LockQuiet`）不改。

## 没有实机测试

只做了离线检查（`drive_gear_test`、`lock_cue_test`、`keybind_test`）。
