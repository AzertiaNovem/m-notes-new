# 错题本 · C++ 嵌入式版

这是第二个项目中的独立实现。前端延续原项目的 React、纸张配色、导航、KaTeX 公式和 PWA；运行时后端为 C++20，SQLite 直接嵌入进程，不需要 PostgreSQL、Node 服务或额外数据库进程。Node/pnpm 仅用于前端构建与自动化测试。

Alpha 后端已独立部署到 `https://m-notes-alpha.missazertia.com`，运行在 `cpa.missazertia.com` 的 127.0.0.1:8080，经 Nginx 提供 HTTPS。服务器位置、环境配置、更新及备份方式见 [部署说明](docs/DEPLOYMENT-ALPHA.md)。此地址提供后端 API 和 MCP，未发布 React 前端。

首次注册、MCP 客户端配置、各页面操作、只读子账户、JSON 导出及排错见 [完整使用教程](docs/USER-GUIDE.zh-CN.md)。当前前端入口为 [mnote-alpha.azertia.org](https://mnote-alpha.azertia.org)。

保留错题录入和编辑、解题思路与参考答案、SVG 配图、科目与考点、标签别名合并、关键词及本地相似度搜索、笔记目录、历史版本与对比、关联图谱、纸质本归档和 MCP 工具。

已移植上游的标题 LaTeX、手机笔记下拉选择器和重点染色。标题支持 `$a_n$`、`a_{n+1}`、`2^{n+1}` 等写法；笔记和错题正文支持 `==重点==`、`==red:易错==`、`==green:结论==`、`==blue:条件==` 及红、绿、蓝、黄别名，标记也可包住行内公式。归档打印使用相同的公式与染色渲染。此次参照上游 `bde23ec` 中的 `8c7bbba`、`8784ae1`、`b1253ee`、`a8bf0f3` 实现。

向量编码与分块前会去掉有效的染色标记，只保留正文，因此换颜色不会改变相似度、考点近似匹配或自动关联。原始 Markdown、历史版本、JSON 导出和打印快照仍保留标记。

新增自主注册与主账户数据隔离。在“设置”中，每个主账户可创建最多 5 个只读子账户；子账户用自己的用户名和密码登录，能阅读、搜索、打印已归档内容及导出所属主账户的数据，不能修改学习内容或管理其他账户。主账户可以删除子账户，删除后其已有登录立即失效。子账户可以修改自己的登录信息。

## 本机启动

需要 C++20 编译器、CMake、pkg-config、SQLite（FTS5 和 JSON1）、libsodium、nlohmann-json，以及用于前端构建的 Node 22.13+ 和 pnpm 11。

macOS 安装编译依赖：

```sh
brew install cmake pkg-config sqlite libsodium nlohmann-json
```

Ubuntu / Debian 安装编译依赖：

```sh
sudo apt-get install build-essential cmake pkg-config libsqlite3-dev libsodium-dev nlohmann-json3-dev
```

在本项目目录执行：

```sh
cp .env.example .env
./scripts/start.sh
```

打开 http://127.0.0.1:8080，自主注册主账户。新安装没有预置密码，也不复制第一个项目的数据库或账户。数据库首次启动自动建立，默认文件为 `data/mistakebook.db`。进程在后台使用 4 个 SQLite 连接和 8 个 HTTP 工作线程。

开发时可分开启动：

```sh
pnpm install
pnpm build:api
pnpm dev:api
# 另一个终端
pnpm dev:web
```

Vite 默认在 5174，代理同机 8080。构建后的前端由 C++ 服务直接提供。前端 `.env.production` 默认同源，未绑定旧项目的生产 API。

## 容器运行

```sh
docker compose up --build -d
```

这是单个应用容器，SQLite 仍然嵌入 C++ 进程，数据通过命名卷持久保存。默认只映射本机 8080。对外提供服务时用 HTTPS 反向代理，并将 `CORS_ORIGINS` 设置为实际前端来源。容器配方已提供；本次本机环境没有 Docker，验证的是原生构建。

## Cloudflare 前端自动部署

仓库根目录的 `wrangler.jsonc` 将 `packages/web/dist` 部署为 Workers 静态资源，并为 React Router 启用 SPA 回退。后端继续通过 `https://m-notes-alpha.missazertia.com` 提供 API。

在 Workers & Pages 中连接 GitHub 仓库 `AzertiaNovem/m-notes-new`，项目名称使用 `m-notes-new`，生产分支选择 `main`，根目录留空。构建命令填写 `pnpm build:web`，部署命令填写 `npx wrangler deploy`；启用预览时保留 `npx wrangler preview`。API 令牌可以选择“创建新令牌”，由 Cloudflare 为构建生成。

在构建变量中分别添加以下值：

```dotenv
NODE_VERSION=24.18.0
PNPM_VERSION=11.22.0
VITE_API_BASE_URL=https://m-notes-alpha.missazertia.com
```

这些值用于构建，不是 Worker 运行时绑定。API 地址不要追加 `/api/v1`；改值后需要重新构建。不要将构建命令改为 `pnpm build`，因为它还会编译 C++。连接 Git 后，推送到生产分支会触发前端更新。

部署完成后，将实际的 `https://…workers.dev` 或前端自定义域名追加到服务器 `/etc/mistakebook-alpha/environment` 的 `CORS_ORIGINS`，用英文逗号分隔，不加空格或末尾斜杠，再执行 `systemctl restart mistakebook-alpha`。保留现有的后端来源。当前 CORS 只支持精确 origin 匹配，每个预览来源需要单独放行；未经配置的预览页面能加载，但 API 请求会被拒绝。前端自定义域名应与当前后端域名不同。

如果使用 Pages，根目录、构建命令和构建变量相同，输出目录填写 `packages/web/dist`，不需要 Workers 部署命令；Pages 默认提供 SPA 回退。配置参考 [Workers Builds](https://developers.cloudflare.com/workers/ci-cd/builds/configuration/) 和 [Workers 构建环境](https://developers.cloudflare.com/workers/ci-cd/builds/build-image/)。

## 存储与性能

原项目已经使用 SQLite，因此新版没有把更换数据库名称当作性能提升。新实现使用 C++ 原生 SQLite 接口、WAL、连接池、每连接预编译 SQL 缓存，以及围绕账户和查询条件建立的复合索引。题目和笔记以独立 JSON 聚合记录保存，读取一条题目时不再逐表拼接解答、配图和标签。

题目列表在 SQL 中过滤和分页。科目、标签、父节点和历史记录建立专用索引。关键词检索使用 FTS5 trigram，短于三个字的关键词回退到同账户范围内的文本匹配。所有写入在事务中完成，题目、标签、历史记录和全文索引一起提交或回滚。子账户数量同时由事务和数据库触发器约束。

正文未变化的笔记移动和目录排序不会重建全文索引。本地向量使用有容量与内存预算限制的线程安全缓存，减少重复编码；可复现的冷、热查询测量见 [性能说明](docs/PERFORMANCE.md)。

本地相似度使用 256 维文本特征哈希，不调用生成模型或外部嵌入 API。它保留无 API Key 即可运行的体验，但不等同于大型语义嵌入模型；查询大题库时仍需要扫描账户内的候选内容。SQLite WAL 支持读写并发，但仍只有一个写入者，不能承诺无限并发或直接宣称比原项目快多少。

## JSON 导出

设置页“导出全部数据”会下载 UTF-8 JSON。REST 路径为 `GET /api/v1/export`，使用当前用户 Bearer token。

导出包含格式版本、UTC 导出时间、账户公开资料、只读子账户公开资料，以及 `data` 下的全部科目、标签和别名、题目与配图和解答、错题修改日志、笔记、完整笔记历史快照、关联、纸质本及打印归档页。JSON 保留原始 Markdown、LaTeX 和 SVG。

导出使用同一数据库读事务获取一致快照。不会包含密码哈希、会话令牌或其他主账户的内容。只读子账户导出同样以所属主账户的数据范围为限。此版本提供导出；没有添加用户未要求的 JSON 导入界面。

## 与原版的具体差异

打印由前端现有的 Markdown、KaTeX 与 SVG 渲染后调用浏览器打印，可另存为 PDF。服务保存纸质本、题目快照与逻辑页，检测哪些逻辑页内容发生变化，并支持选择归档页重新打印。一个逻辑页可能因题目长度跨多张物理纸，因此不声称保留原版固定双面纸的最小重排算法。

原版可配置外部 embedding 服务，当前新版尚未接入，使用本地文本相似度。C++ 与嵌入式 SQLite 并不限制调用外部模型 API。笔记和错题的自动关联、标签近似匹配由同一套本地特征计算。初始题库为空，各主账户注册时建立九个常用科目。

## 测试

```sh
pnpm build:api
ctest --test-dir backend/build --output-on-failure
pnpm test
pnpm test:web
pnpm --filter @mistakebook/web exec tsc --noEmit
pnpm build:web
```

C++ 测试覆盖内容、标签、搜索、笔记树、版本差异、关联与打印归档。HTTP 测试使用临时数据库与独立进程，验证自主注册、跨账户 ID 访问、只读 REST/MCP、并发创建子账户上限、导出一致性、令牌撤销、打印快照和输入校验。

标题与染色测试覆盖裸上下标、公式定界、代码与链接保护、颜色别名、无效和跨行标记、行内公式渲染，以及 Markdown 在笔记版本、导出和打印快照中的完整保存。

`tests/browser/oauth.mjs` 提供真实 Chromium 授权回归，覆盖本机与 HTTPS 回调、密码重试、取消授权、PKCE 兑换及 MCP 工具读取。它需要现有 Playwright 安装（可通过 `PLAYWRIGHT_MODULE` 指定）和 Chromium（可通过 `BROWSER_EXECUTABLE` 指定），不属于只依赖 Node 内置模块的后端接口测试。

备份运行中的 SQLite 请使用 SQLite backup API 或 `.backup`；不要只复制活动数据库主文件而忽略 WAL。服务停止后可完整复制数据目录。新项目不会自行部署到第一个项目的服务器。

## MCP 连接

远程工具端点为 `/mcp`，实现无状态 Streamable HTTP 的 JSON 响应。可用网页登录得到的 Bearer token 连接；同一账号的 REST 和 MCP 使用相同数据层与权限检查。只读账号的工具列表隐藏写工具，直接调用写工具也会被拒绝。

原 Node stdio MCP 入口没有复制到新版；客户端通过上述 HTTP 端点连接 C++ 服务。

同时提供 OAuth 授权服务器发现、动态公共客户端注册、授权码 + S256 PKCE、刷新令牌轮换及撤销。将 `.env` 中的 `PUBLIC_URL` 配置为实际服务的外部地址；本机默认 `http://localhost:8080`。OAuth token 仅用于绑定的 `/mcp` 资源，不能拿来管理网页账户。第三方客户端需要提供 resource 参数；回调 URI 必须与注册值一致，只允许 HTTPS 或 loopback HTTP。

授权页的 `form-action` 只放行本站和本次已注册回调的 origin，使 Chromium 能完成表单提交后的跨域跳转；单一的 `Referrer-Policy: same-origin` 保持提交 Origin 可校验，同时不向跨域回调泄露原页面地址。Cookie、一次性表单和 PKCE 校验仍然保留。

新版没有复制旧项目的远程服务器配置，也没有对真实第三方连接器进行上线测试。这里支持动态客户端注册，不支持把任意 HTTPS URL 当作 client_id 后由服务器抓取客户端元数据（CIMD）。需要该机制的客户端须改用已注册 client_id 或 Bearer 配置。

SQLite 与密码实现使用的库分别可见 [SQLite WAL 文档](https://www.sqlite.org/wal.html) 和 [libsodium 密码哈希文档](https://doc.libsodium.org/password_hashing/default_phf)。HTTP 实现使用固定版本 cpp-httplib，许可证保存在 `backend/vendor/httplib.LICENSE`。
