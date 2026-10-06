# 错题本 C++ 嵌入式版使用教程

本教程面向当前新版错题本的使用者和部署维护者，覆盖首次注册、MCP 连接、错题录入与订正、搜索、考点、笔记版本、关联图谱、纸质本、只读子账户和数据导出。内容按 2026 年 10 月 6 日的代码与在线服务核对，包含标题公式、手机笔记选择器和重点染色；客户端菜单可能随版本调整。

当前网页入口是 [错题本前端](https://mnote-alpha.azertia.org)，后端是 `https://m-notes-alpha.missazertia.com`，MCP 地址是 `https://m-notes-alpha.missazertia.com/mcp`。网页操作在前端域名完成；MCP 和 API 使用后端域名。服务器目前只在后端域名提供 API、MCP 和 OAuth 授权页，直接打开后端首页不能代替前端。

新版使用独立账户与数据，不会自动继承原版账户或题库。搜索使用本地文本相似度，当前尚未接入外部 Embedding 模型。网页版能新建错题，但题干、答案的完整修改和笔记写入需要 MCP 或 API；下面分别给出对应入口。

## 阅读路线

第一次使用，可先完成“首次注册”，再选择一种 MCP 客户端配置方式，按“通过 MCP 录入与订正”保存一道示例题，最后在网页中查看、搜索和打印。只使用网页的用户可以跳过 MCP 配置，直接从“手动录入与题库”开始。

需要维护服务时，阅读最后的“部署与维护”。教程中的 JSON 是工具参数或接口请求体；只有标明 `jsonrpc` 的示例才是完整 MCP 请求。示例里的题目、笔记和版本 ID 必须换成你自己账户查询到的 ID，不要按示例数字猜测。

章节入口：

- [首次注册与登录](#首次注册与登录)
- [配置 MCP](#配置-mcp)
- [通过 MCP 录入与订正](#通过-mcp-录入与订正)
- [手动录入与题库](#手动录入与题库)
- [搜索与模型配置](#搜索与模型配置)
- [考点规范名与别名](#考点规范名与别名)
- [笔记目录与历史版本](#笔记目录与历史版本)
- [手工关联与图谱](#手工关联与图谱)
- [纸质本与 PDF](#纸质本与-pdf)
- [只读子账户与个人设置](#只读子账户与个人设置)
- [导出全部数据与备份](#导出全部数据与备份)
- [PWA 安装与日常使用](#pwa-安装与日常使用)
- [MCP 工具清单](#mcp-工具清单)
- [REST API 补充操作](#rest-api-补充操作)
- [部署与维护](#部署与维护)
- [常见问题](#常见问题)

## 首次注册与登录

打开前端，点击“注册账户”，输入用户名、密码和确认密码，再点击“注册并登录”。用户名最多 64 字，可使用中文、字母、数字、点、下划线和连字符，不使用空格。密码至少 8 位。

自主注册得到的是一个独立主账户。每个主账户的题库、笔记、考点、关联、纸质本独立保存，注册时预置语文、数学、英语、物理、化学、生物、历史、地理、政治九个学科。首次注册不会复制旧项目的数据，也没有通用的默认登录密码。

登录后，顶部依次显示“题库、笔记、图谱、搜索、考点、纸质本、录入、设置”。右上角显示当前用户名，并提供“退出”。只读子账户没有“录入”入口，页面里的写入按钮也会隐藏。

主账户之间的数据不共享。需要让另一人查看你的内容，应在“设置”创建只读子账户；让对方自主注册会生成一个空的独立错题本。

## 配置 MCP

### 连接地址与认证方式

MCP 让 AI 客户端读取或整理你账户里的错题和笔记。它连接已经运行的 C++ 后端，不需要在本机启动原项目的 Node MCP 程序。

```text
名称：mistakebook
类型：Streamable HTTP
地址：https://m-notes-alpha.missazertia.com/mcp
```

当前支持两种认证：支持动态客户端注册和 PKCE 的客户端可以通过浏览器 OAuth 授权；支持自定义请求头的客户端可以发送 `Authorization: Bearer <错题本登录令牌>`。这里的令牌属于错题本账户，和 OpenAI API Key、Cloudflare API 令牌、GitHub Token 都不同。

选择一种认证方式即可。OAuth 的访问令牌仅可访问 MCP，不能用来调用网页账户管理 API。网页登录令牌可以用于该账户的 MCP 和 REST API。

### 在 Codex 中通过 OAuth 连接

安装并可以运行 Codex CLI 后，在终端执行：

```sh
codex mcp add mistakebook --url https://m-notes-alpha.missazertia.com/mcp
codex mcp login mistakebook --oauth-client-registration dcr
codex mcp list
```

登录命令会启动浏览器授权流程。在错题本授权页输入已经注册的用户名与密码，核对客户端名称后批准连接，再回到 Codex。新建会话后，先让它列出学科和考点，验证连接是否可用。

`dcr` 表示动态客户端注册，本服务提供这种注册方式，当前不支持 CIMD。若较旧的 CLI 不认识该参数，先查看 `codex mcp login --help`；可以尝试普通的 `codex mcp login mistakebook`，或使用下一节的 Bearer 配置。若客户端已从环境变量或请求头取得 Bearer 令牌，应先移除该认证配置，再切换 OAuth。

Codex 的配置也可写入 `~/.codex/config.toml`：

```toml
[mcp_servers.mistakebook]
url = "https://m-notes-alpha.missazertia.com/mcp"
oauth_resource = "https://m-notes-alpha.missazertia.com/mcp"
```

CLI 添加和手动写配置是同一件事的两种做法，不要重复添加同名配置。客户端字段和注册选项依据 [OpenAI 官方 MCP 文档](https://learn.chatgpt.com/docs/extend/mcp?surface=cli)。

### 获取网页登录令牌

使用 Bearer 配置时，先在网页登录你准备使用的账户。在浏览器开发者工具的“应用程序 / Application”中找到当前前端域名的 Local Storage，复制键 `mistakebook_native_token` 的值。不同浏览器也可能把这一面板称为“存储”。这是当前网页会话的令牌。

如果习惯使用控制台，可以只在自己的错题本页面执行以下代码，将令牌复制到剪贴板：

```js
copy(localStorage.getItem("mistakebook_native_token"))
```

`copy()` 是部分浏览器开发者工具提供的辅助函数；若不支持，直接在 Local Storage 面板复制。不要把令牌发到聊天、截图或 Git 仓库。只读访问应登录只读子账户，再复制它自己的令牌。

已安装 Python 3 时，在 macOS 或 Linux 终端中可用下面的命令隐藏输入并设置环境变量；提示出现后粘贴令牌：

```sh
export MISTAKEBOOK_TOKEN="$(python3 -c 'import getpass; print(getpass.getpass("粘贴错题本登录令牌："))')"
```

环境变量只属于这个终端及其后续启动的进程。桌面客户端从 Dock 或开始菜单启动时，通常不会自动继承这个终端的变量，需要在客户端实际运行的环境中提供它。

登录令牌当前有效期为 30 天，没有长期 API Key 创建入口。若 MCP 复用网页登录令牌，从该网页会话退出会使这个令牌失效；修改密码会使该用户的旧网页登录和 OAuth 令牌失效，需要重新登录或授权。

### 在 Codex 中使用 Bearer

先取得令牌并设置 `MISTAKEBOOK_TOKEN`，再在 `~/.codex/config.toml` 添加：

```toml
[mcp_servers.mistakebook]
url = "https://m-notes-alpha.missazertia.com/mcp"
bearer_token_env_var = "MISTAKEBOOK_TOKEN"
```

从设置了环境变量的终端启动 `codex`，打开新会话检查 MCP。`bearer_token_env_var` 的值是环境变量名称，不是令牌本身。不要把 `${MISTAKEBOOK_TOKEN}` 写成配置里的令牌值；桌面或远程主机需要保证对应主机进程能读到该变量。Bearer 与 `oauth_resource` 字段依据 [OpenAI 官方配置参考](https://learn.chatgpt.com/docs/config-file/config-reference)。

### 在 Claude Code 中连接

使用 OAuth 时执行：

```sh
claude mcp add --transport http --scope user mistakebook https://m-notes-alpha.missazertia.com/mcp
```

进入 Claude Code 后运行 `/mcp`，选择这个服务并完成浏览器授权。`--scope user` 使连接在当前用户的不同项目中可用。

使用 Bearer 时，先设置 `MISTAKEBOOK_TOKEN`，再执行下面的替代命令。相同名称已经存在时，先查看或移除旧配置，不要把两种认证叠在一起：

```sh
claude mcp add --transport http --scope user mistakebook https://m-notes-alpha.missazertia.com/mcp \
  --header "Authorization: Bearer ${MISTAKEBOOK_TOKEN}"
claude mcp list
claude mcp get mistakebook
```

这个命令把当前令牌写入客户端的个人配置，之后令牌失效需要更新配置。HTTP 连接、认证和作用域的说明见 [Claude Code 官方 MCP 文档](https://code.claude.com/docs/en/mcp)。

### 在 Cursor 中连接

可在个人配置 `~/.cursor/mcp.json` 添加下列内容，再让 Cursor 重新加载 MCP。文件已有其他服务时，只合并 `mcpServers` 下的新条目。

```json
{
  "mcpServers": {
    "mistakebook": {
      "url": "https://m-notes-alpha.missazertia.com/mcp",
      "headers": {
        "Authorization": "Bearer ${env:MISTAKEBOOK_TOKEN}"
      }
    }
  }
}
```

Cursor 的环境变量语法是 `${env:变量名}`，需要保证 Cursor 进程能读取该变量。也可在自己的个人配置中将这段占位符替换为实际令牌，但不要提交含真实令牌的配置。连接字段依据 [Cursor 官方 MCP 文档](https://cursor.com/docs/mcp)。

### 在 ChatGPT 中连接

当前官方流程是在设置的“Security and login”中启用 Developer mode，然后在 Plugins 页面通过加号添加自定义 MCP，填写服务名称、说明和上面的 HTTPS MCP 地址，再创建连接。连接后安装个人插件，在支持插件的会话中选择它或通过 `@` 调用。账号或工作区策略可能影响入口是否可用。[官方连接说明](https://developers.openai.com/plugins/deploy/connect-chatgpt)

本服务需要账户认证，不能选无认证来访问私人数据。OAuth 接入需要客户端支持动态注册，或者能配置已注册的公共客户端 ID，并发送正确的 resource 参数。当前服务不支持 CIMD、固定客户端密钥认证，也没有完成真实 ChatGPT 连接器的端到端验证。若具体界面仅支持这些方式，应先使用前面可配 Bearer 的客户端，不要把“发现了服务”当作“已经能调用工具”。

### 验证连接

连接后，给 AI 一条只读请求：

```text
请使用 mistakebook 的 list_taxonomy 列出我的学科和考点，再使用
list_problems 查看最近的 5 道题。不要创建或修改内容。
```

能返回学科及当前账户的题目，才说明认证和工具调用可用。新账户题目列表为空是正常现象。工具名前可能带客户端生成的前缀，但服务发布的原始名称见本文末尾的工具清单。

还可以从已经设置令牌的终端发送一次 MCP 初始化请求：

```sh
curl --fail-with-body --silent --show-error \
  https://m-notes-alpha.missazertia.com/mcp \
  -H "Authorization: Bearer ${MISTAKEBOOK_TOKEN}" \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  --data-binary '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"manual-check","version":"1.0"}}}'
```

成功响应应包含 `serverInfo.name` 为 `mistakebook-native`。当前后端使用无状态 JSON 响应，不提供旧式 `/sse` 地址；在浏览器直接打开 `/mcp` 得到 401，或认证后 GET 得到 405，不能据此判断 MCP 失效，它的工具请求使用 POST。

## 通过 MCP 录入与订正

### 从题目照片整理为错题

把题目照片交给支持图片理解的 AI 客户端，让客户端在对话中识别并整理，再通过 MCP 写入 Markdown、LaTeX 和 SVG。后端不接收题目照片、扫描件或图片 URL 作为源图，也不负责 OCR。

可以直接使用这段请求：

```text
请把这道题整理到我的错题本。先识别图片并让我检查题干；
调用 list_taxonomy 复用现有学科和考点，必要时调用 suggest_tags。
题干使用 Markdown 和 LaTeX。图形用简洁的 SVG 重绘，不保存扫描件。
approach_md 写审题、方法选择与易错点，answer_md 写完整推导和最终答案。
检查数学内容后调用 upsert_problem，并填写 editor_tool 和 change_summary。
录入后告诉我返回的题目 ID 和规范化后的考点。
```

服务还发布了名为 `upload_mistake` 的 MCP prompt；支持 prompts 的客户端可以加载它。若客户端没有 prompt 入口，使用上面的自然语言指令即可。

### 新建题目的参数示例

调用 `upsert_problem` 时不传 `id` 表示新建：

```json
{
  "title": "二次函数的顶点与最小值",
  "subject": "数学",
  "tags": ["二次函数", "顶点坐标"],
  "stem_md": "已知函数 $y=x^2-4x+3$，求顶点坐标和最小值。",
  "approach_md": "先配方转为顶点式，再依据二次项系数为正判断最小值。注意顶点横坐标的符号。",
  "answer_md": "$$\ny=x^2-4x+3=(x-2)^2-1\n$$\n因此顶点为 $(2,-1)$，当 $x=2$ 时最小值为 $-1$。",
  "diagrams": [],
  "source": "课堂练习",
  "mistake_note": "曾把顶点横坐标写成 -2。",
  "editor_tool": "mcp-client",
  "change_summary": "首次录入二次函数顶点题"
}
```

学科必须已经存在。考点会按规范名、别名和本地相似度匹配，返回的 `tag_resolution` 可以帮助检查输入名称是否被并入已有考点。`editor_tool` 表示本次编辑来源，`change_summary` 表示变更大意，两者在错题和笔记写入中需要填写。

### 修改已有题目

先调用 `get_problem` 读取全文，再调用 `update_problem`，传入实际题目 ID、需要变化的字段及编辑记录。下例假设已查到 ID 为 42：

```json
{
  "id": 42,
  "mistake_note": "配方时应保持常数项等价，顶点横坐标为 2。",
  "editor_tool": "mcp-client",
  "change_summary": "补充配方过程的错因"
}
```

`update_problem` 支持题目标题、题干、学科、考点、思路、答案、配图、来源、错因等字段。未提交的字段保留原值。`upsert_problem` 传入已有 `id` 也能更新，但它的工具描述仍要求完整的新建字段，因此局部修改优先用 `update_problem`。

查看修改记录使用 `list_problem_logs`；这不是题目版本一键回滚功能。删除使用 `delete_problem`，删除后题目的修改日志和当前关联也会删除，已保存的纸质本快照另行管理。

## 手动录入与题库

### 网页录入

打开“录入”，填写标题、学科、考点、题干、解题思路、参考答案、来源、错因和变更大意。考点之间用中文或英文逗号分隔。SVG 和图注可不填；需要图示时，把完整 SVG 文本粘贴到 SVG 输入框。当前网页录入表单提供一个 SVG 输入框，MCP 的 `diagrams` 数组可提交多幅图。

“解题思路”用于说明如何审题、为什么选某种方法以及常见错误；“参考答案”用于记录完整计算或证明和最终答案。不要只把答案写在思路栏而留空参考答案。

点击“写入题库”后会进入该题详情。网页自动把编辑来源记录为 `web`。写入后刷新页面仍能看到内容，才说明保存完成；在输入框里编辑不会自动保存。

### Markdown 公式与 SVG

正文支持 Markdown 和 LaTeX。行内公式可写 `$x^2-4x+3$`，独立公式使用下面的写法：

```markdown
配方后得到：

$$
y=(x-2)^2-1
$$

因此顶点为 $(2,-1)$。
```

标题也可以写公式，如 `数列 $a_n$ 的递推关系`、`a_{n+1} 与 2^{n+1}`。题库、详情、搜索结果、笔记目录、历史版本标题和打印预览会渲染这些公式。标题仍使用原来的 `title` 字段，无需新增 `title_latex` 字段；标题中的独立公式会按行内布局显示，普通文字、代码和链接保留各自格式。

题干、解题思路、参考答案和笔记正文支持重点染色：

```markdown
已知 ==blue:$a_1=1$==，请关注 ==重点==。

易错处是 ==red:不要漏看下标==。

结论为 ==green:$a_{n+1}>a_n$==。
```

`==重点==` 默认黄底，也可显式使用 `yellow`、`red`、`green`、`blue`，或中文别名“黄、红、绿、蓝”，例如 `==红:易错==`。标记可以包住行内公式和 Markdown 强调，但应在同一行内闭合；代码块、行内代码和公式内部的标记不会被解释为染色。正确写法是 `==red:$a_n$==`，不要写在 `$...$` 里面。少量标记关键条件或结论即可。

MCP 和 API 保存的是原始标记文本，历史版本、JSON 导出和归档快照都会保留。打印预览使用相同颜色；浏览器或打印机仍可能受背景图形、彩色/黑白选项影响，需要以打印窗口预览为准。

SVG 示例可直接粘贴到图示栏：

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 240 140">
  <path d="M30 115 L200 115 L200 25 Z" fill="none" stroke="#222" stroke-width="2"/>
  <text x="20" y="132">A</text>
  <text x="202" y="132">B</text>
  <text x="205" y="24">C</text>
</svg>
```

这个 SVG 只是格式示例，不是上面二次函数题的配图。图应与实际题目对应；优先使用静态线条、文字和基本形状，外链资源和脚本不会作为题目配图保留。

### 浏览与订正

“题库”可按学科和考点筛选。点击题目卡片查看题干、图示、思路、答案、关联笔记和修改日志。当前网页没有题库翻页按钮；数据较多时可通过搜索定位，或通过 `list_problems` 的 `limit` 和 `offset` 分页查询。

主账户在题目详情的“订正”区可以修改考点和错因，填写变更大意并点击“保存”。当前该区不提供题干、答案等全部字段的编辑表单，完整修改使用 MCP 或 REST PATCH。

删除题目可从题库卡片或详情页执行，需要确认。没有回收站或一键恢复入口，重要内容应先导出。

## 搜索与模型配置

### 搜索模式

打开“搜索”，输入考点、题干或思路关键词，再选择模式和目标：

- “仅关键词”使用文本匹配与全文检索，适合明确术语、公式附近的词或考点名称。
- “本地相似度”比较本地文本特征，适合查找表述接近的题目或笔记。MCP 对应 `mode: "rag"`。
- “混合检索”融合关键词和本地相似度的排名，MCP 对应 `mode: "hybrid"`，也是网页默认模式。

目标可选“题+笔记”“仅题目”“仅笔记”。学科和考点是额外筛选条件，留空表示不筛选。笔记当前没有题目的考点标签字段，因此选择“仅笔记”时，应清空考点筛选。结果会显示命中片段，并提供查看题干或笔记正文的入口。

搜索结果可能来自同一道题的不同片段，不应把命中条数当作独立题目数。当前检索包括题目标题、题干、解题思路、参考答案、考点和笔记正文，不应把它当作对来源、错因等每个字段都能精确检索的承诺。

MCP 搜索参数示例：

```json
{
  "query": "二次函数 顶点 配方",
  "mode": "hybrid",
  "target": "all",
  "subject": "数学",
  "limit": 10
}
```

这组参数传给 `search_mistakes`。`target` 可取 `all`、`problems`、`notes`；`limit` 控制返回片段数。先去掉学科等筛选再换模式，有助于排查“没有命中”。

染色只用于显示。向量检索、考点近似匹配和自动关联在计算前会去掉有效标记与颜色名称，只保留正文；长段落先去标记再分块，避免标记被切断后混入向量。相同内容写成 `==red:正文==` 或 `==blue:正文==` 不会改变相似度。检索片段可以显示去标记后的正文，展开的原文、历史版本、导出和打印仍保留染色语法；代码或公式内部的字面标记不作为染色剥离。

### 当前可以配置什么模型

当前后端固定使用 `local-hash`，维度为 256，没有接入外部 Embedding、重排模型或后端生成模型。健康接口会报告 `embedding_provider: "local-hash"` 和 `embedding_dim: 256`。因此页面中的“向量”“embedding”或接口中的 `rag` 指本地相似度，不表示已在使用大型语义模型，也不代表搜索后会自动生成答案。

新版尚未读取原版的 `OPENAI_BASE_URL`、`OPENAI_API_KEY`、`EMBEDDING_MODEL`、`EMBEDDING_DIM`。把这些变量填入 Cloudflare 或服务器不会让外部模型生效。外部 Embedding 接入需要后端实现及索引管理，当前教程不能给出可用的外部模型配置步骤。

通过 MCP 对话时，识图、解释和生成答案的模型由 AI 客户端选择。客户端利用 MCP 保存或读取学习资料，错题本后端负责数据和权限。

管理员目前能调整两个本地相似度阈值：`TAG_MERGE_THRESHOLD` 默认 `0.92`，`NOTE_LINK_THRESHOLD` 默认 `0.80`。较高阈值会减少近似合并或自动关联；这属于服务器全局配置，不是每个账户的模型设置，更改时需要重启后端。已有手工关联不因调整阈值自动消失，也没有全量重建关联的网页按钮。

## 考点规范名与别名

打开“考点”查看规范名、别名和疑似重复。录入错题时，完全匹配的名称优先复用，再检查已有别名和本地近似匹配；未匹配到的考点会在相应学科下建立。

主账户可以在“手动合并”里选择“被合并（废弃名）”和“保留（规范名）”，确认后执行合并。例如把“抛物线顶点坐标”并入“顶点坐标”。相关题目的考点会更新，被合并名称成为保留考点的别名。合并方向决定最终保留哪个名字，应先核对；不同学科的考点不能合并。

“疑似重复”只表示相似度达到当前判断标准，仍需要检查实际含义。MCP 的 `suggest_tags` 可以查询输入名称与现有考点的建议，`merge_tags` 执行实际合并，均受当前账户范围限制。只读子账户可以查看规范名、别名和建议，不能执行合并。

## 笔记目录与历史版本

### 创建和修改笔记

“笔记”网页目前用于浏览目录、正文和历史版本，没有新建或编辑正文的表单。通过 MCP 调用 `upsert_note_node` 建立根笔记，参数例如：

```json
{
  "title": "二次函数复习",
  "body_md": "## 顶点式\n\n$y=a(x-h)^2+k$ 的顶点为 $(h,k)$。\n\n当 $a>0$ 时有最小值 $k$。",
  "subject": "数学",
  "sort_order": 0,
  "editor_tool": "mcp-client",
  "change_summary": "建立二次函数复习笔记"
}
```

新建根节点时不传 `parent_id`。要建立子笔记，先用 `list_note_toc` 查询父节点 ID，再在新建参数中传入这个 `parent_id`。`sort_order` 控制同级顺序，较小的值排在前面；省略时默认追加。

修改笔记时传入实际 `id`，保留工具要求的 `title`、`editor_tool`、`change_summary`，并提交需要更新的 `body_md`、`subject`、`parent_id` 或 `sort_order`。移动到根目录需要把 `parent_id` 设置为 `null`；当前 MCP 描述将该字段列为整数，若客户端因此拒绝 `null`，使用后面 API 的 `/move` 接口。

目录不能形成循环。删除有子笔记的父节点会返回冲突，需要先移动或删除子笔记。删除笔记同时删除其版本日志和相关连边，没有独立回收站。

### 查看和比较版本

打开网页“笔记”，桌面端在左侧目录选择笔记。手机端点击顶部的笔记选择器，展开带层级缩进的目录，选择后菜单关闭；当前笔记会显示在按钮中。菜单可以滚动，点击外部或按 Esc 可关闭，也支持方向键选择。

正文下方的“版本”列出编辑来源、时间和变更大意。点击“查看”读取该次保存的快照；“对比上一版”展示相邻版本变化；最早版本对应“对比空版”；“与当前对比”展示该版本到当前正文的差异。点“返回当前”退出历史视图。

版本对比可以展示正文行、标题、父节点和排序变化。查看旧版或比较版本不会改变当前笔记。当前没有“恢复此版本”按钮；需要恢复内容时，先读取历史快照，再通过 MCP 将所需标题和正文写入当前笔记，并记录恢复说明，这会形成一次新的编辑记录。

相关 MCP 工具为 `list_note_logs`、`list_note_versions`、`get_note_version`、`diff_note_versions`。工具参数里的 `version_id` 或 `from` 是查询返回的版本记录 ID，不是版本列表的第几行。对比当前版本时，`to` 使用字符串 `"current"`；对比另一个历史版本时，按工具 schema 将该版本 ID 写成字符串。

## 手工关联与图谱

题目和笔记在写入时会计算本地相似度，符合阈值的内容可以形成自动关联。自动连边可能随相关内容更新而重算，具体结果以详情页与图谱为准。

要明确建立关系，先读取两端的实际 ID，再调用 `link_notes`。下例仅展示参数形状，42 和 57 均需替换：

```json
{
  "from": {"kind": "note", "id": 57},
  "to": {"kind": "problem", "id": 42},
  "label": "配方求顶点的应用题"
}
```

两端类型可以是 `note` 或 `problem`，不能关联到自身。手工关联不依赖相似度达到阈值。网页详情会显示关联入口；“图谱”中实线表示手工边，虚线表示自动相似度边。点击节点可进入对应笔记或题目；“显示关联题目”控制图中是否包含有关联的题目，并非显示整个题库所有题目。

当前网页不提供添加或删除连边的编辑表单。MCP 提供添加手工关联工具；删除关联可通过 REST `DELETE /api/v1/notes/links`，传入同样的 `from` 和 `to`，删除自动边时还需指定 `via: "embedding"`。内容再次更新时，满足阈值的自动边可能重新生成。

## 纸质本与 PDF

### 创建和首次归档

主账户打开“纸质本”，填写名称并勾选学科或考点，点击“创建纸质本”。名称可留空；学科不选表示全部学科，考点不选表示全部考点。勾选多个考点时，题目包含其中任一考点即可进入选定范围，学科条件仍需满足。

纸质本创建后，点击“首次生成并预览”。这一步将当前符合条件的题目保存为归档快照，打开打印预览；仅创建纸质本还没有可打印的归档内容。预览页面点击“打印 / 另存为 PDF”，在浏览器或系统打印窗口选择 A4，再选择打印机或 PDF 保存。

归档保存的是生成时的题目内容。之后修改题库不会立即改掉旧归档；要使用新内容，需要按下一节更新。

### 更新归档与补印

回到纸质本详情，系统会比较当前题库与归档，提示涉及的逻辑页、移动的题目 ID，或在归档表中显示“已变”。主账户点击“更新归档并预览”保存新快照并预览需要更新的页。如果显示“归档与当前题库一致”，无需重新生成。

只想再次打印已有归档时，可点“预览全部归档页”，或者勾选“归档逻辑页”后点“预览选中页”。表格中的“分组”按钮可一次选中同组逻辑页。选中页预览读取的是保存的归档内容，不会自动刷新为最新题目。

这里的页码是系统的逻辑页码，不是打印机实际纸页数。当前每题形成逻辑内容单元，可能插入空白逻辑页以配对；长题可能跨多张物理纸。浏览器缩放、字体、边距和打印设置会改变实际分页。不要据此承诺固定双面纸排版或最小补印张数，应以打印预览核对页数。

只读子账户可以预览、打印和另存已有归档，但不能新建纸质本、生成或更新归档、删除纸质本。删除纸质本会删除该本归档快照，不会删除题库中的原题。当前 MCP 工具清单没有纸质本管理或服务器 PDF 生成工具，这些操作使用网页或 REST API。

## 只读子账户与个人设置

### 创建和删除子账户

主账户进入“设置”的“只读子账户”区域，填写子账户用户名和初始密码，点击“创建只读子账户”。最多 5 个，名称在系统内需要可用，初始密码至少 8 位。对方使用自己的子账户凭据在相同前端登录，也可用该账户连接 MCP。

子账户可以浏览、搜索、查看修改与版本记录、查看考点和图谱、打印已归档内容、导出所属主账户的全部学习数据，并修改自己的用户名或密码。它不能新增、修改或删除学习内容，不能管理其他账户，也不能创建新的只读子账户。只读包括“可以导出全部内容”，不表示仅能看单篇资料。

达到 5 个后，需要删除一个才可继续创建。删除子账户会立即取消其访问权限和已有登录，其所属主账户的学习资料仍保留。当前没有子账户密码由主账户重置的页面，也没有独立禁用开关；不要把自主注册当作创建子账户。

### 修改用户名和密码

在“设置”填写新用户名和当前密码，再点击“保存用户名”；修改的是当前登录账户。修改密码填写当前密码和新密码，点击“保存密码”，页面会取得新的登录令牌。

修改密码后，该用户以前的网页登录令牌与 OAuth 授权失效。其他客户端需要重新登录或授权。当前没有邮箱绑定、忘记密码邮件、双重验证或管理员恢复入口，不能按通用网站的找回密码流程操作。

## 导出全部数据与备份

在“设置”点击“导出全部数据（JSON）”，浏览器会下载 UTF-8 JSON，文件名类似 `错题本-全部数据-2026-10-05.json`。

文件包含格式版本、导出时间、主账户和导出者的公开资料、只读子账户公开资料，以及 `data` 内的学科、考点及别名、题目、题目修改日志、笔记、笔记版本快照、关联、纸质本和归档页。题目保留 Markdown、LaTeX、SVG、思路和答案。导出不包含密码哈希、登录令牌、OAuth 凭据或其他主账户的数据。

只读子账户也可以导出所属主账户的全部学习资料。下载后可用文本编辑器或 JSON 工具查看，妥善保存其中的学习内容和账户公开资料。

当前提供 JSON 导出，没有 JSON 导入或网页一键恢复。它适合离线保存和迁移准备；需要完整恢复账户及服务时，管理员应另备份 SQLite 数据库。

从终端导出时，用网页登录令牌执行：

```sh
curl --fail-with-body --silent --show-error \
  https://m-notes-alpha.missazertia.com/api/v1/export \
  -H "Authorization: Bearer ${MISTAKEBOOK_TOKEN}" \
  -o mistakebook-export.json
python3 -m json.tool mistakebook-export.json >/dev/null
```

第二条命令只检查 JSON 是否可解析，不会导入数据。OAuth MCP 令牌不能替代这里的网页登录令牌。

## PWA 安装与日常使用

支持 PWA 的浏览器可把前端安装到桌面或主屏幕。使用浏览器的“安装应用”或“添加到主屏幕”入口，安装后仍使用同一服务和账户。

PWA 可以缓存前端资源，当前 API 请求仍需要网络。不能把安装应用理解为整个题库的离线副本；离线保存资料使用 JSON 导出或 PDF。前端更新后，若安装版仍显示旧界面，可重新联网打开并刷新，必要时清理该站点的前端缓存后重新登录。

日常流程可以是：录入错题，补写错因，整理成一篇笔记，建立手工关联，搜索同类题目复习，最后按学科生成纸质本。AI 在对话里解释内容后，如未调用写入工具，解释不会自动保存到错题本。

## MCP 工具清单

当前主账户发布 20 个工具，其中 13 个为只读，7 个写入内容。只读子账户的工具列表隐藏写入工具，直接尝试写入也会被后端拒绝。表中的名称是服务端原始工具名。

| 工具 | 用途 | 只读子账户 |
| --- | --- | --- |
| `list_taxonomy` | 查询学科、考点规范名和别名 | 可用 |
| `list_problems` | 按学科、考点分页列题；支持 `limit`、`offset` | 可用 |
| `get_problem` | 读取题目完整内容与关联 | 可用 |
| `list_problem_logs` | 查看题目修改日志 | 可用 |
| `search_mistakes` | 搜索题目与笔记 | 可用 |
| `suggest_tags` | 查询已有考点建议 | 可用 |
| `upsert_problem` | 新建或完整更新错题 | 不可用 |
| `update_problem` | 局部更新错题并记录变更 | 不可用 |
| `delete_problem` | 删除错题 | 不可用 |
| `merge_tags` | 把一个考点并入另一个规范考点 | 不可用 |
| `list_note_toc` | 查询树状笔记目录 | 可用 |
| `get_note_node` | 读取笔记正文与关联 | 可用 |
| `upsert_note_node` | 新建、修改或调整笔记目录位置 | 不可用 |
| `delete_note_node` | 删除不含子节点的笔记 | 不可用 |
| `list_note_logs` | 查看笔记修改记录 | 可用 |
| `list_note_versions` | 列出笔记版本和快照信息 | 可用 |
| `get_note_version` | 读取指定历史版本 | 可用 |
| `diff_note_versions` | 比较两个版本或历史版与当前版 | 可用 |
| `get_note_graph` | 查询笔记及关联题目的图谱 | 可用 |
| `link_notes` | 建立笔记或题目之间的手工关联 | 不可用 |

MCP 还发布 `upload_mistake` prompt。当前没有发布账号管理、JSON 导出、学科新增、纸质本管理、删除关联、外部模型配置等 MCP 工具；这些功能分别使用已有网页或下节说明的 REST API，不能仅凭名称让客户端调用不存在的工具。笔记创建与修改使用 `upsert_note_node`。

## REST API 补充操作

所有私人接口使用网页登录令牌，格式为 `Authorization: Bearer …`。下面假定已设置 `MISTAKEBOOK_TOKEN`，命令用 HTTPS 后端域名。REST API 与 MCP 具有同样的账户隔离和只读限制。

### 添加自定义学科

当前网页设置没有学科编辑表单，MCP 也没有新增学科工具。主账户可通过 API 添加学科，再刷新网页：

```sh
curl --fail-with-body --silent --show-error \
  https://m-notes-alpha.missazertia.com/api/v1/subjects \
  -H "Authorization: Bearer ${MISTAKEBOOK_TOKEN}" \
  -H 'Content-Type: application/json' \
  --data-binary '{"name":"计算机"}'
```

删除学科使用 `DELETE /api/v1/subjects/<经过 URL 编码的名称>`，仅在该学科没有题目、考点或笔记时可删除。当前错误提示可能写“在设置中创建科目”，实际创建入口是此接口。

### 移动笔记或重排目录

移动某篇笔记使用 `POST /api/v1/notes/<笔记ID>/move`。请求体如下，`null` 表示移动到根目录：

```json
{
  "parent_id": null,
  "sort_order": 0,
  "editor_tool": "api",
  "change_summary": "将笔记移到根目录"
}
```

完整重排使用 `POST /api/v1/notes/toc`，请求体传 `nodes` 树、`editor_tool` 和 `change_summary`。每个节点以 `id` 和 `children` 表示，需要包含账户的全部笔记且每篇恰好一次，不是只提交待移动的一篇。先 GET 同一路径取得当前目录，再构造完整的新目录。

### 常用接口位置

题目使用 `/api/v1/problems` 和 `/api/v1/problems/<id>`，搜索使用 `POST /api/v1/search`，笔记使用 `/api/v1/notes` 和 `/api/v1/notes/<id>`，关联使用 `/api/v1/notes/links`。题目、笔记新增为 POST，局部修改为 PATCH，删除为 DELETE。

纸质本使用 `/api/v1/print/books`。创建为 POST，详情和计划分别为 GET `/api/v1/print/books/<id>` 与 `…/<id>/plan`，归档更新为 POST `…/<id>/apply`，重印选择为 POST `…/<id>/reprint`，归档文档为 GET `…/<id>/document`。`document` 返回 JSON 内容，PDF 由前端浏览器打印产生。

账户接口使用 `/api/v1/auth/`，子账户使用 `/api/v1/subaccounts`。`/register` 是 OAuth 客户端注册接口，不能用它注册网页账户；网页注册接口是 `/api/v1/auth/register`。

## 部署与维护

### Cloudflare 前端自动部署

在 Workers & Pages 中连接 [GitHub 仓库](https://github.com/AzertiaNovem/m-notes-new)，Worker 名称使用 `m-notes-new`，生产分支选择 `main`，根目录留空。构建命令为 `pnpm build:web`，部署命令为 `npx wrangler deploy`，启用预览时保留 `npx wrangler preview`。仓库的 `wrangler.jsonc` 已配置静态资源目录和 SPA 回退。

构建变量分别添加：

```dotenv
NODE_VERSION=24.18.0
PNPM_VERSION=11.22.0
VITE_API_BASE_URL=https://m-notes-alpha.missazertia.com
```

API 地址不要附加 `/api/v1`。这些是构建变量，修改后需要重新构建；Worker 的运行时变量不能直接改掉已经构建好的前端地址。根目录的 `pnpm build` 还会编译 C++，Cloudflare 构建前端应使用 `pnpm build:web`。

若使用 Pages，保持仓库根目录与相同构建命令，输出目录填 `packages/web/dist`。Workers 与 Pages 的创建流程选择一种即可。Git 集成会在相应分支更新时触发前端构建；这条前端部署流程不会自动替换服务器的 C++ 二进制。[Workers Builds 官方说明](https://developers.cloudflare.com/workers/ci-cd/builds/configuration/)

### 前端域名与 CORS

部署到新的域名时，修改服务器 `/etc/mistakebook-alpha/environment`，将实际前端 origin 追加到 `CORS_ORIGINS`，用英文逗号分隔，不加空格或末尾斜杠，保留现有来源。随后重启：

```sh
ssh root@cpa.missazertia.com
systemctl restart mistakebook-alpha
```

当前服务器已允许 `https://mnote-alpha.azertia.org`。Pages、Workers 的默认域名、预览域名、自定义域名都是不同来源，不会自动互相放行；当前没有通配符来源匹配。前端绑定域名应保留后端 `m-notes-alpha.missazertia.com` 的用途，MCP 客户端继续使用后端地址。

### 本机运行

准备 Node 22.13 或更高版本、pnpm 11，以及 C++20、CMake、pkg-config、SQLite FTS5 和 JSON1、libsodium、nlohmann-json。macOS 编译依赖可执行：

```sh
brew install cmake pkg-config sqlite libsodium nlohmann-json
```

Ubuntu / Debian 使用：

```sh
sudo apt-get install build-essential cmake pkg-config libsqlite3-dev libsodium-dev nlohmann-json3-dev
```

在仓库根目录首次运行：

```sh
test -f .env || cp .env.example .env
./scripts/start.sh
```

已有 `.env` 时不要覆盖自己的配置。脚本会构建 C++ 与前端并启动服务，默认网页为 `http://127.0.0.1:8080`，MCP 为 `http://127.0.0.1:8080/mcp`，数据文件为 `data/mistakebook.db`。本地客户端连接本机地址；云端客户端无法直接访问你电脑的 loopback 地址。

开发时也可运行 `pnpm dev:api` 与 `pnpm dev:web`，但需要先安装依赖和执行 `pnpm build:api`。Vite 开发前端默认端口为 5174，通过代理连接本机 8080。

仓库提供 `docker compose up --build -d` 的容器配方，使用命名卷保存 SQLite；本次教程核对的是原生服务，未把容器配方视为已经实际部署验证。

### 检查服务与备份数据库

线上检查可使用：

```sh
curl --fail https://m-notes-alpha.missazertia.com/health
ssh root@cpa.missazertia.com 'systemctl status mistakebook-alpha --no-pager'
ssh root@cpa.missazertia.com 'journalctl -u mistakebook-alpha -n 50 --no-pager'
```

后端运行于 `127.0.0.1:8080`，Nginx 提供 HTTPS，服务名为 `mistakebook-alpha`。数据库位于 `/var/lib/mistakebook-alpha/mistakebook.db`，发行版位于 `/opt/mistakebook-alpha/current`。更新与回滚操作见 [Alpha 部署说明](DEPLOYMENT-ALPHA.md)，不要覆盖同机旧项目及其数据库。

完整备份运行中的数据库使用 SQLite `.backup`，例如在服务器以 root 执行：

```sh
install -d -m 700 /var/backups/mistakebook-alpha
sqlite3 /var/lib/mistakebook-alpha/mistakebook.db '.backup /var/backups/mistakebook-alpha/mistakebook-backup.db'
chmod 600 /var/backups/mistakebook-alpha/mistakebook-backup.db
```

该示例目标是固定文件，后续执行会覆盖同名备份；需要多份备份时先选择新的文件名。备份包含全部用户和认证数据，应限制访问。运行中的数据库采用 WAL，不要只复制主 DB 文件而忽略 WAL。

## 常见问题

### 页面打开但登录提示网络错误或 403

先检查前端构建时的 `VITE_API_BASE_URL`，再检查服务器 `CORS_ORIGINS` 是否包含浏览器实际地址的完整 origin。修改构建变量后需要重新部署，修改服务器环境后需要重启后端。浏览器 Origin 是前端来源，不是后端 API 域名。

### MCP 返回 401 或突然无法访问

检查令牌是否属于本账户、是否已经失效、是否仍在客户端进程的环境中。复用网页令牌后退出网页，或修改密码、删除子账户，都会影响相应凭据。OAuth 客户端应重新授权；不要拿 OAuth MCP 令牌调用 REST 账户接口。

### MCP 只出现读取工具

通常是连接了只读子账户，或客户端自行限制了工具。主账户当前发布 20 个工具，只读子账户为 13 个。也应检查客户端已刷新工具列表，而不是只看旧会话的缓存。

### OAuth 提示 client_id 或 resource 不正确

本服务只接受已注册的公共客户端 ID、S256 PKCE 和正确的 MCP resource。resource 应为 `https://m-notes-alpha.missazertia.com/mcp`。当前不支持把任意 HTTPS URL 当作 CIMD client_id，也不使用 client_secret。Codex 可选择 DCR 注册；若其他客户端无法配合，使用支持 Bearer 请求头的方式。

### OAuth 授权后出现表单过期或浏览器校验失败

旧版授权页曾将 `form-action` 限制为本站，Chromium 会因此拦截登录成功后的外部回调；授权表单已被消费，再次提交便显示 `Authorization form expired or browser verification failed`。2026 年 10 月 6 日已修正回调放行及重复 Referrer-Policy 问题，并用真实浏览器验证本机和 HTTPS 回调。

更新后应关闭旧授权页面，从客户端重新发起 OAuth，不要重新提交旧页面。该错误仍可能表示表单超过十分钟、已使用，或浏览器没有带回对应 Cookie，安全校验不会被跳过。Bearer 模式直接填写 `Bearer 实际令牌`；`${变量名}` 是环境变量引用，不能把令牌本身放在花括号里。

### 搜索结果不好或没有结果

先清除学科和考点限制，使用明确词语选择“仅关键词”，再尝试混合检索。仅笔记搜索不能携带考点筛选。当前本地特征不是大型语义 Embedding，长篇抽象提问、复杂同义改写或跨语言相似度不保证准确；增加实际出现在资料中的词语更容易定位。

### 网页找不到笔记编辑或题目全文编辑

这是当前界面的功能范围。笔记通过 `upsert_note_node` 编辑，题干和答案等通过 `update_problem` 修改。笔记网页用于阅读与版本对比；题目网页的“订正”区只编辑考点和错因。

### 纸质本仍显示旧答案或无法打印

预览读取归档快照，先由主账户更新归档。没有归档页时需要“首次生成并预览”；只读子账户不能代替主账户生成。PDF 在浏览器系统打印窗口保存，后端没有生成 PDF 的任务队列。

### 子账户建立失败或删除笔记提示冲突

子账户可能达到 5 个上限、用户名已经存在或密码不符合要求。父笔记仍有子节点时不能直接删除，应先移动子笔记。不要通过反复注册主账户绕过子账户管理，因为新主账户不会共享原有数据。

### 导出文件能否直接恢复

目前不能在网页中重新导入。JSON 可以离线阅读或用于后续迁移，完整服务恢复使用管理员保存的 SQLite 备份。下载文件后先确认能够解析，并检查账户与数据数量是否符合预期。
