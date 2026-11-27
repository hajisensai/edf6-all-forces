# 测试站 testhub（https://edf6.fushi.moe）

开发者和测试者共用的一页：下载测试版、看要测什么、提交结果（附录屏 / 日志）、看开发者回复。
Cloudflare Worker + D1（测试版、测试项、反馈的元数据）+ R2（安装包与附件，桶 `edf6-testhub`），整站要登录，例外是公开下载页和三个策划页的只读查看（见下面「游客」）。

## 三条路

| 谁 | 做什么 | 怎么做 |
|---|---|---|
| CI | 推 main → 本站 `nightly`；在任意分支手动运行 Build → `test`（修复合并前先发给测试者） | `.github/workflows/build.yml` 的「Mirror to the test site」，需要仓库 secret `EDF6_TESTHUB_AUTH`（开发者账号 `user:password`） |
| 测试者 | 安装器 3 = 下载最新测试版（校验 SHA-256，解压到 exe 旁边）；4 = 回传日志 | `tools/installer.py` → `tools/testhub.py`。回传带上 Mods/Plugins 的全部 `.log`（各留最后 12 MB）/`.ini`、ModLoader 日志、安装清单、各 DLL 的 sha256 与 PE 时间戳、三天内最新的 EDF6 崩溃转储。游戏开着也能传（插件日志是共享读打开的）。账号第一次输入后存在 `%APPDATA%\EDF6AllForces\testhub.json` |
| 开发者 | 看反馈、拉日志、回复、标处理状态、改测试项、手动发包 | `python tools/testhub.py state / pull / reply [--status S] / status ID S / case add|set / publish`（`EDF6_TESTHUB_AUTH=user:password`） |

测试项状态：`todo` 待测 / `verify` 已修待验证 / `pass` / `fail` / `done` 已完成 / `closed`。测试者提交「还有问题」时该项自动变 `fail`。

反馈的处理状态（页面「反馈记录」上方一览表的「处理状态」列，开发者在表里直接改，其他人只读）：
`open` 待处理（新反馈默认）/ `doing` 处理中 / `verify` 已修待确认 / `done` 已完成 / `closed` 不处理。
反馈人跟进自己的反馈（测试者，或者把 QQ 群里的反馈整理过来的机器人「かぐや」，账号 `kaguya`）：
`GET /api/report/mine?ids=1,2` 看开发者回复和处理状态；`POST /api/report/append`（multipart：`report_id`、`note`、`file`）追加说明和附件，
说明接在原文后面、标上「补充」和时间；`POST /api/report/withdraw {report_id}` 撤回，只在「待处理 / 处理中」时可以（变成「不处理」），
开发者给过结论的回 409。
`POST /api/report/retract {report_id, contains?}` 撤回自己最后一段「补充」（`contains` 非空时撤回内容含它的最后一段；附件不动），
只在「待处理 / 处理中」时可以（否则 409），没有补充回 404，返回 `{ok, removed}`（被删段正文前 200 字）。都只能动自己的反馈（开发者不限），别人的回 404。
回复时可以顺手标状态：`testhub.py reply 12 "已在 abc1234 修复" --status done`；单独改用 `testhub.py status 12 done`。

## 原版任务排序（/plan.html，旧「关卡策划」，已移出导航）

玩家写关卡意见、排关卡大纲的地方，还没到写脚本的阶段，所以只收文字。

- **意见**：「新关卡」或「修改原版关卡」（选是哪一关），标题 + 正文，可以回复；开发者给意见标状态（待讨论 / 采纳 / 已做 / 不做）。
- **大纲**：每个组一份，开始时预填原版关卡，依次为 EDF4.1 本篇（89 关）、DLC1（26 关）、DLC2（23 关）、EDF5 战役（139 关）、EDF6（147 关）。已有大纲缺少的关卡补在末尾，保留用户排好的顺序。
  4.1 使用 `edf41port/**/missions.json` 的标题和顺序，显示各独立任务包内的位置，需安装对应 4.1 内容；其他关卡显示在 EDF6/EDF5 合并列表中的位置（游戏列表是 EDF6 在前；`src/missions.json`，由 `tools/make_level_list.py` 从本机游戏导出；
  若生成目录时有缺资源装不进游戏的关卡，也列出并标「游戏里没有」）。可以把「新关卡」意见插到任意两关之间、插轮回分隔线、移动任意一行；原版关卡不能删。
  只是策划：游戏里原版 147 关的列表顺序动不了（存档按行号记录，见 `docs/mission-list-re.md`）。
