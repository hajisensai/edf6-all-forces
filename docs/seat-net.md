# 载具的插件联机消息（tag 15）与座位选弹同步

2026-10-20，审查 #216（新弹种 HEAT / 霰弹）指出的联机缺口与「每个功能各写一份 tag 15 传输」的重复。

## 缺口（改前）

- 原版载具的可切换挂载 / 原生弹种（`payload.cpp`）只在操作者那台机器上生效：
  - 弹种选择从不发给别的机器；
  - `PullHook`（原版扳机 `0x62C000` 的接管）只对本机玩家（`Rider::player`）和本机运行的 NPC 炮手重定向；
  - `FindPick` 只认本机玩家；
  - NPC 的 `NpcPayloadSelect` 只在运行它的机器（主机，`AiGunner` → `IsOnlineAuthority`）上算。
  - 结果：远端玩家选了 AP / HEAT / 霰弹 / 导弹，本机的这辆车副本仍按原版主炮开火；主机 NPC 选的弹在客机上也是原版的。
- 钻头（`drill_net`）、普罗透斯（`proteus_net`）、沙扎比（`sazabi_net`）各自复制了一份完全相同的传输：签名、`Stream`、`Peek`、接收钩子、发送、ReferenceId、发送者 / 序号、去重 `Gate`。

## 传输：`src/seat_net.h`、`src/seat_net.cpp`

- 载具自己的 NetworkObject（车辆 `+0x120`）：slot 16（`+0x80`，`0x773DA0`）发送，原版对象描述符把消息送到每台机器上的同一辆车；slot 17（原版 `0x6325B0`）接收。逆向依据见 `docs/drill-re.md` §8.5（H，`drill_net_native_test` 在映射的 EDF.dll 上实跑）。
- 所有种类共用 **tag 15**（coop 占 13 伤害、14 RNG），块头相同（`seat_net::Head`：magic、version、sender、sequence、controller，24 字节，偏移与钻头 / 普罗透斯 / 沙扎比原有布局一致，旧块格式不变）。接收钩子按 magic 分发给登记的处理函数（`seat_net::Register`）；不认识的 magic 与其它 tag 原样交给下一个接收函数。
- 一个发送者身份（`rand_s`）、一个单调序号（跳过 0）；去重 `seat_net::BasicGate<N>`（原 `drill_net::Gate` 抽出）：每个发送者的水位、序号回绕、控制纪元（座位当前注册乘员的 ReferenceId，空座 / 主机 NPC 为 -1）、本机运行该流时不收自己的。
- 钻头 / 普罗透斯 / 沙扎比照旧在安装时链各自类的 slot 17（`0x17DB0D0` / `0x17DEE98` / `0x17DB4C8`）；其它原版载具类在它的车第一次参与选弹同步时按车辆 `+0x120` 的表链上（`ChainVehicle`，只链 send / receive 仍是原版 `0x773DA0` / `0x6325B0` 的类）。
- 普罗透斯的「别的版本」判定（同 magic 不同 version → 还原原版）保留在它的处理函数里。

## 座位选弹：`seat_net::Pick`（magic `SPCK`，40 字节）

`{座位, 武器在车辆持有器数组里的下标（NativeHolderIndex，-1 无）, 标志 kNpc}`，序号在块头。

- 发送（`payload.cpp SendPick`）：运行该座位的机器发——本机玩家的座位在 `PayloadFrame` 里，NPC 炮手在 `NpcPayloadSelect` 里（只有 `AiGunner` 为真的机器，即 `IsOnlineAuthority`）。选择变化立即发，否则每 `kPickRefreshMs`（500 ms）补发一次（丢包、晚加入的副本、过期的接收记录）。离线、未注册的车不发。
- 接收（`PickReceived`）：只收本机不运行的座位（座位里不是本机玩家、本机也不跑那名 NPC），纪元等于本机看到的该座位乘员，序号新于该发送者上一条；记录按 `ObjRef` + 座位保存。
- 应用（`ApplyRemotePicks`，每帧）：按持有器下标找到座位上的那件武器，按与本机相同的规则（`Switch` / `NpcPayloadSelect` 的挂载骑哪个原版控制、原生弹种互换）建重定向；`PullHook` 放行「坐在该座位的远端玩家（`IsAnyPlayer`）或远端 NPC」的扳机。`kPickStaleMs`（2 s）未刷新、离开会话、本机接管该座位时记录作废，扳机回到原版。远端选择不进入本机玩家的 HUD / 瞄具（`FindPick` 不认它）。
- 伤害仍只在一台机器结算：选弹只决定副本上「哪件武器的扳机被拉」，原版武器的开火与命中照原版同步。

## 旧版本对端

- 旧插件的钻头接收函数吃掉 505 上所有 tag 15、解不开就丢；普罗透斯 / 沙扎比的接收按 magic 放行给原版；原版 `0x6325B0` 对 tag 15 只读类型就返回、不改车辆（`drill_net_native_test` 实跑，新增「旧对端的原版接收忽略 SPCK」一项）。所以旧对端安静地忽略选弹消息，不会刷 malformed 日志；旧对端那里远端玩家的挂载照旧按原版开火（与改前相同的降级）。
- 钻头 / 普罗透斯 / 沙扎比的块格式与 magic / version 不变，新旧插件之间照旧互通。

## 验证

- `seat_net_test`：块头偏移、各 magic 互异、选弹编解码与每个越界字段、Gate 的去重 / 乱序 / 纪元 / 回绕 / 满员不驱逐、发送时机。
- `payload_runtime_test`（生产 `payload.cpp` + 假传输 `tests/seat_net_fake.inc`）：上车即发、刷新、LB 切换立即发、离线不发；远端玩家座位：未收到前原版、收到后扳机落到所选挂载、旧序号 / 别的纪元 / 畸形块被拒、过期回到原版、本机玩家接管时不用远端选择；NPC：运行机器发带 kNpc 的选择、副本照它重定向、离开会话作废。
- `drill_net_native_test` / `proteus_net_native_test` / `sazabi_net_native_test`（`EDF.dll` 只读映射，不运行游戏）：迁移后三种照旧往返，选弹块经原生编解码到它自己的处理函数，原版接收忽略它。
- 未实机：两台机器上远端玩家 / 主机 NPC 的选弹是否在副本上打出对应的弹（需要双机房间）。
