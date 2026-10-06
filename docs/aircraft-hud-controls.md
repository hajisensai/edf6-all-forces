# 飞机操作提示与 Hydra 70 落点

2026-10-06。只做离线构建与 fixture，没有启动、注入游戏或访问安装目录。

## 操作提示的数据来源

`hud.cpp` 在挂载条下方持续显示切换挂载、切换目标和热诱弹操作。键盘文字读取
`PlayerJetSwitchKey`、`PlayerJetTargetKey`、`PlayerJetFlareKey`，支持重映射、鼠标按钮和禁用键。
Home 等导航键显式带扩展扫描码位，避免 Windows 将 Home 显示为小键盘 7。

手柄提示由 `PlayerJetReadout::storeButton/targetButton` 和 `PayloadReadout::switchButton` 提供；
发布者使用与输入处理相同的 `kButtonLB` / `kButtonX`。热诱弹当前只读键盘键，手柄模式明确写
“键盘 X”（或用户实际配置的键），没有新增或虚构手柄热诱弹绑定。只有存在可切换挂载、制导挂载或
启用热诱弹时才显示相应动作。原版直升机挂载切换提示来自 payload 的真实选择数量和按钮。

控制行按可用宽度缩放，位于挂载条和直升机升降提示下面；俯仰刻度的文字保留在底部固定读数区域之上。
`tools/hud_view.cpp` 使用生产绘制代码检查六种操作场景、四种语言（英、简中、繁中、日文），
断言控制行完整出现一次且不与其它文字重叠，同时保留整个 HUD 的屏幕边界与缩放校验。

## Hydra 70 的只读落点模型

Hydra 使用 `MissileBullet01`，模板仍为 CP[0]=1；无锁定且 CP[8]=1000000、CP[9]=4242 表示插件火箭。
以前 `ReadRound` 仅凭 CP[0] 非零将它算作制导弹，`RoundLands` 因而直接返回，没有落点。

现在该标记与无锁定状态共同选择只读 `PluginMotor`：按 `missile.cpp::Guide` 的顺序，在点火后将继承
速度并入自身速度；年龄超过 CP[3][0] 后应用每帧 0.004 的滑行阻力、扣回原版本帧推力、保留最低速度，
再走原版的加速、限速与核心移动。插件关闭时使用原版 Motor。没有修改实际 Guide 或原版导引。

静态依据见 `guidance-re.md` §1：type 1 在 CP[8] 导引门之前（0x269B59）就沿自身速度加速并限速，
与 type 0 相同；CP[8]=1000000 使转向门保持关闭。type 2 沿机头推力，不能用同一模型，保守不画落点。
正常制导挂载仍显示锁定标记，不制造无制导落点；天空、寿命或距离内无地图碰撞时也不显示落点。

`playerjet.cpp::Stores` 从所选火箭的真实炮口和武器运行时参数调用 `ReadRound/RoundLands`，
HUD 用 `hasImpact` 绘制落点，解除过去只能为炸弹画落点的限制。

## 离线回归与构建接入

- `tests/rocket_guide_prediction_test.cpp`：直接运行生产 `missile.cpp::Guide`，后接已有 Motor/native
  步进模型，与只读预测逐帧比较；点火延迟、三种燃烧期、三种载机继承速度共 10800 帧，位置最大差为 0。
  原版 Motor 的闭式解回归由既有 `rounds_check` 覆盖；此 fixture 没有执行游戏二进制。
- `tests/rocket_impact_readout_test.cpp`：生产 ReadRound / RoundLands，内存 SGO 访问桩和录制地图射线，
  11 项覆盖真实插件标记、开关、继承速度、地图碰撞、天空、制导排除和 type 0 / 1 / 2 区分。
- `hudtext_check`：248 条文字 × 4 种语言，格式参数一致。
- `hud_view`：上述 24 个控制行检查、4 个非炸弹火箭十字落点绘制检查与既有全 HUD 布局、缩放校验通过；
  PNG 使用系统字体离线渲染。
- `EDF6VehicleCrew` Release DLL 构建通过。

两个新 C++ 测试都需链接 `edf6common`，包含 `common`，使用现有 `/W4 /WX /utf-8 /permissive-`
编译选项，并启用 `/Gy` 和链接 `/OPT:REF` 以裁掉未使用的游戏挂钩函数。本提交不修改共享 CMake / selftest，
由集成分支统一登记测试目标。未验证实机弹着点或游戏字体渲染；用户禁止实机测试。