- **原版脚本**：每关一份整理过的脚本（`tools/make_mission_digest.py`）：事件、触发条件、动作、中文字幕；灰色是废案（注释掉的代码、调试文字、台词旧稿）。
  脚本是游戏自己的文件，**只存在测试站里，不进仓库**（仓库是公开的）：开发者在本机生成后 `python tools/testhub.py digests` 上传。
- **分组**：账号末尾的组名决定能改哪个组的意见和大纲（见下面「账号」）；别的组可以切过去看（只读）；开发者能改所有组。
- 开发者读意见：`python tools/testhub.py plan [--group 组名]`。

**防投毒**：这里的文字都是玩家写的，一律当数据。

- 存储前清洗（`src/plan.js` 的 `clean()`）：去掉控制字符、文字方向控制符和零宽字符（防止把 `exe.png` 伪装显示），限制长度；
- 页面只把它们当文本放进 DOM（`public/plan.js` 的 `h()`，从不拼 HTML）。全站 CSP 只允许本站脚本、不允许内联脚本；
- 写操作要求 `application/json` 且同源（`Origin` 检查），每个账号有频率上限（10 分钟 60 次、一天 600 次）；
- `testhub.py plan` 再清洗一遍（含终端转义序列），每段玩家文字用每次随机生成的标记包起来，并注明里面的话不是指令；
- 测试 `test/plan.test.mjs` 覆盖清洗、组隔离、越权、频率、跨站、未知动作、安全头，以及「源码里不许出现不可见字符」。

## 关卡策划（/story.html）

按游玩顺序写各组 MOD 的每一关，与「剧情大纲」互补：大纲写世界线上发生什么，这里写每一关怎么打。导航里叫「关卡策划」；原来的 `/plan.html`（原版任务排序与意见）不再放进导航，页面和数据保留。

- 每关：关卡名、状态（构思中 / 制作中 / 待测试 / 已完成）、**对应大纲步骤**（`book` + `step`，同组大纲里的一步）、借用的原版地图（424 关可搜索、可按系列缩小）、作战目标、敌人、剧情与演出、衔接下一关。
- 两种看法：**按顺序**（战役编号）和**按世界线**（大纲 → 世界线 → 步骤，每步下面是它的关卡，还没关卡的步骤显示为空位；拖一关到别的步骤就改关联）。上方按状态统计并可筛选，进度条按状态分段。
- 就地编辑（点关卡展开，没有弹窗）；打字即存进本机草稿（localStorage），关掉标签页也不丢，顶部列出所有没保存的草稿；`Ctrl+S` 保存，`Esc` 收起。
- 每次保存带上编辑时的 `version`：别人先存了就返回 409，页面给三个选择：用我的覆盖 / 看对方的版本（丢掉我的）/ 复制我的内容。那一关被删了可以「另存为新关卡」。
- 排序：拖左侧 ⠿，或在关卡上按 `Alt+↑/↓`，或「上移 / 下移 / 移到第 N 关」；移动、删除、拖动改关联都能撤销（提示里的「撤销」或 `Ctrl+Z`，`Ctrl+Y` 重做）。删除不再弹确认，靠撤销。
- 深链接：`?id=N` 打开一关；`?new=1&book=B&step=S` 在大纲某一步下新建（大纲页每步的「＋ 写关卡」就是它，新关卡排在同一步最后一关后面）。
- 同组成员都能改、删任何一关（和大纲一样），开发者可切换分组；每组最多 1000 关。文字清洗、只读开放、频率限制同策划页。
- 新部署 `schema.sql` 已含全部列；已有库执行一次 `migrations/0008-story-mission-fields.sql`（**不可重复执行**：ADD COLUMN 第二次会报错）。测试 `test/story.test.mjs`。

## 剧情大纲（/outline.html）

每组可以有好几份剧情大纲（页面上一排按钮切换，`?book=N` 直接打开第 N 份；同组成员都能新建、改、删，最多 20 份）。每份大纲：

