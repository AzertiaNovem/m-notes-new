#include "core.hpp"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
namespace mb {
Db::Db(const std::string &path) {
  if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                      nullptr) != SQLITE_OK) {
    std::string err = sqlite3_errmsg(db_);
    sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error(err);
  }
  sqlite3_busy_timeout(db_, 10000);
  exec("PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA cache_size=-8192; "
       "PRAGMA temp_store=MEMORY;");
}
Db::~Db() {
  for (auto &[sql, s] : statements_)
    sqlite3_finalize(s);
  if (db_)
    sqlite3_close(db_);
}
Json Db::query(const std::string &sql, const Params &params) {
  sqlite3_stmt *stmt = nullptr;
  auto found = statements_.find(sql);
  if (found != statements_.end())
    stmt = found->second;
  else {
    if (sqlite3_prepare_v3(db_, sql.c_str(), -1, SQLITE_PREPARE_PERSISTENT, &stmt, nullptr) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db_));
    if (statements_.size() > 256) {
      sqlite3_finalize(statements_.begin()->second);
      statements_.erase(statements_.begin());
    }
    statements_[sql] = stmt;
  }
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
  struct Reset {
    sqlite3_stmt *s;
    ~Reset() {
      sqlite3_reset(s);
      sqlite3_clear_bindings(s);
    }
  } reset{stmt};
  for (size_t i = 0; i < params.size(); ++i) {
    if (params[i])
      sqlite3_bind_text(stmt, static_cast<int>(i + 1), params[i]->c_str(),
                        static_cast<int>(params[i]->size()), SQLITE_TRANSIENT);
    else
      sqlite3_bind_null(stmt, static_cast<int>(i + 1));
  }
  Json result = Json::array();
  int rc;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    Json row = Json::object();
    for (int i = 0; i < sqlite3_column_count(stmt); ++i) {
      const char *key = sqlite3_column_name(stmt, i);
      switch (sqlite3_column_type(stmt, i)) {
      case SQLITE_INTEGER:
        row[key] = sqlite3_column_int64(stmt, i);
        break;
      case SQLITE_FLOAT:
        row[key] = sqlite3_column_double(stmt, i);
        break;
      case SQLITE_NULL:
        row[key] = nullptr;
        break;
      default:
        row[key] = std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, i)),
                               sqlite3_column_bytes(stmt, i));
      }
    }
    result.push_back(std::move(row));
  }
  if (rc != SQLITE_DONE) {
    int ext = sqlite3_extended_errcode(db_);
    if (ext == SQLITE_CONSTRAINT_UNIQUE)
      throw Error(409, "名称或用户名已被占用");
    if ((ext & 255) == SQLITE_CONSTRAINT)
      throw Error(409, "数据关联冲突，请检查后重试");
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      throw Error(503, "数据库繁忙，请稍后重试");
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  return result;
}
Json Db::one(const std::string &sql, const Params &p) {
  auto r = query(sql, p);
  return r.empty() ? Json() : r[0];
}
void Db::exec(const std::string &sql) {
  char *err = nullptr;
  int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    std::string msg = err ? err : "database error";
    sqlite3_free(err);
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      throw Error(503, "数据库繁忙，请稍后重试");
    throw std::runtime_error(msg);
  }
}
int64_t Db::last_id() const {
  return sqlite3_last_insert_rowid(db_);
}
int Db::changes() const {
  return sqlite3_changes(db_);
}
std::string now() {
  auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
  gmtime_r(&t, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}
std::string text(const Json &j, const std::string &key, const std::string &fallback) {
  if (!j.contains(key) || j[key].is_null())
    return fallback;
  if (!j[key].is_string())
    throw Error(422, key + " 必须是文本");
  return j[key].get<std::string>();
}
std::string required(const Json &j, const std::string &key, size_t max) {
  auto s = text(j, key);
  if (s.empty() || s.find_first_not_of(" \r\n\t") == std::string::npos || s.size() > max)
    throw Error(422, key + " 不能为空或过长");
  return s;
}
int64_t number(const Json &j, const std::string &key, int64_t fallback) {
  if (!j.contains(key) || j[key].is_null())
    return fallback;
  if (!j[key].is_number_integer())
    throw Error(422, key + " 必须是整数");
  return j[key].get<int64_t>();
}
int64_t parse_id(const std::string &s) {
  try {
    size_t pos = 0;
    auto id = std::stoll(s, &pos);
    if (pos != s.size() || id <= 0)
      throw Error(400, "无效 ID");
    return id;
  } catch (const Error &) {
    throw;
  } catch (...) {
    throw Error(400, "无效 ID");
  }
}
Json parse_body(const std::string &body) {
  if (body.empty())
    return Json::object();
  try {
    auto j = Json::parse(body);
    if (!j.is_object())
      throw Error(400, "请求必须为 JSON 对象");
    return j;
  } catch (const Json::exception &) {
    throw Error(400, "无效 JSON");
  }
}
void validate_audit(const Json &b) {
  required(b, "editor_tool", 64);
  required(b, "change_summary", 2000);
}
Json User::json() const {
  return {{"id", id},
          {"owner_id", owner_id ? Json(owner_id) : Json(nullptr)},
          {"username", username},
          {"role", role},
          {"created_at", created_at}};
}
void Store::writable() const {
  if (user.readonly())
    throw Error(403, "只读子账户不能修改数据");
}
static Json record(const Json &r) {
  auto j = Json::parse(r.at("data").get<std::string>());
  j["id"] = r.at("id");
  j["created_at"] = r.at("created_at");
  j["updated_at"] = r.at("updated_at");
  return j;
}
Json Store::get(const std::string &kind, int64_t id) {
  auto r = db.one("SELECT id,data,created_at,updated_at FROM records WHERE owner_id=? AND type=? AND id=?",
                  {std::to_string(user.tenant()), kind, std::to_string(id)});
  if (r.is_null())
    throw Error(404, "内容不存在");
  return record(r);
}
Json Store::list(const std::string &kind) {
  auto rows =
      db.query("SELECT id,data,created_at,updated_at FROM records WHERE owner_id=? AND type=? ORDER BY id",
               {std::to_string(user.tenant()), kind});
  Json out = Json::array();
  for (auto &r : rows)
    out.push_back(record(r));
  return out;
}
Json Store::save(const std::string &kind, Json data, int64_t id) {
  writable();
  if (!data.is_object())
    throw Error(422, "无效内容");
  data.erase("id");
  data.erase("owner_id");
  data.erase("created_at");
  data.erase("updated_at");
  auto t = now();
  if (id) {
    get(kind, id);
    db.query("UPDATE records SET data=?,updated_at=? WHERE id=? AND owner_id=? AND type=?",
             {data.dump(), t, std::to_string(id), std::to_string(user.tenant()), kind});
  } else {
    db.query("INSERT INTO records(owner_id,type,data,created_at,updated_at) VALUES(?,?,?,?,?)",
             {std::to_string(user.tenant()), kind, data.dump(), t, t});
    id = db.last_id();
  }
  return get(kind, id);
}
void Store::erase(const std::string &kind, int64_t id) {
  writable();
  get(kind, id);
  db.query("DELETE FROM records WHERE owner_id=? AND type=? AND id=?",
           {std::to_string(user.tenant()), kind, std::to_string(id)});
}
void initialize(Db &db) {
  db.exec(R"SQL(
CREATE TABLE IF NOT EXISTS users(
 id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL COLLATE NOCASE UNIQUE,
 password_hash TEXT NOT NULL,role TEXT NOT NULL CHECK(role IN ('user','readonly')),
 owner_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
 created_at TEXT NOT NULL,
 CHECK((role='user' AND owner_id IS NULL) OR (role='readonly' AND owner_id IS NOT NULL))
);
CREATE INDEX IF NOT EXISTS users_owner ON users(owner_id);
CREATE TRIGGER IF NOT EXISTS subaccount_limit BEFORE INSERT ON users WHEN NEW.owner_id IS NOT NULL BEGIN
 SELECT CASE WHEN (SELECT count(*) FROM users WHERE owner_id=NEW.owner_id)>=5 THEN RAISE(ABORT,'subaccount limit') END;
 SELECT CASE WHEN NOT EXISTS(SELECT 1 FROM users WHERE id=NEW.owner_id AND role='user' AND owner_id IS NULL) THEN RAISE(ABORT,'invalid owner') END;
END;
CREATE TABLE IF NOT EXISTS sessions(token_hash TEXT PRIMARY KEY,user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,expires_at INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS sessions_user ON sessions(user_id);
CREATE INDEX IF NOT EXISTS sessions_expiry ON sessions(expires_at);
CREATE TABLE IF NOT EXISTS records(
 id INTEGER PRIMARY KEY AUTOINCREMENT,owner_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
 type TEXT NOT NULL,data TEXT NOT NULL CHECK(json_valid(data)),created_at TEXT NOT NULL,updated_at TEXT NOT NULL,
 subject TEXT GENERATED ALWAYS AS (json_extract(data,'$.subject')) VIRTUAL,
 name TEXT GENERATED ALWAYS AS (json_extract(data,'$.name')) VIRTUAL,
 title TEXT GENERATED ALWAYS AS (json_extract(data,'$.title')) VIRTUAL,
 parent_id INTEGER GENERATED ALWAYS AS (json_extract(data,'$.parent_id')) VIRTUAL
);
CREATE INDEX IF NOT EXISTS records_owner_type ON records(owner_id,type,id DESC);
CREATE INDEX IF NOT EXISTS records_subject ON records(owner_id,type,subject,id DESC);
CREATE INDEX IF NOT EXISTS records_parent ON records(owner_id,type,parent_id,id);
CREATE INDEX IF NOT EXISTS records_log_problem ON records(owner_id,type,json_extract(data,'$.problem_id'),id DESC);
CREATE INDEX IF NOT EXISTS records_log_note ON records(owner_id,type,json_extract(data,'$.note_id'),id DESC);
CREATE INDEX IF NOT EXISTS records_print_page ON records(owner_id,type,json_extract(data,'$.book_id'),id);
CREATE UNIQUE INDEX IF NOT EXISTS records_subject_name ON records(owner_id,type,name) WHERE type='subject';
CREATE UNIQUE INDEX IF NOT EXISTS records_tag_name ON records(owner_id,type,subject,name) WHERE type='tag';
CREATE VIRTUAL TABLE IF NOT EXISTS records_fts USING fts5(title,body,tokenize='trigram');
CREATE TRIGGER IF NOT EXISTS records_fts_insert AFTER INSERT ON records WHEN NEW.type IN ('problem','note') BEGIN
 INSERT INTO records_fts(rowid,title,body) VALUES(NEW.id,coalesce(NEW.title,''),coalesce(json_extract(NEW.data,'$.stem_md'),'')||' '||coalesce(json_extract(NEW.data,'$.solution.approach_md'),'')||' '||coalesce(json_extract(NEW.data,'$.solution.answer_md'),'')||' '||coalesce(json_extract(NEW.data,'$.body_md'),'')||' '||coalesce(json_extract(NEW.data,'$.tags'),'')||' '||coalesce(json_extract(NEW.data,'$.mistake_note'),''));
END;
CREATE TRIGGER IF NOT EXISTS records_fts_delete AFTER DELETE ON records WHEN OLD.type IN ('problem','note') BEGIN
 DELETE FROM records_fts WHERE rowid=OLD.id;
END;
DROP TRIGGER IF EXISTS records_fts_update;
CREATE TRIGGER records_fts_update AFTER UPDATE ON records
WHEN NEW.type IN ('problem','note') AND (
 OLD.title IS NOT NEW.title
 OR json_extract(OLD.data,'$.stem_md') IS NOT json_extract(NEW.data,'$.stem_md')
 OR json_extract(OLD.data,'$.solution') IS NOT json_extract(NEW.data,'$.solution')
 OR json_extract(OLD.data,'$.body_md') IS NOT json_extract(NEW.data,'$.body_md')
 OR json_extract(OLD.data,'$.tags') IS NOT json_extract(NEW.data,'$.tags')
 OR json_extract(OLD.data,'$.mistake_note') IS NOT json_extract(NEW.data,'$.mistake_note')
) BEGIN
 DELETE FROM records_fts WHERE rowid=OLD.id;
 INSERT INTO records_fts(rowid,title,body) VALUES(NEW.id,coalesce(NEW.title,''),coalesce(json_extract(NEW.data,'$.stem_md'),'')||' '||coalesce(json_extract(NEW.data,'$.solution.approach_md'),'')||' '||coalesce(json_extract(NEW.data,'$.solution.answer_md'),'')||' '||coalesce(json_extract(NEW.data,'$.body_md'),'')||' '||coalesce(json_extract(NEW.data,'$.tags'),'')||' '||coalesce(json_extract(NEW.data,'$.mistake_note'),''));
END;
PRAGMA user_version=1;
)SQL");
}
} // namespace mb
