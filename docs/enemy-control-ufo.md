# UfoSmall507 / UfoSmall515 全技能复核（只读）

支持 DLL: Steam EDF.dll TimeDateStamp 0x678CCB46。只读取 DLL / Root.cpk，未运行游戏，不运行游戏。该复核不改变原生武器模式；新增控制仍保留原生发射、完成和取消生命周期。

## UfoSmall507

结论：每个 SGO 固定一个机载弹丸配置，BIG 的轰炸参数是替换同一弹丸参数块，不是同体可切换的第二枪组。现有玩家输入 button 0 到实际原生射弹路径覆盖这五个变种各自的机载武器。没有依据给它增加“模式切换”按钮。

| SGO | ai_attack_pattern | bullet_pattern | bullet_class | object_type |
|---|---|---|---|---|
| E507_GOLDUFO | 1 | 0 | BeamBullet01 | absent -> 0 |
| E507_GOLDUFO_HI | 1 | 0 | BeamBullet01 | absent -> 0 |
| E507_GOLDUFO_ACE | 1 | 1 | LaserBullet01 | absent -> 0 |
| E507_GOLDUFO_CHARGE | 0 | 1 | LaserBullet01 | absent -> 0 |
| E507_GOLDUFO_BIG | 0 | 2 | PlasmaBullet01 | 1 |

SGO 所有五种都是单 bullet_class / bullet_custom_param / bullet_info。BIG 额外有 ai_big_bomber_info 与 bullet_big_bomber_info。

- constructor 0x5288D7 查 object_type，0x528901 写 body+0xBF8；缺失分支 0x528920 写 0。
- 初始化弹丸 parser 0x52AE10（RCX=body，XMM1=level），0x52AE48 查 bullet_pattern，0x52AE5E 查 bullet_info。0x52B737 读取 pattern；0->默认 tint、1->laser tint、2->0x52B757 调用 0x5318E0。
- 0x5318E0（RCX=body，XMM1=level，R8=formula evaluator）查 bullet_big_bomber_info。索引 0->body+0x7B0、1->+0x7E0、2->+0x8FC、3->+0x7B6 flags、4->+0x7D0/4/8/C tint。末尾进入 0x52A980 建立同一弹丸类的组件；并无第二 battery、第二 factory、武器索引或玩家切换字段。
- 原生 slot5 0x52EF70（RCX=body，RDX=dt）；body+0x354/player 与 body+0x340/pad gate 后，0x52F1F3/0x52F1F7 将 pad+0x71 复制到 body+0x1474。pad+0x89 / pad+0xA1 在 0x52F22C / 0x52F291 仅写缩放/调试颜色参数，并非技能。
- constructor 0x52966F 将 callback 0x5320B0 放进 body+0x1448（state object body+0x1430），并把此状态挂到 body+0x1398。slot5 0x52F5E6 / 0x52F5FF tick 当前状态的虚函数 +8。
- callback 0x5320B0：EDX==1 且 body+0x1474!=0 时，更新 RNG 并令 EDX=随机种子，tail-jmp 0x52BFB0（RCX=body）。释放主击后此 gate 不再产生新发射。
- 0x52BFB0 使用 body+0x700 弹丸参数块和 body+0x6F0 factory，0x52C9B1 调用 0x1194280 创建实际伤害弹丸。BIG 的 body+0xBF8==1 分支在 0x52C04A 使用本体发射姿态；不是 CAS-only 效果。
- ai_attack_pattern：slot6 init 0x52D6B4 读，0x52DD2A 存 body+0xB90；AI 0x529F7F 依据 0/1 选 0x52A2D0 / 0x529FD0 的攻击航线状态。它是 AI 航线，不是实时武器索引。
- slot9 0x52EDE0 仅专门处理 0x20102 受击冲量/坠落状态，其他消息委托 0x54A530；没有本类 ObjectAction 可切技能入口。

指纹首 16 字节：