- **世界线总图**：每条世界线一个方框，箭头表示「接在哪条线后面」，方框里是步数、关卡数和每一步一个圆点（实心 = 已经写了关卡）；点方框跳到那条线。
- 下面是各条线的时间线，汇合处画箭头；每一步下面列出关卡策划里关联到它的关卡（点进去打开那一关），组员还有「＋ 写关卡」。`#step-<id>` 定位到某一步。
- 「编辑」后就地改：点一步展开改标题 / 标签 / 颜色 / 内容；线名直接改，说明、颜色、汇合关系在「说明、颜色、接在哪些线后面」里。拖 ⠿ 或 `Alt+方向键` 把一步移到别的位置、别的线；`Delete` 删除选中的一步。所有改动都能撤销 / 重做（按钮或 `Ctrl+Z` / `Ctrl+Y`）。
- 打字即存本机草稿；回到这份大纲时提示「恢复并继续编辑 / 丢弃」，切换大纲或组不再丢改动。
- 整份一次保存（`Ctrl+S` 或「保存」，`POST /api/outline/save` 带 `book` 和 `version`；`version` 为 0 是新建一份）：别人先存了返回 409，页面给「用我的覆盖 / 看最新版 / 复制我的为文字」。删除一份用 `POST /api/outline/remove`。
- **主大纲 / 所有账号可编辑**：开发者用 `POST /api/outline/settings`（`{group, book, main, open}`）设置。主大纲（最多一份）所在的组排第一、不带参数打开页面时默认显示它，按钮前有 ★；`open` 的大纲任何登录账号都能改并保存（不限组），但新建、删除仍只限本组成员和开发者。页面标题旁显示「主大纲」「所有账号可编辑」。迁移 `0009`（不可重复执行）。
- 存在 `story_outlines` 表（每组每份一行，JSON；历史迁移 `0003`–`0006`、`0009`）。`src/outline.js` 的 `normalize()` 只保留已知字段，最多 12 条线、每条 100 步。
- 评分界面已撤掉（大纲转为正式大纲）；`outline_ratings` 表和 `/api/outline/rate` 接口保留，已有评分不删。

## 设定集（/design.html）

剧情要用到、需要设计或制作的东西，按分类列出：角色、载具、武器、装备、地图、怪物、其他。

- 每条：名称、分类、状态（待设计 / 设计中 / 制作中 / 已完成）、阵营 / 归属、外观、设定与玩法、参考，以及**出现在大纲哪几步**（同组一份大纲里的若干步）和**用到它的关卡**（关卡策划里的若干关）。
- 页面顶部可按分类、状态筛选，可搜索（`/` 跳到搜索框）；点一条就地展开编辑，打字即存本机草稿，`Ctrl+S` 保存，`Esc` 收起；保存带版本号，别人先存了返回 409，给「用我的覆盖 / 看对方的版本 / 复制我的内容」；删除后提示里可撤销。`?id=` 打开某条，`?new=1&kind=&book=&step=&mission=` 预填新建。
- 大纲每一步下面列出关联到它的设定（和「＋ 设定」），关卡策划每一关里列出「用到的设定」。
- 权限：谁都能看（访客看不到作者）；本组成员和开发者能写。**组里有对所有账号开放的大纲时**（见上文 `open`），任何登录账号都能新增、编辑这个组的设定和关卡策划里的关卡、调关卡顺序；删除仍只限本组成员和开发者。不带 `?group=` 打开大纲、关卡策划、设定集时，默认显示主大纲所在的组。
- 接口：`GET /api/design?group=`，`POST /api/design/save`（`id` + `version` 为修改，不带 `id` 为新增），`POST /api/design/remove`。存在 `story_designs` 表（迁移 `0010`，可重复执行），`src/design.js`；每组最多 1000 条。

## 剧情大纲外部评分（/rate.html，已下架）

目前没有展示中的大纲（`review-publish` 发了空列表），已有评分保留在结果里；导航里也去掉了入口，需要时用下面的命令重新发布。

