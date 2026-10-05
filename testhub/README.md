# 测试站 testhub（https://edf6.fushi.moe）

开发者和测试者共用的一页：下载测试版、看要测什么、提交结果（附录屏 / 日志）、看开发者回复。
Cloudflare Worker + D1（测试版、测试项、反馈的元数据）+ R2（安装包与附件，桶 `edf6-testhub`），整站要登录。

## 三条路

| 谁 | 做什么 | 怎么做 |
|---|---|---|
| CI | 推 main → 本站 `nightly`；在任意分支手动运行 Build → `test`（修复合并前先发给测试者） | `.github/workflows/build.yml` 的「Mirror to the test site」，需要仓库 secret `EDF6_TESTHUB_AUTH`（开发者账号 `user:password`） |
| 测试者 | 安装器 3 = 下载最新测试版（校验 SHA-256，解压到 exe 旁边）；4 = 回传日志 | `tools/installer.py` → `tools/testhub.py`。回传带上 Mods/Plugins 的全部 `.log`（各留最后 12 MB）/`.ini`、ModLoader 日志、安装清单、各 DLL 的 sha256 与 PE 时间戳、三天内最新的 EDF6 崩溃转储。游戏开着也能传（插件日志是共享读打开的）。账号第一次输入后存在 `%APPDATA%\EDF6AllForces\testhub.json` |
| 开发者 | 看反馈、拉日志、回复、改测试项、手动发包 | `python tools/testhub.py state / pull / reply / case add|set / publish`（`EDF6_TESTHUB_AUTH=user:password`） |

测试项状态：`todo` 待测 / `verify` 已修待验证 / `pass` / `fail` / `closed`。测试者提交「还有问题」时该项自动变 `fail`。

## 账号

secret `ACCOUNTS = "user:password:role,..."`（role `tester` / `dev`）、`SESSION_SECRET`（cookie 签名）。
浏览器用登录页拿 60 天 cookie（签名里带密码：改密码即全部下线）；脚本和安装器用 HTTP Basic。
改账号：`wrangler secret put ACCOUNTS --config wrangler.toml`。

## 部署

```
npm i wrangler@4
node node_modules/wrangler/bin/wrangler.js d1 execute edf6-testhub --remote --file schema.sql --config wrangler.toml
node node_modules/wrangler/bin/wrangler.js deploy --config wrangler.toml
```

一律带 `--config wrangler.toml`（裸 `wrangler deploy` 在没有配置的目录会以目录名新建一个公开 Worker）。
本地：`.dev.vars` 写 `ACCOUNTS=` 与 `SESSION_SECRET=`，`wrangler d1 execute edf6-testhub --local --file schema.sql`，再 `wrangler dev`。

限制：单次上传（附件合计）≤ 95 MB（Workers 请求上限 100 MB）；R2 只保留最近 15 个测试版（`KEEP_BUILDS`）。