```
52EF70 48 8b c4 48 89 58 18 55 56 57 41 54 41 55 41 56
5320B0 83 fa 01 75 3e 80 b9 74 14 00 00 00 74 35 48 8b
52BFB0 48 8b c4 55 53 56 41 57 48 8d a8 48 fd ff ff 48
52AE10 48 89 5c 24 18 55 56 57 41 54 41 55 41 56 41 57
5318E0 40 55 48 83 ec 40 48 89 5c 24 50 48 8d 15 7e ad
52D680 48 89 5c 24 08 48 89 74 24 18 48 89 7c 24 20 55
52EDE0 40 53 48 83 ec 30 48 8b d9 81 fa 02 01 02 00 0f
```

UfoSmall515 的独立核查如下；其 CAS 状态不计作独立技能。

## EDF4.1 aliases

- tools/make_edf41_objects.py 的 ufo301_battery() 为 UFO301/UFO301ACE 注册六条炮口（muzzleL01/02/03、muzzleR01/02/03），每炮 4 发、period 3 frame、cooldown 5.5，全部引用唯一 bullet_object。301=BeamBullet01；ACE=LaserBullet02。
- UfoSmall515 原生按 battery_config count 动态分配/遍历（见下），故主击会驱动全六炮。不可把“原版双炮”写成类级固定数量，也不可另造六个武器切换技能。
- UFO401/ACE 经 UfoSmall507；现有生成器提供同一 LaserBullet01 单弹丸配置。

## UfoSmall515 独立复核

结论：原版每个 SGO 固定一套双炮，主击覆盖完整攻击生命周期。类级别按 SGO 动态 battery 数目遍历，所以六炮口 EDF4.1 aliases 也由同一主击全部驱动。没有切换第二武器。

原版 NORMAL/ACE/PROTOTYPE：均为两个 battery_config 条目，炮口 muzzleL01/muzzleR01，唯一 bullet_object=PulseBullet01。

| SGO | 每炮 fire row | 左/右 delay | config |
|---|---|---|---|
| NORMAL | [bullet_object,6,0,5.5,0,0,12,1,0.05] | 0/0 | [60,1,[20,240],[0,240]] |
| ACE | [bullet_object,128,0,2,0,0,30,16,0.1] | 0/15 | [180,1,[10,120],[0,120]] |
| PROTOTYPE | [bullet_object,6,0,[5.5,5.5,11,16.5],0,0,3,1,0.0125] | 0/0 | [60,1,[20,120],[0,50]] |

- 0x534272 查 battery_config；0x5342B3 取 SGO count；0x5343DA 按此数量 resize；data body+0x1298、count +0x12A8、stride 0x1B0；0x534400 循环绑定全部炮口。
- slot5 0x5371C0 在 0x53743D/0x537441 复制 pad+0x71 -> body+0x614，只读取这一攻击输入，无武器切换输入。
- Idle callback 0x539EF0：EDX==1 且 body+0x614!=0，经 0x538F60 切 body+0xE70 状态机至 Open 0x539D20。Open 等 CAS 完成及 config 计时，然后切 Fire 0x539C20。
- Fire enter 遍历全部 battery，调 0x5351C0，进 0x6CFBC0 初始化发射及每炮 delay。Fire tick 为 0x535960 -> 0x6CFC60 -> 0x6D05C0；0x6D08DE / 0x6D0935 调实际 factory virtual +8。
- 本例 factory table 0x17E6EE0、发射函数 0x6CF7D0，其 0x6CF806 -> 0x84C40 创建弹丸，随后 0x6D0220/0x6D02B0 配置和注册。
- 0x535DA0 遍历每个 battery 的 0x6D0210 完成门；全部完成切 Close 0x539E40，CAS 完成后 Idle。Fire exit EDX==2 在 0x539D00 对每炮调 0x6CFC00 清理。松开主击不强行截断原生一轮，结束后不会起下一轮。
- CAS open/open_state/close/close_state/wakeup 是该炮开闭生命周期，不能算为独立技能。现有准星重定向在 0x5351ED 目标选择边界，保留原始各炮方向求解。

本项未发现生产代码需要新增技能按键的证据。
