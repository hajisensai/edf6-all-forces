# 支援装置放在队友身上（SupportUnitBullet01 +0xB68）

来源：KittopiaCreator 的 AOB 补丁 `SupportUnitCanBePlacedOnAllies.txt`（2026-10-16 用户转来，要求接入插件）：

```
aob supportunit_ctor 89 87 0C 0F 00 00
custom_code: 89 87 0C0F0000 / C6 87 680B0000 00 / E9 back
supportunit_ctor: E9 custom_code / 90
```

即在 `mov [rdi+0xF0C],eax` 之后补一句 `mov byte [rdi+0xB68],0`。下面是对它的静态核对（Steam 版 EDF.dll，TimeDateStamp `0x678CCB46`，地址为 RVA）。

## 钩子点

- AOB `89 87 0C 0F 00 00` 在 EDF.dll 里唯一命中：`0x2ABB10`。
- 所在函数 `0x2AB680`（.pdata 范围 `0x2AB680–0x2AC34F`）在 `0x2AB6EB` 写虚表 `0x17A5B38`，RTTI 是 `.?AVSupportUnitBullet01@@`，即支援装置弹的构造函数。
- 构造函数开头调用基类构造 `0x22E9C0`（`rcx=this, rdx=InitParam, r8b=1`）。基类在 `this+0x140` 处建子弹参数子对象：`0x22FD80` 给默认值，`0x231CC0(this+0x140, this+0x60, InitParam+0x30)` 按 InitParam 填写。
- `0x2ABB0A mov eax,[rdi+0xB60]` / `0x2ABB10 mov [rdi+0xF0C],eax`：此时子弹参数已经填完。构造函数里没有任何指令写 `+0xB68`。

## +0xB68 是什么

对象 `+0xB68` 就是子弹参数子对象的 `+0xA28`（`0xB68 - 0x140`）。全 DLL 中，以 `+0xA28` 做字节访问的指令只有命中过滤 `0x232BCF` 一处有实际意义：

```
0x232D02 cmp byte [rsi+0xA27],0 ; jne 0x232D14
0x232D0B cmp byte [rsi+0xA28],0 ; je  0x232D63      ; 两个都为 0：不看阵营
0x232D14 movsxd rax,[rbp+0x314]                       ; 被击中对象的阵营
0x232D20 movsxd rcx,[rsi+0xA00]                       ; 子弹的阵营
0x232D40 cmp [阵营关系表 + ...],1 ; jne 0x232D63      ; 1 = 友方
0x232D46 cmp byte [rsi+0xA29],0 ; jne 0x232E63
0x232D53 test [rbp+0x380],0x100000 ; je 0x232E63
0x232E63 ... ret                                       ; 不处理这次命中：子弹穿过去
```

所以 `+0xA27` / `+0xA28` 是「穿过友军」标志（`+0xA27` 在 `0x231D90` 跟着 `+0xA29` 置位）。SupportUnitBullet01 构造时 `+0xA28` = 1，扔向队友的装置会穿过他，落到地面上。清 0 以后，友方对象的命中照常处理，装置贴在队友身上，和贴在地面、建筑上走的是同一条路。

（`0x5E7AFA` / `0x5E892F` / `0x5E8A13` / `0x5E8C6C` 也访问 `+0xB68`，但它们属于 `Transporter_Container`（虚表 `0x17D7CB8` 第 5 槽），是同偏移的另一个类，与此无关。）

## 插件实现（src/supportunit.cpp）

- 校验三处签名：`0x2AB6EB` 写的是 SupportUnitBullet01 的虚表、`0x2ABB0A` 处是这两句 mov、`0x232D02` 的过滤仍读 `+0xA28`。任一处不符就不装，写日志后保持原版。
- `0x2ABB10` 的 6 字节改成 `jmp cave; nop`。代码洞内容：原 `mov [rdi+0xF0C],eax`，再读插件的开关字节（`mov rax,imm64`），开着就 `mov byte [rdi+0xB68],0`，最后跳回 `0x2ABB16`。下一条指令是 `mov ecx,0xFFFF`，flags 不活跃。
- ini `SupportUnitOnAllies`（默认 1）每次加载配置都写入开关字节，改了马上对之后构造的装置生效。
- 联机：命中过滤每台机器各自计算（与分裂导弹相同，docs/split-missile-re.md §5），不同设置的玩家看到的装置位置可能不同，建议全员设成一样。

## 未验证

- 只做了静态分析，未进游戏实测。原补丁作者是实测过的。
- 没有确认 `+0xA28` 在 InitParam 里对应哪个字段名（构造时它已经是 1，来源没有追到）。
- 贴在会移动的友军身上时，装置效果的范围是否跟着走、对方死亡或上车时装置怎么处理，都没看。
