#pragma once
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <vector>
namespace mb {
using Json = nlohmann::json;
using Params = std::vector<std::optional<std::string>>;
using Query = std::map<std::string, std::string>;
struct Error : std::runtime_error {
  int status;
  Error(int s, const std::string &m) : std::runtime_error(m), status(s) {}
};
class Db {
  sqlite3 *db_ = nullptr;
  std::map<std::string, sqlite3_stmt *> statements_;

public:
  explicit Db(const std::string &path);
  ~Db();
  Db(const Db &) = delete;
  Json query(const std::string &sql, const Params &params = {});
  Json one(const std::string &sql, const Params &params = {});
  void exec(const std::string &sql);
  int64_t last_id() const;
  int changes() const;
};
struct User {
  int64_t id = 0, owner_id = 0;
  std::string username, role, created_at;
  int64_t tenant() const {
    return owner_id ? owner_id : id;
  }
  bool readonly() const {
    return role == "readonly";
  }
  Json json() const;
};
class Store {
public:
  Db &db;
  User user;
  Store(Db &d, User u) : db(d), user(std::move(u)) {}
  Json get(const std::string &kind, int64_t id);
  Json list(const std::string &kind);
  Json save(const std::string &kind, Json data, int64_t id = 0);
  void erase(const std::string &kind, int64_t id);
  void writable() const;
};
std::string now();
std::string text(const Json &j, const std::string &key, const std::string &fallback = "");
std::string required(const Json &j, const std::string &key, size_t max = 1000000);
int64_t number(const Json &j, const std::string &key, int64_t fallback = 0);
int64_t parse_id(const std::string &s);
Json parse_body(const std::string &body);
void validate_audit(const Json &body);
std::optional<Json> content_route(Store &, const std::string &method, const std::string &path,
                                  const Json &body, const Query &query);
std::optional<Json> notes_route(Store &, const std::string &method, const std::string &path, const Json &body,
                                const Query &query);
std::optional<Json> print_route(Store &, const std::string &method, const std::string &path, const Json &body,
                                const Query &query);
Json content_search(Store &, const Json &body);
void initialize(Db &db);
} // namespace mb
