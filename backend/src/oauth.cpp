#include "oauth.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <set>
#include <sodium.h>
#include <sstream>

namespace mb {
std::string env(const char *, const std::string &);
std::string token();
std::string hash_token(const std::string &);
std::string password_hash(const std::string &);
bool verify(const std::string &, const std::string &);
User user_from(const Json &);
namespace {
struct OAuthError : Error {
  std::string code;
  OAuthError(std::string c, std::string message, int status = 400)
      : Error(status, message), code(std::move(c)) {}
};
class OAuthTx {
  Db &db;
  bool done = false;

public:
  explicit OAuthTx(Db &d) : db(d) {
    db.exec("BEGIN IMMEDIATE");
  }
  ~OAuthTx() {
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
std::string lower_ascii(std::string s) {
  for (auto &c : s)
    if (static_cast<unsigned char>(c) < 128)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
int64_t timestamp() {
  return static_cast<int64_t>(std::time(nullptr));
}
std::string base_url() {
  auto value = env("PUBLIC_URL", "http://localhost:8080");
  while (!value.empty() && value.back() == '/')
    value.pop_back();
  return value;
}
std::string resource_url() {
  return base_url() + "/mcp";
}
std::string url_origin(const std::string &value) {
  auto scheme = value.find("://");
  if (scheme == std::string::npos)
    return {};
  auto end = value.find_first_of("/?#", scheme + 3);
  return lower_ascii(end == std::string::npos ? value : value.substr(0, end));
}
std::string escape_html(const std::string &value) {
  std::string out;
  for (char c : value)
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    case '\'':
      out += "&#39;";
      break;
    default:
      out += c;
    }
  return out;
}
std::string encode_url(const std::string &value) {
  constexpr char digits[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      out += static_cast<char>(c);
    else {
      out += '%';
      out += digits[c >> 4];
      out += digits[c & 15];
    }
  }
  return out;
}
std::string get(const httplib::Request &req, const std::string &key, bool needed = false,
                size_t maximum = 4096) {
  if (req.params.count(key) > 1)
    throw OAuthError("invalid_request", "Duplicate parameter: " + key);
  auto value = req.get_param_value(key);
  if (value.size() > maximum || (needed && value.empty()))
    throw OAuthError("invalid_request", "Missing or invalid parameter: " + key);
  return value;
}
bool random_token(const std::string &value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c); });
}
std::string browser_cookie(const httplib::Request &req) {
  std::istringstream input(req.get_header_value("Cookie"));
  std::string part;
  while (std::getline(input, part, ';')) {
    auto first = part.find_first_not_of(" \t");
    if (first == std::string::npos)
      continue;
    part = part.substr(first);
    if (part.rfind("mb_oauth_browser=", 0) == 0) {
      auto value = part.substr(17);
      if (random_token(value))
        return value;
    }
  }
  return {};
}
bool equal_secret(const std::string &a, const std::string &b) {
  return a.size() == b.size() && sodium_memcmp(a.data(), b.data(), a.size()) == 0;
}
std::string challenge(const std::string &verifier) {
  unsigned char digest[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(digest, reinterpret_cast<const unsigned char *>(verifier.data()), verifier.size());
  std::array<char, 64> out{};
  sodium_bin2base64(out.data(), out.size(), digest, sizeof(digest), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
  return out.data();
}
void check_verifier(const std::string &value) {
  if (value.size() < 43 || value.size() > 128 ||
      !std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
      }))
    throw OAuthError("invalid_grant", "Invalid PKCE verifier");
}
void check_redirect(const std::string &uri) {
  if (uri.empty() || uri.size() > 2048 || uri.find('#') != std::string::npos ||
      uri.find('\\') != std::string::npos ||
      std::any_of(uri.begin(), uri.end(), [](unsigned char c) { return c <= 32 || c == 127; }))
    throw OAuthError("invalid_redirect_uri", "Invalid redirect URI");
  auto colon = uri.find("://");
  if (colon == std::string::npos)
    throw OAuthError("invalid_redirect_uri", "Redirect URI must use HTTPS or loopback HTTP");
  auto scheme = lower_ascii(uri.substr(0, colon));
  if (scheme != "https" && scheme != "http")
    throw OAuthError("invalid_redirect_uri", "Unsupported redirect URI scheme");
  auto end = uri.find_first_of("/?", colon + 3);
  auto authority =
      lower_ascii(uri.substr(colon + 3, end == std::string::npos ? std::string::npos : end - colon - 3));
  if (authority.empty() || authority.find('@') != std::string::npos ||
      authority.find('%') != std::string::npos)
    throw OAuthError("invalid_redirect_uri", "Invalid redirect host");
  std::string host, port;
  if (authority[0] == '[') {
    auto close = authority.find(']');
    if (close == std::string::npos)
      throw OAuthError("invalid_redirect_uri", "Invalid IPv6 redirect host");
    host = authority.substr(0, close + 1);
    if (close + 1 < authority.size()) {
      if (authority[close + 1] != ':')
        throw OAuthError("invalid_redirect_uri", "Invalid redirect port");
      port = authority.substr(close + 2);
    }
    if (host != "[::1]")
      throw OAuthError("invalid_redirect_uri", "Only loopback IPv6 redirects are supported");
  } else {
    auto split = authority.find(':');
    host = authority.substr(0, split);
    if (split != std::string::npos)
      port = authority.substr(split + 1);
    if (host.empty() || !std::all_of(host.begin(), host.end(),
                                     [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-'; }))
      throw OAuthError("invalid_redirect_uri", "Invalid redirect host");
  }
  if (authority.back() == ':' ||
      (!port.empty() &&
       (!std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c); }) ||
        port.size() > 5 || std::stoi(port) < 1 || std::stoi(port) > 65535)))
    throw OAuthError("invalid_redirect_uri", "Invalid redirect port");
  if (scheme == "http" && host != "localhost" && host != "127.0.0.1" && host != "[::1]")
    throw OAuthError("invalid_redirect_uri", "HTTP redirects must use a loopback host");
}
void check_resource(const std::string &value) {
  if (value != resource_url())
    throw OAuthError("invalid_target", "resource must identify this MCP endpoint");
}
Json client(Db &db, const std::string &id) {
  auto value = db.one("SELECT * FROM oauth_clients WHERE client_id=?", {id});
  if (value.is_null())
    throw OAuthError("invalid_client", "Unknown registered client", 401);
  return value;
}
void form_content(const httplib::Request &req) {
  auto content = lower_ascii(req.get_header_value("Content-Type"));
  if (content.rfind("application/x-www-form-urlencoded", 0) != 0)
    throw OAuthError("invalid_request", "Content-Type must be application/x-www-form-urlencoded");
}
void json_response(httplib::Response &res, const Json &body, int status = 200) {
  res.status = status;
  res.set_content(body.dump(), "application/json; charset=utf-8");
}
void cleanup(Db &db) {
  auto time = timestamp();
  db.query("DELETE FROM oauth_pending WHERE expires_at<?", {std::to_string(time)});
  db.query("DELETE FROM oauth_codes WHERE expires_at<?", {std::to_string(time - 86400)});
  db.query("DELETE FROM oauth_tokens WHERE refresh_expires_at<?", {std::to_string(time)});
}
void render_form(httplib::Response &res, const Json &pending, const std::string &request_id,
                 const std::string &name, const std::string &error = "") {
  std::string hidden = "<input type='hidden' name='request_id' value='" + escape_html(request_id) + "'>";
  for (const auto &key : {"client_id", "redirect_uri", "response_type", "code_challenge",
                          "code_challenge_method", "resource", "scope", "state"})
    hidden += "<input type='hidden' name='" + std::string(key) + "' value='" +
              escape_html(text(pending, key)) + "'>";
  auto html =
      std::string(
          R"HTML(<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>授权访问错题本</title><style>body{margin:0;background:#f4f5f7;color:#212631;font:16px/1.6 system-ui,sans-serif}main{max-width:440px;margin:8vh auto;padding:32px;background:white;border-radius:18px;box-shadow:0 8px 40px #172b4d12}h1{font-size:25px;margin:0 0 14px}p{color:#5a6270}label{display:block;margin-top:18px}input{box-sizing:border-box;width:100%;padding:12px;border:1px solid #d7dce2;border-radius:8px;font:inherit}button{cursor:pointer;font:inherit;padding:12px 16px;border:0;border-radius:8px;margin-top:22px}.allow{background:#295bcf;color:white}.deny{background:#eef0f4;margin-left:10px}.error{color:#b42318}.client{overflow-wrap:anywhere;color:#212631}small{display:block;overflow-wrap:anywhere;color:#667085}@media(max-width:520px){main{margin:20px;padding:24px}}</style></head><body><main><h1>授权访问错题本</h1><p>应用 <span class="client">)HTML") +
      escape_html(name) +
      "</span> 请求使用你的错题和笔记。主账户允许查询和修改，只读子账户仅允许查询。</p><small>授权后返回：" +
      escape_html(text(pending, "redirect_uri")) + "</small>";
  if (!error.empty())
    html += "<p class='error' role='alert'>" + escape_html(error) + "</p>";
  html += "<form method='post' action='" + escape_html(base_url() + "/authorize") + "'>" + hidden +
          "<label for='username'>用户名</label><input id='username' name='username' autocomplete='username' "
          "maxlength='192'><label for='password'>密码</label><input id='password' name='password' "
          "type='password' autocomplete='current-password' maxlength='1024'><button class='allow' "
          "name='approve' value='yes'>登录并授权</button><button class='deny' name='approve' "
          "value='no'>取消</button></form></main></body></html>";
  // Chromium also checks form-action on the 303 redirect after approval. The
  // callback is from the registered, validated pending request, not POST data.
  const auto callback_origin = url_origin(text(pending, "redirect_uri"));
  res.set_header("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; form-action "
                                            "'self' " + callback_origin +
                                            "; frame-ancestors 'none'; base-uri 'none'");
  res.set_header("X-Frame-Options", "DENY");
  // no-referrer makes a normal form POST use Origin: null in Chromium. Keep the
  // same-origin POST verifiable without disclosing the URL to other origins.
  res.headers.erase("Referrer-Policy");
  res.set_header("Referrer-Policy", "same-origin");
  res.set_content(html, "text/html; charset=utf-8");
}
Json authorization(Db &db, const httplib::Request &req) {
  Json data;
  for (const auto &key :
       {"client_id", "redirect_uri", "response_type", "code_challenge", "code_challenge_method", "resource"})
    data[key] = get(req, key, true, 2048);
  data["state"] = get(req, "state", false, 2048);
  auto scope = get(req, "scope");
  data["scope"] = scope.empty() ? "mcp:tools" : scope;
  if (data["response_type"] != "code")
    throw OAuthError("unsupported_response_type", "Only authorization code is supported");
  if (data["code_challenge_method"] != "S256")
    throw OAuthError("invalid_request", "S256 PKCE is required");
  auto c = text(data, "code_challenge");
  if (c.size() != 43 || !std::all_of(c.begin(), c.end(),
                                     [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; }))
    throw OAuthError("invalid_request", "Invalid S256 code challenge");
  if (data["scope"] != "mcp:tools")
    throw OAuthError("invalid_scope", "Supported scope is mcp:tools");
  check_resource(text(data, "resource"));
  auto registered = client(db, text(data, "client_id"));
  auto redirects = Json::parse(text(registered, "redirect_uris"));
  if (std::find(redirects.begin(), redirects.end(), data["redirect_uri"]) == redirects.end())
    throw OAuthError("invalid_request", "redirect_uri does not match client registration");
  return data;
}
void redirect_response(httplib::Response &res, const Json &data, const std::string &key,
                       const std::string &value) {
  auto url = text(data, "redirect_uri");
  url += (url.find('?') == std::string::npos ? '?' : '&');
  url += key + "=" + encode_url(value);
  if (!text(data, "state").empty())
    url += "&state=" + encode_url(text(data, "state"));
  url += "&iss=" + encode_url(base_url());
  res.status = 303;
  res.set_header("Location", url);
  res.set_content("", "text/plain");
}
Json issue(Db &db, const Json &grant, const std::string &family) {
  auto access = token(), refresh = token();
  auto time = timestamp();
  db.query("INSERT INTO "
           "oauth_tokens(access_hash,refresh_hash,user_id,client_id,resource,scope,authorization_id,family_"
           "id,access_expires_at,refresh_expires_at,revoked) VALUES(?,?,?,?,?,?,?,?,?,?,0)",
           {hash_token(access), hash_token(refresh), std::to_string(number(grant, "user_id")),
            text(grant, "client_id"), text(grant, "resource"), text(grant, "scope"),
            text(grant, "authorization_id"), family, std::to_string(time + 3600),
            std::to_string(time + 30 * 86400)});
  return {{"access_token", access},
          {"refresh_token", refresh},
          {"token_type", "Bearer"},
          {"expires_in", 3600},
          {"scope", text(grant, "scope")}};
}
} // namespace

