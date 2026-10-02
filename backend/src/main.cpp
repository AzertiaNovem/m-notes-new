#include "core.hpp"
#include "httplib.h"
#include "oauth.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sodium.h>
#include <sstream>
#include <thread>
namespace mb {
std::string env(const char *key, const std::string &fallback = "") {
  auto v = std::getenv(key);
  return v ? v : fallback;
}
void load_env() {
  std::ifstream f(".env");
  std::string line;
  while (std::getline(f, line)) {
    auto p = line.find('=');
    if (p == std::string::npos || line.empty() || line[0] == '#')
      continue;
    auto k = line.substr(0, p), v = line.substr(p + 1);
    if (v.size() > 1 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
      v = v.substr(1, v.size() - 2);
    if (!std::getenv(k.c_str()))
      setenv(k.c_str(), v.c_str(), 0);
  }
}
std::string client_ip(const httplib::Request &req) {
  static const auto trusted = [] {
    std::vector<std::string> ips;
    std::istringstream input(env("TRUSTED_PROXY_IPS"));
    std::string ip;
    while (std::getline(input, ip, ','))
      if (!ip.empty())
        ips.push_back(ip);
    return ips;
  }();
  if (std::find(trusted.begin(), trusted.end(), req.remote_addr) == trusted.end() ||
      req.get_header_value_count("X-Real-IP") != 1)
    return req.remote_addr;
  const auto forwarded = req.get_header_value("X-Real-IP");
  if (forwarded.empty() || forwarded.find_first_not_of("0123456789abcdefABCDEF:.") != std::string::npos)
    return req.remote_addr;
  // Only a configured immediate proxy may supply one IP. Normalize IPv6 spelling
  // so equivalent addresses cannot acquire independent rate-limit buckets.
  char normalized[INET6_ADDRSTRLEN];
  in_addr ipv4{};
  in6_addr ipv6{};
  if (inet_pton(AF_INET, forwarded.c_str(), &ipv4) == 1 &&
      inet_ntop(AF_INET, &ipv4, normalized, sizeof(normalized)))
    return normalized;
  if (inet_pton(AF_INET6, forwarded.c_str(), &ipv6) == 1 &&
      inet_ntop(AF_INET6, &ipv6, normalized, sizeof(normalized)))
    return normalized;
  return req.remote_addr;
}
class Pool {
  std::mutex mutex_;
  std::condition_variable ready_;
  std::vector<std::unique_ptr<Db>> free_;

public:
  Pool(const std::string &path, int count) {
    for (int i = 0; i < count; ++i)
      free_.push_back(std::make_unique<Db>(path));
  }
  struct Lease {
    Pool &pool;
    std::unique_ptr<Db> db;
    ~Lease() {
      std::lock_guard lock(pool.mutex_);
      pool.free_.push_back(std::move(db));
      pool.ready_.notify_one();
    }
  };
  Lease acquire() {
    std::unique_lock lock(mutex_);
    ready_.wait(lock, [&] { return !free_.empty(); });
    auto db = std::move(free_.back());
    free_.pop_back();
    return {*this, std::move(db)};
  }
};
class Transaction {
  Db &db;
  bool done = false;

public:
  Transaction(Db &d, bool write) : db(d) {
    db.exec(write ? "BEGIN IMMEDIATE" : "BEGIN");
  }
  ~Transaction() {
    if (!done)
      try {
        db.exec("ROLLBACK");
      } catch (...) {
      }
  }
  void commit() {
    db.exec("COMMIT");
    done = true;
  }
};
std::string hash_token(const std::string &t) {
  unsigned char h[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(h, reinterpret_cast<const unsigned char *>(t.data()), t.size());
  char out[65];
  sodium_bin2hex(out, sizeof(out), h, sizeof(h));
  return out;
}
std::string token() {
  unsigned char b[32];
  randombytes_buf(b, sizeof b);
  char s[65];
  sodium_bin2hex(s, sizeof s, b, sizeof b);
  return s;
}
std::string password_hash(const std::string &p) {
  if (p.size() < 8 || p.size() > 1024)
    throw Error(422, "密码须为 8–1024 字节");
  char out[crypto_pwhash_STRBYTES];
  if (crypto_pwhash_str(out, p.data(), p.size(), crypto_pwhash_OPSLIMIT_INTERACTIVE,
                        crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
    throw Error(503, "密码处理繁忙，请稍后重试");
  return out;
}
bool verify(const std::string &p, const std::string &hash) {
  return p.size() <= 1024 && crypto_pwhash_str_verify(hash.c_str(), p.data(), p.size()) == 0;
}
User user_from(const Json &r) {
  return {r.at("id").get<int64_t>(), r.at("owner_id").is_null() ? 0 : r.at("owner_id").get<int64_t>(),
          r.at("username"), r.at("role"), r.at("created_at")};
}
std::string username(const Json &b) {
  auto n = required(b, "username", 192);
  if (n.size() > 192)
    throw Error(422, "用户名过长");
  size_t chars = 0;
  for (unsigned char c : n) {
    if ((c & 0xc0) != 0x80)
      ++chars;
    if (c < 128 && !(std::isalnum(c) || c == '_' || c == '-' || c == '.'))
      throw Error(422, "用户名可用中文、字母、数字、点、下划线或连字符");
  }
  if (chars > 64)
    throw Error(422, "用户名最多 64 字");
  return n;
}
Json session(Db &db, const User &u) {
  auto t = token();
  constexpr int ttl = 86400 * 30;
  auto now = std::time(nullptr);
  db.query("DELETE FROM sessions WHERE expires_at<=?", {std::to_string(now)});
  db.query("INSERT INTO sessions(token_hash,user_id,expires_at) VALUES(?,?,?)",
           {hash_token(t), std::to_string(u.id), std::to_string(now + ttl)});
  return {{"user", u.json()}, {"token", t}, {"expires_in", ttl}};
}
User authenticate(Db &db, const httplib::Request &req) {
  auto h = req.get_header_value("Authorization");
  if (h.size() < 8 || h.substr(0, 7) != "Bearer ")
    throw Error(401, "请先登录");
  auto r = db.one(
      "SELECT u.* FROM sessions s JOIN users u ON u.id=s.user_id WHERE s.token_hash=? AND s.expires_at>?",
      {hash_token(h.substr(7)), std::to_string(std::time(nullptr))});
  if (r.is_null()) {
    if (req.path == "/mcp")
      if (auto u = oauth_authenticate(db, h.substr(7)))
        return *u;
    throw Error(401, "登录已失效，请重新登录");
  }
  return user_from(r);
}
void primary(const User &u) {
  if (u.readonly() || u.owner_id)
    throw Error(403, "仅主账户可管理子账户");
}
class RateLimit {
  std::mutex m;
  struct Window {
    int64_t at;
    int count;
  };
  std::map<std::string, Window> windows;

public:
  void check(const std::string &key, int limit) {
    std::lock_guard lock(m);
    auto ts = std::time(nullptr);
    for (auto it = windows.begin(); it != windows.end();)
      if (ts - it->second.at >= 60)
        it = windows.erase(it);
      else
        ++it;
    if (windows.size() > 10000)
      throw Error(429, "请求过于频繁");
    auto &w = windows[key];
    if (!w.at)
      w.at = ts;
    if (++w.count > limit)
      throw Error(429, "请求过于频繁，请一分钟后重试");
  }
};
std::optional<Json> auth_route(Db &db, const httplib::Request &req, const Json &b, RateLimit &rate) {
  auto path = req.path;
  auto method = req.method;
  if (method == "POST" && (path == "/api/v1/auth/register" || path == "/api/v1/auth/login")) {
    rate.check(client_ip(req) + path, path.ends_with("register") ? 10 : 30);
    auto n = username(b);
    auto p = required(b, "password", 1024);
    if (path.ends_with("register")) {
      if (b.contains("owner_id") || b.contains("role"))
        throw Error(422, "注册只能创建独立主账户");
      auto hash = password_hash(p);
      Transaction tx(db, true);
      db.query("INSERT INTO users(username,password_hash,role,created_at) VALUES(?,?,'user',?)",
               {n, hash, now()});
      auto u = user_from(db.one("SELECT * FROM users WHERE id=?", {std::to_string(db.last_id())}));
      Store s(db, u);
      for (const auto &subject : {"语文", "数学", "英语", "物理", "化学", "生物", "历史", "地理", "政治"})
        s.save("subject", {{"name", subject}});
      auto out = session(db, u);
      tx.commit();
      return out;
    }
    auto r = db.one("SELECT * FROM users WHERE username=?", {n});
    // Keep unknown-user verification comparable to real login attempts.
    static const std::string dummy = password_hash("dummy-password-not-an-account");
    bool ok = verify(p, r.is_null() ? dummy : r.at("password_hash").get<std::string>());
    if (!ok || r.is_null())
      throw Error(401, "用户名或密码不正确");
    Transaction tx(db, true);
    auto current = db.one("SELECT * FROM users WHERE id=?", {std::to_string(number(r, "id"))});
    if (current.is_null() || current.at("password_hash") != r.at("password_hash"))
      throw Error(401, "登录信息已变化，请重新登录");
    auto out = session(db, user_from(current));
    tx.commit();
    return out;
  }
  if (!path.starts_with("/api/v1/auth/") && !path.starts_with("/api/v1/subaccounts"))
    return std::nullopt;
  auto u = authenticate(db, req);
  if (path == "/api/v1/auth/me" && method == "GET")
    return Json{{"user", u.json()}};
  if (path == "/api/v1/auth/logout" && method == "POST") {
    db.query("DELETE FROM sessions WHERE token_hash=?",
             {hash_token(req.get_header_value("Authorization").substr(7))});
    return Json{{"ok", true}};
  }
  if ((path == "/api/v1/auth/password" || path == "/api/v1/auth/username") && method == "POST") {
    rate.check(std::to_string(u.id) + "credential", 10);
    auto r = db.one("SELECT password_hash FROM users WHERE id=?", {std::to_string(u.id)});
    if (r.is_null() || !verify(required(b, "current_password", 1024), r.at("password_hash")))
      throw Error(403, "当前密码不正确");
    if (path.ends_with("username")) {
      auto n = username(b);
      Transaction tx(db, true);
      auto current = db.one("SELECT password_hash FROM users WHERE id=?", {std::to_string(u.id)});
      if (current.is_null() || current.at("password_hash") != r.at("password_hash"))
        throw Error(409, "登录信息已变化，请重试");
      db.query("UPDATE users SET username=? WHERE id=?", {n, std::to_string(u.id)});
      u.username = n;
      tx.commit();
      return Json{{"user", u.json()}};
    }
    auto hash = password_hash(required(b, "new_password", 1024));
    Transaction tx(db, true);
    auto current = db.one("SELECT password_hash FROM users WHERE id=?", {std::to_string(u.id)});
    if (current.is_null() || current.at("password_hash") != r.at("password_hash"))
      throw Error(409, "登录信息已变化，请重试");
    db.query("UPDATE users SET password_hash=? WHERE id=?", {hash, std::to_string(u.id)});
    db.query("DELETE FROM sessions WHERE user_id=?", {std::to_string(u.id)});
    db.query("DELETE FROM oauth_codes WHERE user_id=?", {std::to_string(u.id)});
    db.query("DELETE FROM oauth_tokens WHERE user_id=?", {std::to_string(u.id)});
    auto out = session(db, u);
    out["ok"] = true;
    tx.commit();
    return out;
  }
  if (path == "/api/v1/subaccounts") {
    primary(u);
    if (method == "GET") {
      Json items = Json::array();
      for (auto &r : db.query("SELECT * FROM users WHERE owner_id=? ORDER BY id", {std::to_string(u.id)}))
        items.push_back(user_from(r).json());
      return Json{{"items", items}, {"limit", 5}};
    }
    if (method == "POST") {
      rate.check(std::to_string(u.id) + "subaccounts", 15);
      auto n = username(b);
      auto hash = password_hash(required(b, "password", 1024));
      Transaction tx(db, true);
      auto count = db.one("SELECT COUNT(*) AS n FROM users WHERE owner_id=?", {std::to_string(u.id)})
                       .at("n")
                       .get<int>();
      if (count >= 5)
        throw Error(409, "最多只能创建 5 个只读子账户");
      db.query(
          "INSERT INTO users(username,password_hash,role,owner_id,created_at) VALUES(?,?,'readonly',?,?)",
          {n, hash, std::to_string(u.id), now()});
      auto child = user_from(db.one("SELECT * FROM users WHERE id=?", {std::to_string(db.last_id())}));
      tx.commit();
      return child.json();
    }
  }
  if (path.starts_with("/api/v1/subaccounts/") && method == "DELETE") {
    primary(u);
    auto id = parse_id(path.substr(std::string("/api/v1/subaccounts/").size()));
    db.query("DELETE FROM users WHERE id=? AND owner_id=?", {std::to_string(id), std::to_string(u.id)});
    if (!db.changes())
      throw Error(404, "子账户不存在");
    return Json{{"ok", true}};
  }
  throw Error(404, "接口不存在");
}
Json export_data(Store &s) {
  Json data = Json::object();
  for (const auto &[kind, key] :
       std::vector<std::pair<std::string, std::string>>{{"subject", "subjects"},
                                                        {"tag", "tags"},
                                                        {"problem", "problems"},
                                                        {"problem_log", "problem_change_logs"},
                                                        {"note", "notes"},
                                                        {"note_log", "note_versions"},
                                                        {"link", "links"},
                                                        {"print_book", "print_books"},
                                                        {"print_page", "print_pages"}})
    data[key] = s.list(kind);
  Json accounts = Json::array();
  for (auto &r :
       s.db.query("SELECT id,username,role,owner_id,created_at FROM users WHERE owner_id=? ORDER BY id",
                  {std::to_string(s.user.tenant())}))
    accounts.push_back(r);
  auto owner = s.db.one("SELECT id,username,role,owner_id,created_at FROM users WHERE id=?",
                        {std::to_string(s.user.tenant())});
  return {{"format", "mistakebook"},
          {"version", 1},
          {"exported_at", now()},
          {"account", owner},
          {"exported_by", s.user.json()},
          {"subaccounts", accounts},
          {"data", data}};
}
struct ToolSpec {
  std::string name, description, method, path;
  Json properties;
  std::vector<std::string> required;
  bool write = false;
};
std::vector<ToolSpec> specs() {
  Json str = {{"type", "string"}}, num = {{"type", "integer"}};
  Json diagram = {
      {"type", "object"}, {"properties", {{"svg", str}, {"caption", str}}}, {"required", {"svg"}}};
  Json problemProps = {{"id", num},
                       {"title", str},
                       {"stem_md", str},
                       {"subject", str},
                       {"tags", {{"type", "array"}, {"items", str}}},
                       {"approach_md", str},
                       {"answer_md", str},
                       {"diagrams", {{"type", "array"}, {"items", diagram}}},
                       {"source", str},
                       {"mistake_note", str},
                       {"difficulty", str},
                       {"editor_tool", str},
                       {"change_summary", str}};
  return {
      {"list_taxonomy", "列出当前用户的学科与标签", "GET", "/api/v1/taxonomy", Json::object(), {}, false},
      {"list_problems",
       "分页列出错题",
       "GET",
       "/api/v1/problems",
       {{"subject", str}, {"tag", str}, {"limit", num}, {"offset", num}},
       {},
       false},
      {"get_problem", "查看错题完整内容", "GET", "/api/v1/problems/{id}", {{"id", num}}, {"id"}, false},
      {"list_problem_logs",
       "查看错题修改日志",
       "GET",
       "/api/v1/problems/{id}/logs",
       {{"id", num}},
       {"id"},
       false},
      {"search_mistakes",
       "检索错题和笔记",
       "POST",
       "/api/v1/search",
       {{"query", str},
        {"mode", {{"type", "string"}, {"enum", {"keyword", "hybrid", "rag"}}}},
        {"subject", str},
        {"target", str},
        {"limit", num}},
       {"query"},
       false},
      {"suggest_tags",
       "获取已有考点建议",
       "POST",
       "/api/v1/tags/suggest",
       {{"subject", str}, {"names", {{"type", "array"}, {"items", str}}}},
       {"subject", "names"},
       false},
      {"upsert_problem",
       "创建或更新错题；先查询分类，使用LaTeX Markdown，思路与答案分开，图仅SVG",
       "POST",
       "/api/v1/problems",
       problemProps,
       {"title", "stem_md", "subject", "approach_md", "answer_md", "editor_tool", "change_summary"},
       true},
      {"update_problem",
       "修改错题的指定字段，并记录变更大意",
       "PATCH",
       "/api/v1/problems/{id}",
       problemProps,
       {"id", "editor_tool", "change_summary"},
       true},
      {"delete_problem", "删除错题", "DELETE", "/api/v1/problems/{id}", {{"id", num}}, {"id"}, true},
      {"merge_tags",
       "合并当前用户的考点",
       "POST",
       "/api/v1/tags/merge",
       {{"source_tag_id", num}, {"target_tag_id", num}},
       {"source_tag_id", "target_tag_id"},
       true},
      {"list_note_toc", "查看笔记目录", "GET", "/api/v1/notes/toc", Json::object(), {}, false},
      {"get_note_node", "查看笔记", "GET", "/api/v1/notes/{id}", {{"id", num}}, {"id"}, false},
      {"upsert_note_node",
       "创建或更新笔记",
       "POST",
       "/api/v1/notes",
       {{"id", num},
        {"title", str},
        {"body_md", str},
        {"subject", str},
        {"parent_id", num},
        {"sort_order", num},
        {"editor_tool", str},
        {"change_summary", str}},
       {"title", "editor_tool", "change_summary"},
       true},
      {"delete_note_node", "删除笔记", "DELETE", "/api/v1/notes/{id}", {{"id", num}}, {"id"}, true},
      {"list_note_logs", "笔记修改日志", "GET", "/api/v1/notes/{id}/logs", {{"id", num}}, {"id"}, false},
      {"list_note_versions", "笔记版本", "GET", "/api/v1/notes/{id}/versions", {{"id", num}}, {"id"}, false},
      {"get_note_version",
       "查看历史版本",
       "GET",
       "/api/v1/notes/{id}/versions/{version_id}",
       {{"id", num}, {"version_id", num}},
       {"id", "version_id"},
       false},
      {"diff_note_versions",
       "比较笔记版本",
       "GET",
       "/api/v1/notes/{id}/diff",
       {{"id", num}, {"from", num}, {"to", str}},
       {"id", "from", "to"},
       false},
      {"get_note_graph", "查看知识图谱", "GET", "/api/v1/notes/graph", Json::object(), {}, false},
      {"link_notes",
       "建立笔记或题目的关联",
       "POST",
       "/api/v1/notes/links",
       {{"from", {{"type", "object"}}}, {"to", {{"type", "object"}}}, {"label", str}},
       {"from", "to"},
       true}};
}
Json dispatch(Store &s, const std::string &m, const std::string &p, const Json &b, const Query &q) {
  if (auto r = content_route(s, m, p, b, q))
    return *r;
  if (auto r = notes_route(s, m, p, b, q))
    return *r;
  if (auto r = print_route(s, m, p, b, q))
    return *r;
  throw Error(404, "接口不存在");
}
Json mcp(Store &s, const Json &b) {
  auto id = b.value("id", Json(nullptr));
  auto method = text(b, "method");
  Json result;
  if (method == "initialize")
    result = {{"protocolVersion", "2025-03-26"},
              {"capabilities", {{"tools", Json::object()}, {"prompts", Json::object()}}},
              {"serverInfo", {{"name", "mistakebook-native"}, {"version", "0.2.0"}}}};
  else if (method == "ping")
    result = Json::object();
  else if (method == "tools/list") {
    Json list = Json::array();
    for (auto &t : specs()) {
      if (s.user.readonly() && t.write)
        continue;
      list.push_back(
          {{"name", t.name},
           {"description", t.description},
           {"inputSchema", {{"type", "object"}, {"properties", t.properties}, {"required", t.required}}},
           {"annotations", {{"readOnlyHint", !t.write}, {"destructiveHint", t.method == "DELETE"}}}});
    }
    result = {{"tools", list}};
  } else if (method == "tools/call") {
    auto params = b.value("params", Json::object());
    auto name = required(params, "name", 100);
    auto args = params.value("arguments", Json::object());
    if (!args.is_object())
      throw Error(400, "arguments 必须为对象");
    bool found = false;
    for (auto &t : specs())
      if (t.name == name) {
        found = true;
        try {
          if (t.write)
            s.writable();
          auto path = t.path, verb = t.method;
          for (auto key : {"id", "version_id"}) {
            auto pos = path.find("{" + std::string(key) + "}");
            if (pos != std::string::npos)
              path.replace(pos, std::string(key).size() + 2, std::to_string(number(args, key)));
          }
          if ((name == "upsert_problem" || name == "upsert_note_node") && number(args, "id")) {
            verb = "PATCH";
            path += "/" + std::to_string(number(args, "id"));
          }
          Query q;
          for (auto it = args.begin(); it != args.end(); ++it)
            if (it.value().is_primitive() && !it.value().is_null())
              q[it.key()] = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
          auto out = dispatch(s, verb, path, args, q);
          result = {{"content", Json::array({{{"type", "text"}, {"text", out.dump()}}})}, {"isError", false}};
        } catch (const Error &e) {
          result = {{"content", Json::array({{{"type", "text"}, {"text", e.what()}}})}, {"isError", true}};
        }
        break;
      }
    if (!found)
      return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32601}, {"message", "Unknown tool"}}}};
  } else if (method == "prompts/list")
    result = {{"prompts", Json::array({{{"name", "upload_mistake"}, {"description", "将错题排版后录入"}}})}};
  else if (method == "prompts/get")
    result = {
        {"messages",
         Json::array({{{"role", "user"},
                       {"content",
                        {{"type", "text"},
                         {"text", "先在对话中OCR，禁止上传照片或图片URL。先list_"
                                  "taxonomy复用学科考点，再upsert_problem。题干使用LaTeX "
                                  "Markdown，图用手写SVG。approach_md只写方法分析，answer_"
                                  "md写完整计算与答案。每次提交填写editor_tool和change_summary。"}}}}})}};
  else
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32601}, {"message", "Method not found"}}}};
  return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}
} // namespace mb
int main() {
  using namespace mb;
  try {
    load_env();
    if (sodium_init() < 0)
      throw std::runtime_error("libsodium initialization failed");
    auto path = env("DATABASE_PATH", "data/mistakebook.db");
    if (path != ":memory:")
      std::filesystem::create_directories(std::filesystem::path(path).parent_path().empty()
                                              ? "."
                                              : std::filesystem::path(path).parent_path());
    {
      Db db(path);
      initialize(db);
      initialize_oauth(db);
    }
    Pool pool(path, 4);
    RateLimit rate;
    httplib::Server app;
    app.new_task_queue = []() { return new httplib::ThreadPool(8); };
    app.set_payload_max_length(8 * 1024 * 1024);
    app.set_read_timeout(10, 0);
    app.set_write_timeout(30, 0);
    app.set_keep_alive_max_count(100);
    auto origins =
        env("CORS_ORIGINS",
            "http://localhost:5174,http://127.0.0.1:5174,http://localhost:8080,http://127.0.0.1:8080");
    app.set_pre_routing_handler([&](const httplib::Request &req, httplib::Response &res) {
      res.set_header("X-Content-Type-Options", "nosniff");
      res.set_header("Referrer-Policy", "same-origin");
      if (req.path.starts_with("/api/") || req.path == "/mcp")
        res.set_header("Cache-Control", "no-store");
      auto origin = req.get_header_value("Origin");
      if (!origin.empty()) {
        std::istringstream ss(origins);
        std::string allowed;
        bool ok = false;
        while (std::getline(ss, allowed, ','))
          if (origin == allowed)
            ok = true;
        if (!ok) {
          res.status = 403;
          res.set_content("{\"detail\":\"来源未获允许\"}", "application/json");
          return httplib::Server::HandlerResponse::Handled;
        }
        res.set_header("Access-Control-Allow-Origin", origin);
        res.set_header("Vary", "Origin");
        res.set_header("Access-Control-Allow-Headers",
                       "Authorization, Content-Type, MCP-Protocol-Version, MCP-Session-Id");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        res.set_header("Access-Control-Expose-Headers", "Content-Disposition, WWW-Authenticate");
      }
      if (req.method == "OPTIONS") {
        res.status = 204;
        return httplib::Server::HandlerResponse::Handled;
      }
      return httplib::Server::HandlerResponse::Unhandled;
    });
    auto handle = [&](const httplib::Request &req, httplib::Response &res) {
      try {
        auto lease = pool.acquire();
        Db &db = *lease.db;
        Json out;
        if (req.path == "/authorize" || req.path == "/register" || req.path == "/token" ||
            req.path == "/revoke" || req.path.starts_with("/.well-known/")) {
          if (req.method == "POST" || req.path == "/authorize")
            rate.check(client_ip(req) + req.path, 30);
          if (oauth_route(db, req, res))
            return;
        }
        auto body = parse_body(req.body);
        if (req.path == "/health" || req.path == "/api/v1/health") {
          db.one("SELECT 1 AS ok");
          out = {{"ok", true},
                 {"backend", "C++20"},
                 {"storage", "SQLite WAL + FTS5"},
                 {"embedding_provider", "local-hash"},
                 {"embedding_dim", 256},
                 {"vec_enabled", false},
                 {"print_ready", true},
                 {"print_mode", "browser"}};
        } else if (auto auth = auth_route(db, req, body, rate))
          out = *auth;
        else {
          auto u = authenticate(db, req);
          Store store(db, u);
          Query query;
          for (auto &[k, v] : req.params)
            query[k] = v;
          bool write = req.method != "GET" && req.method != "HEAD";
          if (req.path == "/api/v1/search" || req.path == "/api/v1/tags/suggest" ||
              req.path.ends_with("/reprint"))
            write = false;
          if (req.path == "/mcp") {
            if (req.method != "POST")
              throw Error(405, "MCP 使用 POST JSON-RPC");
            auto method = text(body, "method");
            write = false;
            if (method == "tools/call") {
              auto name = text(body.value("params", Json::object()), "name");
              for (auto &t : specs())
                if (t.name == name)
                  write = t.write;
            }
            if (write)
              store.writable();
            Transaction tx(db, write);
            if (!body.contains("id") && method.starts_with("notifications/")) {
              res.status = 202;
              tx.commit();
              return;
            }
            out = mcp(store, body);
            if (out.contains("result") && out["result"].value("isError", false)) { /* rollback failed tool */
            } else
              tx.commit();
          } else {
            if (write)
              store.writable();
            Transaction tx(db, write);
            if (req.path == "/api/v1/export" && req.method == "GET") {
              out = export_data(store);
              res.set_header("Content-Disposition", "attachment; filename=\"mistakebook-export.json\"");
            } else
              out = dispatch(store, req.method, req.path, body, query);
            tx.commit();
          }
        }
        res.set_content(out.dump(), "application/json; charset=utf-8");
      } catch (const Error &e) {
        res.status = e.status;
        if (e.status == 401) {
          auto public_url = env("PUBLIC_URL", "http://localhost:8080");
          if (!public_url.empty() && public_url.back() == '/')
            public_url.pop_back();
          res.set_header("WWW-Authenticate", req.path == "/mcp"
                                                 ? "Bearer resource_metadata=\"" + public_url +
                                                       "/.well-known/oauth-protected-resource\""
                                                 : "Bearer realm=\"mistakebook\"");
        }
        res.set_content(Json{{"detail", e.what()}}.dump(), "application/json; charset=utf-8");
      } catch (const Json::exception &) {
        res.status = 422;
        res.set_content("{\"detail\":\"字段类型或结构不正确\"}", "application/json; charset=utf-8");
      } catch (const std::exception &e) {
        std::cerr << "request error: " << e.what() << '\n';
        res.status = 500;
        res.set_content("{\"detail\":\"服务器处理失败\"}", "application/json; charset=utf-8");
      }
    };
    app.Get("/health", handle);
    app.Get("/api/.*", handle);
    app.Post("/api/.*", handle);
    app.Patch("/api/.*", handle);
    app.Delete("/api/.*", handle);
    app.Post("/mcp", handle);
    app.Get("/mcp", handle);
    app.Get("/.well-known/.*", handle);
    app.Get("/authorize", handle);
    app.Post("/authorize", handle);
    app.Post("/register", handle);
    app.Post("/token", handle);
    app.Post("/revoke", handle);
    auto web = env("WEB_ROOT", "packages/web/dist");
    if (std::filesystem::exists(web)) {
      app.set_mount_point("/", web);
      app.Get("/.*", [web](const httplib::Request &, httplib::Response &res) {
        std::ifstream f(web + "/index.html");
        std::ostringstream ss;
        ss << f.rdbuf();
        res.set_content(ss.str(), "text/html; charset=utf-8");
      });
    }
    auto host = env("HOST", "127.0.0.1");
    auto port = std::stoi(env("PORT", "8080"));
    std::cout << "Mistakebook C++ listening on http://" << host << ":" << port << std::endl;
    if (!app.listen(host, port))
      throw std::runtime_error("Unable to bind HTTP port");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
  return 0;
}
