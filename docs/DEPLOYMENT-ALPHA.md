# Alpha 后端部署

目标服务器为 `root@cpa.missazertia.com`（45.77.71.103），新域名为 `https://m-notes-alpha.missazertia.com`。这是独立的 C++ 后端部署，不发布 React 前端，不复用原项目的数据库或账户。原 Node 服务继续监听 127.0.0.1:8000，新服务监听 127.0.0.1:8080。

新域名使用 DNS-only A 记录，TTL 300 秒，指向 45.77.71.103。Nginx 使用服务器已有的 `*.missazertia.com` 证书提供 HTTPS。80 端口由原有 Caddy 服务占用，本次不更改 Caddy；访问入口使用 HTTPS。

## 服务器位置

| 用途 | 位置 |
| --- | --- |
| systemd 服务 | `mistakebook-alpha.service` |
| 运行用户 | `mistakebook-alpha` |
| 当前发行版 | `/opt/mistakebook-alpha/current` |
| 初始发行版 | `/opt/mistakebook-alpha/releases/20261002-native-alpha` |
| 运行二进制 | `/opt/mistakebook-alpha/current/bin/mistakebook` |
| 环境配置 | `/etc/mistakebook-alpha/environment` |
| SQLite 数据 | `/var/lib/mistakebook-alpha/mistakebook.db` |
| Nginx 虚拟主机 | `/etc/nginx/sites-available/mistakebook-alpha.conf` |
| Nginx 访问日志 | `/var/log/nginx/mistakebook-alpha.access.log` |
| Nginx 错误日志 | `/var/log/nginx/mistakebook-alpha.error.log` |
| TLS 更新监听 | `mistakebook-alpha-certificate.path` |

数据库由服务首次启动建立；工作目录中的本机测试数据不会上传。数据目录权限为 0700，服务使用独立系统用户，只有数据目录可写。发行版目录及二进制由 root 拥有。

## 运行与检查

```sh
ssh root@cpa.missazertia.com 'systemctl status mistakebook-alpha --no-pager'
ssh root@cpa.missazertia.com 'journalctl -u mistakebook-alpha -n 50 --no-pager'
curl --fail https://m-notes-alpha.missazertia.com/health
curl --fail https://m-notes-alpha.missazertia.com/.well-known/oauth-authorization-server
```

MCP 地址为 `https://m-notes-alpha.missazertia.com/mcp`。未登录请求返回 401 是预期结果，响应包含 OAuth 发现地址。网页账户管理 API 位于 `/api/v1`。

`PUBLIC_URL` 必须保持为新域名。`CORS_ORIGINS` 当前允许新域名和本机 8080/5174 开发来源；如果前端发布到其他域名，应将实际前端 origin 精确追加到该变量，再重启本服务。

`TRUSTED_PROXY_IPS=127.0.0.1` 只信任本机 Nginx。Nginx 覆盖 `X-Real-IP`，后端验证和规范化后用该地址做限流；未启用信任时一律忽略客户端代理头。不要直接对公网暴露 8080，也不要把所有地址列为可信代理。

现有 acme.sh 负责续期通配符证书。新增 systemd path 单元只监听证书文件变化，在 `nginx -t` 通过后 reload Nginx，使续期后的证书生效；不修改已有证书或 acme.sh 凭据与配置。

## 更新与备份

每次更新使用新的 release 目录，在服务器原生编译（本机 macOS 二进制不能用于 Linux）：

```sh
cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build backend/build -j 1
ctest --test-dir backend/build --output-on-failure
node --test --test-concurrency=1 tests/*.mjs
```

服务器约 1 GiB 内存，构建使用单线程，测试使用串行套件以限制密码哈希的峰值内存。测试各自创建临时数据库，不写正式库。通过后将二进制安装到新发行版的 `bin/mistakebook`，切换 `current` 符号链接，重启 `mistakebook-alpha`，再检查本地及公网 health。保留旧发行版便于切回。

活库备份应使用 SQLite `.backup`，例如先建立只允许 root 访问的备份目录，再执行：

```sh
sqlite3 /var/lib/mistakebook-alpha/mistakebook.db '.backup /安全备份目录/mistakebook-alpha.db'
```

不要只拷贝正在运行中的主 DB 文件并忽略 WAL。修改 Nginx 后先执行 `nginx -t`，通过后再 reload。部署配置源文件保存在项目 `deploy/` 目录。