void initialize_oauth(Db &db) {
  auto public_url = base_url();
  check_redirect(public_url);
  if (public_url.find('?') != std::string::npos)
    throw std::runtime_error("PUBLIC_URL must not contain a query");
  db.exec(R"SQL(
CREATE TABLE IF NOT EXISTS oauth_clients(client_id TEXT PRIMARY KEY,client_name TEXT NOT NULL,redirect_uris TEXT NOT NULL CHECK(json_valid(redirect_uris)),created_at INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS oauth_pending(request_hash TEXT PRIMARY KEY,browser_hash TEXT NOT NULL,data TEXT NOT NULL CHECK(json_valid(data)),expires_at INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS oauth_pending_expiry ON oauth_pending(expires_at);
CREATE TABLE IF NOT EXISTS oauth_codes(code_hash TEXT PRIMARY KEY,user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,client_id TEXT NOT NULL REFERENCES oauth_clients(client_id) ON DELETE CASCADE,redirect_uri TEXT NOT NULL,challenge TEXT NOT NULL,resource TEXT NOT NULL,scope TEXT NOT NULL,expires_at INTEGER NOT NULL,redeemed INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS oauth_codes_user ON oauth_codes(user_id);
CREATE INDEX IF NOT EXISTS oauth_codes_expiry ON oauth_codes(expires_at);
CREATE TABLE IF NOT EXISTS oauth_tokens(access_hash TEXT PRIMARY KEY,refresh_hash TEXT NOT NULL UNIQUE,user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,client_id TEXT NOT NULL REFERENCES oauth_clients(client_id) ON DELETE CASCADE,resource TEXT NOT NULL,scope TEXT NOT NULL,authorization_id TEXT NOT NULL,family_id TEXT NOT NULL,access_expires_at INTEGER NOT NULL,refresh_expires_at INTEGER NOT NULL,revoked INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS oauth_tokens_user ON oauth_tokens(user_id);
CREATE INDEX IF NOT EXISTS oauth_tokens_family ON oauth_tokens(family_id);
CREATE INDEX IF NOT EXISTS oauth_tokens_code ON oauth_tokens(authorization_id);
CREATE INDEX IF NOT EXISTS oauth_tokens_expiry ON oauth_tokens(refresh_expires_at);
CREATE TRIGGER IF NOT EXISTS oauth_password_changed AFTER UPDATE OF password_hash ON users WHEN OLD.password_hash<>NEW.password_hash BEGIN
 DELETE FROM oauth_codes WHERE user_id=NEW.id;
 DELETE FROM oauth_tokens WHERE user_id=NEW.id;
END;
)SQL");
}
std::optional<User> oauth_authenticate(Db &db, const std::string &access) {
  if (!random_token(access))
    return {};
  auto row = db.one("SELECT u.* FROM oauth_tokens t JOIN users u ON u.id=t.user_id WHERE t.access_hash=? AND "
                    "t.revoked=0 AND t.access_expires_at>? AND t.resource=? AND t.scope='mcp:tools'",
                    {hash_token(access), std::to_string(timestamp()), resource_url()});
  if (row.is_null())
    return {};
  return user_from(row);
}
bool oauth_route(Db &db, const httplib::Request &req, httplib::Response &res) {
  auto path = req.path;
  bool known = path == "/register" || path == "/authorize" || path == "/token" || path == "/revoke" ||
               path == "/.well-known/oauth-authorization-server" ||
               path == "/.well-known/oauth-protected-resource" ||
               path == "/.well-known/oauth-protected-resource/mcp";
  if (!known)
    return false;
  res.set_header("Cache-Control", "no-store");
  res.set_header("Pragma", "no-cache");
  try {
    if (path == "/.well-known/oauth-authorization-server" && req.method == "GET") {
      auto base = base_url();
      json_response(res, {{"issuer", base},
                          {"authorization_endpoint", base + "/authorize"},
                          {"token_endpoint", base + "/token"},
                          {"registration_endpoint", base + "/register"},
                          {"revocation_endpoint", base + "/revoke"},
                          {"response_types_supported", {"code"}},
                          {"grant_types_supported", {"authorization_code", "refresh_token"}},
                          {"token_endpoint_auth_methods_supported", {"none"}},
                          {"revocation_endpoint_auth_methods_supported", {"none"}},
                          {"code_challenge_methods_supported", {"S256"}},
                          {"scopes_supported", {"mcp:tools"}},
                          {"authorization_response_iss_parameter_supported", true}});
      return true;
    }
    if (path.starts_with("/.well-known/oauth-protected-resource") && req.method == "GET") {
      json_response(res, {{"resource", resource_url()},
                          {"authorization_servers", {base_url()}},
                          {"bearer_methods_supported", {"header"}},
                          {"scopes_supported", {"mcp:tools"}},
                          {"resource_name", "错题本 MCP"}});
      return true;
    }
    if (path == "/register" && req.method == "POST") {
      if (req.body.size() > 32768)
        throw OAuthError("invalid_client_metadata", "Registration body is too large");
      auto body = parse_body(req.body);
      auto method = text(body, "token_endpoint_auth_method", "none");
      if (method != "none")
        throw OAuthError("invalid_client_metadata",
                         "Only public PKCE clients with auth method none are supported");
      if (!body.contains("redirect_uris") || !body["redirect_uris"].is_array() ||
          body["redirect_uris"].empty() || body["redirect_uris"].size() > 20)
        throw OAuthError("invalid_client_metadata", "Provide 1 to 20 redirect_uris");
      std::set<std::string> unique;
      Json redirects = Json::array();
      for (const auto &uri : body["redirect_uris"]) {
        if (!uri.is_string())
          throw OAuthError("invalid_redirect_uri", "Redirect URIs must be strings");
        auto value = uri.get<std::string>();
        check_redirect(value);
        if (unique.insert(value).second)
          redirects.push_back(value);
      }
      for (const auto &key : {"grant_types", "response_types"})
        if (body.contains(key)) {
          if (!body[key].is_array() || body[key].empty())
            throw OAuthError("invalid_client_metadata", std::string("Invalid ") + key);
          for (const auto &value : body[key])
            if (!value.is_string() || (std::string(key) == "grant_types"
                                           ? (value != "authorization_code" && value != "refresh_token")
                                           : value != "code"))
              throw OAuthError("invalid_client_metadata", std::string("Unsupported ") + key);
        }
      auto name = text(body, "client_name", "MCP Client");
      if (name.empty() || name.size() > 200)
        throw OAuthError("invalid_client_metadata", "client_name must contain 1 to 200 bytes");
      auto id = token();
      OAuthTx tx(db);
      if (db.one("SELECT count(*) AS n FROM oauth_clients")["n"].get<int64_t>() >= 10000)
        throw OAuthError("temporarily_unavailable", "Client registration capacity reached", 503);
      db.query("INSERT INTO oauth_clients(client_id,client_name,redirect_uris,created_at) VALUES(?,?,?,?)",
               {id, name, redirects.dump(), std::to_string(timestamp())});
      tx.commit();
      json_response(res,
                    {{"client_id", id},
                     {"client_id_issued_at", timestamp()},
                     {"client_name", name},
                     {"redirect_uris", redirects},
                     {"token_endpoint_auth_method", "none"},
                     {"grant_types", {"authorization_code", "refresh_token"}},
                     {"response_types", {"code"}}},
                    201);
      return true;
    }
    if (path == "/authorize" && req.method == "GET") {
      auto data = authorization(db, req), registered = client(db, text(data, "client_id"));
      auto browser = browser_cookie(req);
      if (browser.empty())
        browser = token();
      auto request_id = token();
      OAuthTx tx(db);
      cleanup(db);
      db.query("INSERT INTO oauth_pending(request_hash,browser_hash,data,expires_at) VALUES(?,?,?,?)",
               {hash_token(request_id), hash_token(browser), data.dump(), std::to_string(timestamp() + 600)});
      tx.commit();
      res.set_header("Set-Cookie", "mb_oauth_browser=" + browser +
                                       "; Path=/authorize; HttpOnly; SameSite=Lax; Max-Age=900" +
                                       (base_url().starts_with("https://") ? "; Secure" : ""));
      render_form(res, data, request_id, text(registered, "client_name"));
      return true;
    }
    if (path == "/authorize" && req.method == "POST") {
      form_content(req);
      auto origin = req.get_header_value("Origin");
      if (!origin.empty() && origin != url_origin(base_url()))
        throw OAuthError("invalid_request", "Authorization form origin mismatch", 403);
      auto id = get(req, "request_id", true, 128), browser = browser_cookie(req);
      auto pending = db.one("SELECT * FROM oauth_pending WHERE request_hash=? AND expires_at>?",
                            {hash_token(id), std::to_string(timestamp())});
      if (pending.is_null() || browser.empty() ||
          !equal_secret(text(pending, "browser_hash"), hash_token(browser)))
        throw OAuthError("invalid_request", "Authorization form expired or browser verification failed", 403);
      auto data = Json::parse(text(pending, "data"));
      auto registered = client(db, text(data, "client_id"));
      auto approve = get(req, "approve", true, 8);
      if (approve != "yes" && approve != "no")
        throw OAuthError("invalid_request", "Explicit authorization is required");
      if (approve == "no") {
        db.query("DELETE FROM oauth_pending WHERE request_hash=?", {hash_token(id)});
        redirect_response(res, data, "error", "access_denied");
        return true;
      }
      auto username = get(req, "username", true, 192), password = get(req, "password", true, 1024);
      auto user = db.one("SELECT * FROM users WHERE username=?", {username});
      static const std::string dummy = password_hash("oauth-dummy-password-not-an-account");
      bool ok = verify(password, user.is_null() ? dummy : text(user, "password_hash"));
      if (!ok || user.is_null()) {
        res.status = 401;
        render_form(res, data, id, text(registered, "client_name"), "用户名或密码不正确");
        return true;
      }
      auto code = token();
      OAuthTx tx(db);
      // Consume the browser-bound request under the same lock as code
      // creation. Recheck password state to avoid a credential-reset race.
      auto fresh = db.one("SELECT password_hash FROM users WHERE id=?", {std::to_string(number(user, "id"))});
      if (fresh.is_null() || text(fresh, "password_hash") != text(user, "password_hash"))
        throw OAuthError("access_denied", "Credentials changed; please authorize again", 401);
      db.query("DELETE FROM oauth_pending WHERE request_hash=? AND browser_hash=? AND expires_at>?",
               {hash_token(id), hash_token(browser), std::to_string(timestamp())});
      if (db.changes() != 1)
        throw OAuthError("invalid_request", "Authorization form was already used", 403);
      db.query("INSERT INTO "
               "oauth_codes(code_hash,user_id,client_id,redirect_uri,challenge,resource,scope,expires_at) "
               "VALUES(?,?,?,?,?,?,?,?)",
               {hash_token(code), std::to_string(number(user, "id")), text(data, "client_id"),
                text(data, "redirect_uri"), text(data, "code_challenge"), text(data, "resource"),
                text(data, "scope"), std::to_string(timestamp() + 300)});
      tx.commit();
      redirect_response(res, data, "code", code);
      return true;
    }
    if (path == "/token" && req.method == "POST") {
      form_content(req);
      auto cid = get(req, "client_id", true, 128);
      client(db, cid);
      auto grant = get(req, "grant_type", true, 64), resource = get(req, "resource", true, 2048);
      check_resource(resource);
      if (!get(req, "client_secret").empty() || !req.get_header_value("Authorization").empty())
        throw OAuthError("invalid_client", "Public clients must use client_id and PKCE", 401);
      if (grant == "authorization_code") {
        auto code = get(req, "code", true, 128), redirect = get(req, "redirect_uri", true, 2048),
             verifier = get(req, "code_verifier", true, 128);
        check_verifier(verifier);
        OAuthTx tx(db);
        auto row = db.one("SELECT * FROM oauth_codes WHERE code_hash=?", {hash_token(code)});
        if (row.is_null() || text(row, "client_id") != cid || text(row, "redirect_uri") != redirect ||
            text(row, "resource") != resource || number(row, "expires_at") <= timestamp() ||
            !equal_secret(text(row, "challenge"), challenge(verifier)))
          throw OAuthError("invalid_grant", "Authorization code or PKCE verification failed");
        if (number(row, "redeemed")) {
          db.query("UPDATE oauth_tokens SET revoked=1 WHERE authorization_id=?", {hash_token(code)});
          tx.commit();
          throw OAuthError("invalid_grant", "Authorization code was already used");
        }
        db.query("UPDATE oauth_codes SET redeemed=1 WHERE code_hash=? AND redeemed=0", {hash_token(code)});
        if (db.changes() != 1)
          throw OAuthError("invalid_grant", "Authorization code was already used");
        row["authorization_id"] = hash_token(code);
        auto result = issue(db, row, token());
        cleanup(db);
        tx.commit();
        json_response(res, result);
        return true;
      }
      if (grant == "refresh_token") {
        auto refresh = get(req, "refresh_token", true, 128), scope = get(req, "scope");
        if (!scope.empty() && scope != "mcp:tools")
          throw OAuthError("invalid_scope", "Cannot expand authorization scope");
        OAuthTx tx(db);
        auto row = db.one("SELECT * FROM oauth_tokens WHERE refresh_hash=?", {hash_token(refresh)});
        if (row.is_null() || text(row, "client_id") != cid || text(row, "resource") != resource ||
            number(row, "refresh_expires_at") <= timestamp())
          throw OAuthError("invalid_grant", "Invalid refresh token");
        if (number(row, "revoked")) {
          db.query("UPDATE oauth_tokens SET revoked=1 WHERE family_id=?", {text(row, "family_id")});
          tx.commit();
          throw OAuthError("invalid_grant", "Refresh token replay detected; authorize again");
        }
        db.query("UPDATE oauth_tokens SET revoked=1 WHERE access_hash=?", {text(row, "access_hash")});
        auto result = issue(db, row, text(row, "family_id"));
        cleanup(db);
        tx.commit();
        json_response(res, result);
        return true;
      }
      throw OAuthError("unsupported_grant_type", "Only authorization_code and refresh_token are supported");
    }
    if (path == "/revoke" && req.method == "POST") {
      form_content(req);
      auto cid = get(req, "client_id", true, 128);
      client(db, cid);
      auto secret = get(req, "token", true, 4096), hash = hash_token(secret);
      OAuthTx tx(db);
      auto row =
          db.one("SELECT family_id FROM oauth_tokens WHERE client_id=? AND (access_hash=? OR refresh_hash=?)",
                 {cid, hash, hash});
      if (!row.is_null())
        db.query("UPDATE oauth_tokens SET revoked=1 WHERE family_id=?", {text(row, "family_id")});
      tx.commit();
      json_response(res, Json::object());
      return true;
    }
    throw OAuthError("invalid_request", "Method not allowed", 405);
  } catch (const OAuthError &e) {
    json_response(res, {{"error", e.code}, {"error_description", e.what()}}, e.status);
  } catch (const Error &e) {
    json_response(res, {{"error", "invalid_request"}, {"error_description", e.what()}},
                  e.status == 422 ? 400 : e.status);
  } catch (const Json::exception &) {
    json_response(res, {{"error", "invalid_request"}, {"error_description", "Invalid JSON field type"}}, 400);
  }
  return true;
}
} // namespace mb