给组外的人盲评剧情大纲：不用账号，几份大纲按每个访客各自随机的顺序逐份展示（不显示组、作者、编辑者），中文 / English / 日本語 切换。网址后缀定语言：`/rate/en`、`/rate/ja`、`/rate/zh`（发给外国评审用这个；`?lang=en` 也行），不带后缀时按上次选的、再按浏览器语言。
每份打 1–5 分并可写评论（昵称可空），也可以只写评论；同一浏览器能改或删自己的。打过分后才显示这份的平均分（免得先看到别人的分受影响）；别人的评论和昵称不对访客展示，服务器也不下发，只有开发者看得到：开发者账号登录后打开 /rate.html，页面底部有「评分结果」（每份平均分、分语言、分数分布、全部评论），命令行是 `review-results`。

- 展示的是**快照**：开发者把某份大纲在某个已保存版本连同英、日译文发布，之后组里再改大纲不影响已发布的文字；要更新就重新翻译、重新发布（同一份新版本算新条目，旧版本的评分留在结果里）。
  `python tools/testhub.py review-source src.json` 导出各组当前大纲 → 翻译成 `[{group, book, version, zh_title?, en, ja}]`（`en` / `ja` 是与原文同结构、同编号的整份大纲，`zh_title` 可改中文标题）→
  `python tools/testhub.py review-publish items.json`（整组替换当前展示的大纲，空列表 = 全部下架）→ `python tools/testhub.py review-results` 看各份平均分（分语言）与全部评论。
- 评审人身份是浏览器 localStorage 里的随机令牌，库里只存它的 SHA-256；访客写操作按 IP 限频（10 分钟 30 次、一天 200 次，只存 IP 的带密钥哈希），每份最多 5000 条。
- 文字清洗同策划页，页面只当文本放进 DOM；`review-results` 把访客文字用随机标记包起来。
- 已有库执行一次 `migrations/0007-outline-review.sql`（可重复执行）。测试 `test/review.test.mjs`。

## 游客（不登录）

原版任务排序（`/plan.html`）、关卡策划（`/story.html`）、剧情大纲（`/outline.html`）三页没账户也能看，只读：

- 组列表只列写了内容的组（策划：有意见或放进了新关卡 / 轮回分隔线；剧情关卡：至少一关；剧情大纲：至少一条任务线）；
  空组只对该组自己的成员显示（好开始写）；开发者也只看到有内容的组和自己的组，要开一个空组用 `?group=组名`。
  页面上组列表是一排按钮，点一下就切换。登录的测试者也能切到别的组看，但只能改自己组的。
- 游客看不到作者 / 回复人 / 评价人 / 「最后由谁保存」，也看不到原版脚本（游戏自己的文件，`/api/plan/digest` 仍要登录）。
- 所有写操作仍要登录。账号或密码填错仍是 401，不当成游客。

## 账号

secret `ACCOUNTS = "user:password:role[:组名],..."`（role `tester` / `dev`；组名可省，省了就在「未分组」里，组名只能是字母、数字、`_`、`-`，最长 32 个字符）、`SESSION_SECRET`（cookie 签名）。
浏览器用登录页拿 60 天 cookie（签名里带密码：改密码即全部下线）；脚本和安装器用 HTTP Basic。
改账号：`wrangler secret put ACCOUNTS --config wrangler.toml`。

新增账号也可以单独存为 `ACCOUNT_<大写字母或数字或下划线>` secret，值仍为 `user:password:role[:组名]`，例如
`wrangler secret put ACCOUNT_PLANNER2 --config wrangler.toml`。这样无需读取或替换原有加密的 `ACCOUNTS`。
独立配置中的重名账号不会覆盖原有账号（包括密码、角色、分组和现有会话）；不同独立配置重名时按配置名排序，第一份生效。
这些配置仅由 Cloudflare 管理员维护，不读取请求中的账号配置；密码不得写入源码或普通 vars。删除对应 secret 即撤销该账号。

## 部署

```
npm i wrangler@4
node node_modules/wrangler/bin/wrangler.js d1 execute edf6-testhub --remote --file schema.sql --config wrangler.toml
node node_modules/wrangler/bin/wrangler.js deploy --config wrangler.toml
```

