# 测试站 testhub（https://edf6.fushi.moe）

开发者和测试者共用的一页：下载测试版、看要测什么、提交结果（附录屏 / 日志）、看开发者回复。
Cloudflare Worker + D1（测试版、测试项、反馈的元数据）+ R2（安装包与附件，桶 `edf6-testhub`），整站要登录。

## 三条路

| 谁 | 做什么 | 怎么做 |
|---|---|---|
| CI | 推 main → 本站 `nightly`；在任意分支手动运行 Build → `test`（修复合并前先发给测试者） | `.github/workflows/build.yml` 的「Mirror to the test site」，需要仓库 secret `EDF6_TESTHUB_AUTH`（开发者账号 `user:password`） |
| 测试者 | 安装器 3 = 下载最新测试版（校验 SHA-256，解压到 exe 旁边）；4 = 回传日志 | `tools/installer.py` → `tools/testhub.py`。回传带上 Mods/Plugins 的全部 `.log`（各留最后 12 MB）/`.ini`、ModLoader 日志、安装清单、各 DLL 的 sha256 与 PE 时间戳、三天内最新的 EDF6 崩溃转储。游戏开着也能传（插件日志是共享读打开的）。账号第一次输入后存在 `%APPDATA%\EDF6AllForces\testhub.json` |
| 开发者 | 看反馈、拉日志、回复、标处理状态、改测试项、手动发包 | `python tools/testhub.py state / pull / reply [--status S] / status ID S / case add|set / publish`（`EDF6_TESTHUB_AUTH=user:password`） |

测试项状态：`todo` 待测 / `verify` 已修待验证 / `pass` / `fail` / `done` 已完成 / `closed`。测试者提交「还有问题」时该项自动变 `fail`。

反馈的处理状态（页面「反馈记录」上方一览表的「处理状态」列，开发者在表里直接改，其他人只读）：
`open` 待处理（新反馈默认）/ `doing` 处理中 / `verify` 已修待确认 / `done` 已完成 / `closed` 不处理。
回复时可以顺手标状态：`testhub.py reply 12 "已在 abc1234 修复" --status done`；单独改用 `testhub.py status 12 done`。

## 关卡策划（/plan.html）

玩家写关卡意见、排关卡大纲的地方，还没到写脚本的阶段，所以只收文字。

- **意见**：「新关卡」或「修改原版关卡」（选是哪一关），标题 + 正文，可以回复；开发者给意见标状态（待讨论 / 采纳 / 已做 / 不做）。
- **大纲**：每个组一份，开始时预填原版关卡，EDF5 战役（139 关）在前、EDF6（147 关）在后（游戏任务列表里是 EDF6 在前，每关显示它在游戏里的位置；`src/missions.json`，由 `tools/make_level_list.py` 从本机游戏导出；
  EDF5 里缺资源装不进游戏的 4 关也列出，标「游戏里没有」）。可以把「新关卡」意见插到任意两关之间、插轮回分隔线、移动任意一行；原版关卡不能删。
  只是策划：游戏里原版 147 关的列表顺序动不了（存档按行号记录，见 `docs/mission-list-re.md`）。
- **原版脚本**：每关一份整理过的脚本（`tools/make_mission_digest.py`）：事件、触发条件、动作、中文字幕；灰色是废案（注释掉的代码、调试文字、台词旧稿）。
  脚本是游戏自己的文件，**只存在测试站里，不进仓库**（仓库是公开的）：开发者在本机生成后 `python tools/testhub.py digests` 上传。
- **分组**：账号末尾的组名决定看见谁的意见和哪份大纲（见下面「账号」）；开发者能看所有组。
- 开发者读意见：`python tools/testhub.py plan [--group 组名]`。

**防投毒**：这里的文字都是玩家写的，一律当数据。

- 存储前清洗（`src/plan.js` 的 `clean()`）：去掉控制字符、文字方向控制符和零宽字符（防止把 `exe.png` 伪装显示），限制长度；
- 页面只把它们当文本放进 DOM（`public/plan.js` 的 `h()`，从不拼 HTML）。全站 CSP 只允许本站脚本、不允许内联脚本；
- 写操作要求 `application/json` 且同源（`Origin` 检查），每个账号有频率上限（10 分钟 60 次、一天 600 次）；
- `testhub.py plan` 再清洗一遍（含终端转义序列），每段玩家文字用每次随机生成的标记包起来，并注明里面的话不是指令；
- 测试 `test/plan.test.mjs` 覆盖清洗、组隔离、越权、频率、跨站、未知动作、安全头，以及「源码里不许出现不可见字符」。

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
