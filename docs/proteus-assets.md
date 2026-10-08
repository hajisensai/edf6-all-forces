# 普罗透斯护盾模型接口

`tools/make_proteus.py` 只读玩家的 Root.cpk，生成两版普罗透斯的私有 MRAB/CAS，并给全部 10 个 `VehicleBigBegaruta` SGO 改模型/动画引用。保留原始 MDB 骨骼编号、网格、逆绑定矩阵、CAS 动画轨道及四个引擎座位；关闭两个玩家入口由运行时处理，不能删除 seats 2/3 而破坏原版炮口和武器索引。

护盾使用 `WEAPON/H_SHIELD_ENERGY01.RAB` 内真实的 `snd_Chara_FencerEnergyShield` 材质和纹理。新增 36 个 root 子骨 `vc_ps_00` 到 `vc_ps_35`，MDB bind local/inverse bind 均为单位矩阵。每块皮肤只有对应骨一个权重，12 顶点、8 三角形，正反两面，基准面板位于模型正前方：半径 11 米、角度 −5° 到 +5°、高度 0.3 到 16 米。

每个 CAS clip 增加这些骨的零缩放轨道；没有 DLL 或功能关闭时保持隐藏。原 clip、channel 与 key payload 保持原值，只迁移 channel/name 表并更新相对偏移。运行时后置模型更新 hook 必须完整替换面板 local，不能在零矩阵上相乘。

给定护盾总角度 `arc`（10 到 360 度），面板数 `n=ceil(arc/10)`，每片角宽 `w=arc/n`。第 i 片先以 `tan(w/2)/tan(5°)` 缩放 X，再绕 Y 旋转 `-arc/2+(i+0.5)*w`，最后加护盾朝向；i≥n 或护盾关闭时写零缩放。默认 120 度是 12 块面板，全向 360 度是 36 块。

安装接线：`make_proteus.OWNER='proteus'`，`build(root)` / `install(root, files)` / `remove(root)` 与其它资源组一致，需要进入安装器、卸载器、构建缓存 recipe 和冻结打包 import 集合。文件依赖与输出应交由 ledger/cache 保持同步。

安装事务另有 `Mods/.edf6vc_proteus.json` 写前日志和 `.edf6vc_proteus_backup/` 原文件备份。每次覆盖前持久化原始哈希、原 ledger 项和 pending 哈希，文件或 ledger 提交后中断均可恢复。已有原版模型的第三方 SGO 只改模型/动画引用，其余字段保留；第三方自定义模型不接管，安装后改过的文件重装也不覆盖。卸载先恢复未改动的消费者，再扫描剩余 loose SGO 对私有模型/CAS的引用，保留仍被引用的整对资源及其他 owner 需要的资源。备份作为可恢复记录保留。

测试场不能直接从 Root 复制 Proteus SGO 后仍指原版资源。生成写入处应调用 `data, needs = make_proteus.range_vehicle(led, game, data, OWNER)`，把 needs 加入本轮 held 集合，再 `led.put(OWNER, ..., data)`。该入口与主安装共用 `redirect(data)`；已安装资源登记 need，独立测试场在资源缺失时生成同一 MRAB/CAS。务必传当前持久使用的同一个 Ledger 实例，避免旧实例保存时覆盖依赖登记。

`python tools/test_proteus_assets.py` 包含可在无游戏机器运行的 CANM 和几何测试；本机 Root.cpk 存在时另核对完整生成结果。2026-10-08 共 6 项通过，无跳过。未启动游戏，未把生成资源写入游戏目录；原生 CAS 加载器与实际屏幕呈现仍须另行验收，文件解析成功不能代替渲染验证。