一律带 `--config wrangler.toml`（裸 `wrangler deploy` 在没有配置的目录会以目录名新建一个公开 Worker）。
本地：`.dev.vars` 写 `ACCOUNTS=` 与 `SESSION_SECRET=`，`wrangler d1 execute edf6-testhub --local --file schema.sql`，再 `wrangler dev`。
`schema.sql` 全是 `IF NOT EXISTS`，加了新表后对线上库再执行一次即可，不动已有数据。
给已有表加列走 `migrations/`（按编号各执行一次，`--file migrations/0001-report-status.sql`）：`schema.sql` 已含这些列，新建的库不用执行。

测试：`node --test test/auth.test.mjs test/plan.test.mjs`（Node 22.5+，用内置 `node:sqlite` 加载真实 `schema.sql` 模拟 D1）。

关卡策划上线：执行 schema → 部署 → `python tools/make_level_list.py`（只在游戏更新、关卡列表变了时）→
`python tools/make_mission_digest.py` → `EDF6_TESTHUB_AUTH=dev:… python tools/testhub.py digests` → 给 `ACCOUNTS` 里的人加组名。

限制：单次上传（附件合计）≤ 95 MB（Workers 请求上限 100 MB）；R2 只保留最近 15 个测试版（`KEEP_BUILDS`）。
测试版超过 95 MB（2026-10-10 起安装包约 113 MB，CI 镜像一直 413）时 `testhub.py publish` 自动改走分块：
`POST /api/build/start` → 每块 32 MB 一次 `PUT /api/build/part` → `POST /api/build/finish`（带各块 etag、总大小和上传方算的整包 SHA-256，
大小对不上就丢弃）；中途失败调 `/api/build/abort`。整包 SHA-256 由上传方计算（Worker 的 CPU 预算算不完 100 MB+），安装器下载后按它校验。
每次分块上传在独立临时对象完成并核对大小，再用流和原子条件写入发布对象；失败不会覆盖或删除已有同名包。
已发布文件名不可复用（单次上传和分块上传均返回 409），重发请使用新的构建文件名。升级前尚未完成的上传需重新 start；upload_id 对客户端仍是不透明字符串。

## 公开下载页（/download）

全站唯一不用登录的地方：最新分享包，链接可以直接发给玩家（https://edf6.fushi.moe/download）。两个槽位各放最新一版：
`main` 主包、`models` 模型包（主包的 `models-required.json` 写明要哪一版模型包）。
一版可以有多个文件：模型包按 QQ 群 4 GB 上限分成 `…-1of2.zip`、`…-2of2.zip` 等（即 `models-required.json` 的 `model_packs`），
清单 `files` 按顺序列出，页面每个文件一个下载按钮，并提示全部都要下载、解压到同一个文件夹。
存储和下载与 fushi.moe 的推荐包同一套：每个文件切成 64 MiB 的分片（`share/<slot>/<version>/<k>/part-NNN`，k 是文件序号），清单写明每片的 offset、长度和 SHA-256；
页面（`public/download.js` + 从 fushi.moe 原样复制的 `public/chunked-download.mjs` / `public/pack-download.mjs`，改动请两边同步）
每片开 4 条 Range 连接并发下载，校验后写进用户选的文件（Chrome / Edge 流式写盘，其他浏览器在内存里拼好再保存）。
「普通下载」`/share/<slot>/<version>/<k>/file` 把文件 k 的各片按顺序拼成一个流，给 IDM 等下载工具和不支持分片的情况用。

发布：`python tools/testhub.py share <zip>... --slot main|models [--version V] [--notes TEXT] [--part-mb N]`（开发者账号；多个 zip 即同一版的多个文件，按给出的顺序，必须带 `--version`；`--part-mb` 是每个上传请求的大小，默认 64，上行慢时用 8 之类的小值，避免单个请求太久被断开，每个文件最多 999 片）。每片上传时带 SHA-256，由 R2 校验；
每片先 `GET /api/share/part`（同样的查询参数，不带数据）问一句，已存在且哈希、大小相同的片不再发送，所以断线后原样重跑只会补传缺的片。最后一步 publish 一次带上全部文件，逐个核对分片首尾相接、大小与哈希，再切换该槽位的 `latest.json`；
每个槽位只留这一版，旧版本全部删除（省 R2 空间；正在下载旧版的人会中断，刷新页面拿新版）。
