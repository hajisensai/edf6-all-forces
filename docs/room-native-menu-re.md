# 原生 HQ / 联机房配装菜单入口

EDF.dll TimeDateStamp `0x678CCB46`，RVA；2026-10-19。证据为本机原版二进制静态反汇编、布局读取及离线执行替身，未启动或修改游戏。

## 创建与焦点

原版 HQ 主界面的 `LeftButtonsBase2` 在布局树里起初只有任务按钮。构造函数 `0x89DA40` 根据本地玩家数量追加装备、履历、选项等按钮；7 处调用 `0x8456D0`。

`Shared* CreateByName(HUiLayout* screen, Shared* out, Shared* parentByValue, const wchar_t* memberName)`：四参分别是 RCX/RDX/R8/R9。第三参是被调方消费的强引用副本，调用前必须增加一次 uses，不能在返回后再释放同一副本。成功、父项为空、名称缺失三条路径都会消费它。

`0x8457D5` 读取父项 `+0x138` 的真实孩子列表，取得末兄弟作为相对布局基准，`0x8458BA` 调完整 SGO 工厂 `0x841800`。Button 分支经 `0x83BFF0` / `0x83D060` 创建真实 `ui::HUiButton`；原生代码将按钮强引用尾插父孩子链，同时向 screen `+0x480` 的命名控件表登记弱引用。没有手工伪造焦点邻接数组。

生成器新增 `vcLoad` 定义，房间继承 `Button03Base`、HQ 继承 `Button02Base`；保留原始皮肤与列表布局方式。房间已有 8 行按钮，最后的退出按钮下缘为 y=708，聊天框始于 y=747；新增行采用 2px 间距、34px 高、22px 字号，位于 y=710..744，不压住聊天。HQ 使用原模板尺寸。它不进入 `layout_tree`。插件校验成功后在第一次画面更新时调用同一个 CreateByName，将其加入房间 `ButtonsGroup` 或 HQ `LeftButtonsBase2`。插件未加载、被拒绝或 RoomLoadout=0 时没有实例，不产生不可见的焦点项。

## 原生事件与生命周期

`0x7EAA40(Component*, eventName, const Shared<Action>*)` 将动作的强引用副本加入控件事件列表，不消费调用方的引用。原生触发器 `0x7EAB90` 调 Action 虚表槽 1。点击事件的准确字面量是 `Click`，聚焦和失焦分别为小写 `focus`、`kill_focus`，支持原生鼠标、键盘与手柄导航。

插件没有自建跨 CRT 的共享控制块。`0x811FC0` 从画面原生 enable_shared_from_this 取得一个弱引用；`0x8FB930` 用它和 16 字节 MSVC 成员函数指针构造游戏自己的弱画面回调。工厂消费传入弱引用。适配器 `0x8F8A40` 先尝试锁定原控制块；画面已销毁则不调用。它从动作 `+0x18` 读函数、从 `+0x20` 读 this 调整量，插件提供函数地址与 0 调整量。

Click 回调仅排入一次打开请求；原画面的本帧更新与事件列表遍历返回后，使用与 F2 相同的菜单打开路径加载配置。当前帧原生确认键已经被记录，下一帧不会再进入配置行。输入被遮挡、菜单正在关闭、配置被停用、画面析构或被替换时，排队请求失效。菜单开启期间仍沿用真实 ctx+8 置空的输入所有权路径。

原生 `+0x1F8` 仅控制绘制，并不影响焦点候选；导航检查 `+0x70` 的 current enabled。真正的 `SetEnabled 0x7EE280` 先写 pending `+0x71`，子控件 Update 再提交 current，但父列表先做本帧导航。隐藏入口时因此在调用原生 setter 后，同步关闭插件自己按钮的 current 状态；显示时只用 setter，让子 Update 正常处理皮肤转场。`HasFocus 0x7EBDB0` 检查当前 FocusAgent 身份；仍占有焦点时，先确认原菜单目标 current enabled，再用 `ComponentFocus 0x7EB360(target, true)` 移交，原生发送 kill_focus / focus。动作分配失败时也先禁用刚创建的按钮，不依赖 ready 状态，避免失败分支留下隐藏焦点。

MissionStart 只要求清理配装事务，不等于房间对象已经析构。`ForgetMenu` 清菜单、同步及未消费请求，保留该画面的已创建按钮/回调身份；同一房间跨关恢复不会追加第二个按钮。只有画面身份变化或真正的析构钩子结束该实例时才清除 nativeEntry。

## 按钮文字

HUiButton 不继承 HUiTextField，不能直接使用 `0x8859C0` 转型或把它传给 `0x863690`。按钮 `+0x220/+0x228` 是内部 `ui::TextField` 的弱引用，原版 RTTI 与 `0x7E7847` 写入链可证实。`0x83E4F0` 将其锁定为强引用后，调用 `0x7F8BB0(ui::TextField*, text, optionalFontParams)`，第三参空，与原版 `0x7E782B` 相同，沿用原生字体。

按钮显示已有本地化标题，focus 时写入配装说明及实际快捷键。原有菜单项的 focus 回调会恢复自身说明；插件不修改原版 HQ 描述列表索引。原始 F2 / L3+R3 和底部提示继续可用。

## 验证范围

`tests/room_native_menu_contract_test.py` 对实际 DLL 完整函数散列、参数寄存器、引用消费、原版调用点、父子列表/命名表、RTTI、文字链做只读检查。`room_ui_test` 覆盖单次请求、遮挡/关闭/析构失效、地址复用不继承状态和确认键不穿透。`room_layout_test.py` 验证两份原布局去掉新增内容后逐字节相同，vcLoad 定义不出现在树中。

未做游戏鼠标/手柄手感、不同房间状态及双机联机验收。
