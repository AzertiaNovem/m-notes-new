#include "core.hpp"
#include "notes.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <list>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>

namespace mb {
namespace {
std::string normalize(std::string s) {
  auto space = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && space(s.back()))
    s.pop_back();
  size_t start = 0;
  while (start < s.size() && space(s[start]))
    ++start;
  return s.substr(start);
}
std::string lower(std::string s) {
  for (auto &c : s)
    if (static_cast<unsigned char>(c) < 128)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::vector<std::string> characters(const std::string &s) {
  std::vector<std::string> out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = s[i];
    size_t n =
        c < 0x80 ? 1 : ((c & 0xe0) == 0xc0 ? 2 : ((c & 0xf0) == 0xe0 ? 3 : ((c & 0xf8) == 0xf0 ? 4 : 1)));
    n = std::min(n, s.size() - i);
    out.push_back(s.substr(i, n));
    i += n;
  }
  return out;
}
uint64_t hash64(const std::string &s) {
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  return h;
}
double dot(const std::vector<float> &a, const std::vector<float> &b) {
  double d = 0;
  for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
    d += a[i] * b[i];
  return std::clamp(d, -1.0, 1.0);
}
std::string owner(Store &s) {
  return std::to_string(s.user.tenant());
}
Json hydrate(const Json &row) {
  Json j = Json::parse(row.at("data").get<std::string>());
  j["id"] = row.at("id");
  j["created_at"] = row.at("created_at");
  j["updated_at"] = row.at("updated_at");
  return j;
}
std::string param(const Query &q, const std::string &key, const std::string &fallback = "") {
  auto it = q.find(key);
  return it == q.end() ? fallback : it->second;
}
int integer(const Query &q, const std::string &key, int fallback, int lo, int hi) {
  auto it = q.find(key);
  if (it == q.end())
    return fallback;
  try {
    size_t used = 0;
    long n = std::stol(it->second, &used);
    if (used != it->second.size() || n < lo || n > hi)
      throw Error(400, "invalid " + key);
    return static_cast<int>(n);
  } catch (const Error &) {
    throw;
  } catch (...) {
    throw Error(400, "invalid " + key);
  }
}
int integer(const Json &j, const std::string &key, int fallback, int lo, int hi) {
  if (!j.contains(key))
    return fallback;
  if (!j[key].is_number_integer())
    throw Error(400, key + " must be an integer");
  auto n = j[key].get<int64_t>();
  if (n < lo || n > hi)
    throw Error(400, "invalid " + key);
  return static_cast<int>(n);
}
Json names(const Json &j, const std::string &key) {
  Json result = Json::array();
  std::set<std::string> seen;
  if (!j.contains(key))
    return result;
  if (!j[key].is_array() || j[key].size() > 200)
    throw Error(400, key + " must be an array with at most 200 names");
  for (const auto &v : j[key]) {
    if (!v.is_string())
      throw Error(400, key + " must contain strings");
    auto n = normalize(v.get<std::string>());
    if (n.size() > 256)
      throw Error(400, key + " name is too long");
    if (!n.empty() && seen.insert(n).second)
      result.push_back(n);
  }
  return result;
}
std::string require_subject(Store &s, const std::string &raw) {
  auto name = normalize(raw);
  auto row =
      s.db.one("SELECT id FROM records WHERE owner_id=? AND type='subject' AND name=?", {owner(s), name});
  if (row.empty())
    throw Error(400, "未知科目，请先在设置中创建科目：" + name);
  return name;
}
Json tag_ref(const Json &tag) {
  return Json{{"id", tag.at("id")},
              {"name", tag.at("name")},
              {"subject", tag.at("subject")},
              {"aliases", tag.value("aliases", Json::array())}};
}
Json tags(Store &s, const std::string &subject = "") {
  std::string sql = "SELECT id,data,created_at,updated_at FROM records WHERE owner_id=? AND type='tag'";
  Params ps{owner(s)};
  if (!subject.empty()) {
    sql += " AND subject=?";
    ps.push_back(normalize(subject));
  }
  sql += " ORDER BY subject,name,id";
  Json result = Json::array();
  for (const auto &row : s.db.query(sql, ps))
    result.push_back(tag_ref(hydrate(row)));
  return result;
}
Json subjects(Store &s) {
  static const std::vector<std::string> seeds = {"语文", "数学", "英语", "物理", "化学",
                                                 "生物", "历史", "地理", "政治"};
  std::set<std::string> present;
  for (const auto &row :
       s.db.query("SELECT name FROM records WHERE owner_id=? AND type='subject' ORDER BY name", {owner(s)}))
    present.insert(row.at("name").get<std::string>());
  Json result = Json::array();
  for (const auto &seed : seeds)
    if (present.erase(seed))
      result.push_back(seed);
  for (const auto &n : present)
    result.push_back(n);
  return result;
}
double merge_threshold() {
  const char *raw = std::getenv("TAG_MERGE_THRESHOLD");
  if (!raw)
    return .92;
  try {
    double v = std::stod(raw);
    if (std::isfinite(v) && v >= 0 && v <= 1)
      return v;
  } catch (...) {
  }
  return .92;
}
Json resolve_tags(Store &s, const std::string &subject, const Json &inputs) {
  auto existing = tags(s, subject);
  Json result = Json::array();
  for (const auto &input : inputs) {
    auto name = input.get<std::string>();
    Json match;
    std::string via;
    Json score = nullptr;
    for (const auto &tag : existing)
      if (tag["name"] == name) {
        match = tag;
        via = "exact";
        score = 1;
        break;
      }
    if (match.is_null())
      for (const auto &tag : existing)
        if (std::find(tag["aliases"].begin(), tag["aliases"].end(), input) != tag["aliases"].end()) {
          match = tag;
          via = "alias";
          score = 1;
          break;
        }
    if (match.is_null()) {
      double best = -1;
      size_t best_index = 0;
      for (size_t i = 0; i < existing.size(); ++i) {
        double sim = text_similarity(name, text(existing[i], "name"));
        if (sim > best) {
          best = sim;
          best_index = i;
        }
      }
      if (best >= merge_threshold() && !existing.empty()) {
        match = existing[best_index];
        match["aliases"].push_back(name);
        auto saved = s.save("tag", match, number(match, "id"));
        existing[best_index] = tag_ref(saved);
        via = "auto_merged";
        score = best;
      } else {
        match = tag_ref(s.save("tag", {{"name", name}, {"subject", subject}, {"aliases", Json::array()}}));
        existing.push_back(match);
        via = "created";
      }
    }
    result.push_back(
        {{"input", name}, {"tag_id", match["id"]}, {"name", match["name"]}, {"via", via}, {"score", score}});
  }
  return result;
}
Json canonical_names(const Json &resolutions) {
  Json out = Json::array();
  std::set<std::string> seen;
  for (const auto &r : resolutions) {
    auto n = text(r, "name");
    if (seen.insert(n).second)
      out.push_back(n);
  }
  return out;
}
Json summary(const Json &p) {
  return {{"id", p.at("id")},
          {"title", p.at("title")},
          {"subject", p.at("subject")},
          {"source", p.value("source", Json(nullptr))},
          {"difficulty", p.value("difficulty", Json(nullptr))},
          {"tags", p.value("tags", Json::array())},
          {"created_at", p.at("created_at")},
          {"updated_at", p.at("updated_at")}};
}
Json logs(Store &s, int64_t id) {
  s.get("problem", id);
  Json out = Json::array();
  for (const auto &row : s.db.query("SELECT id,data,created_at,updated_at FROM records WHERE owner_id=? AND "
                                    "type='problem_log' AND json_extract(data,'$.problem_id')=CAST(? AS "
                                    "INTEGER) ORDER BY json_extract(data,'$.changed_at') DESC,id DESC",
                                    {owner(s), std::to_string(id)})) {
    auto log = hydrate(row);
    out.push_back({{"id", log["id"]},
                   {"editor_tool", log["editor_tool"]},
                   {"change_summary", log["change_summary"]},
                   {"changed_at", log["changed_at"]}});
  }
  return out;
}
Json problem_detail(Store &s, int64_t id, const Json &resolutions = Json(), const Json &related = Json()) {
  auto p = s.get("problem", id);
  Json out = summary(p);
  for (const auto &key : {"stem_md", "solution", "diagrams"})
    out[key] = p.at(key);
  out["mistake_note"] = p.value("mistake_note", Json(nullptr));
  if (!resolutions.is_null())
    out["tag_resolution"] = resolutions;
  else {
    out["tag_resolution"] = Json::array();
    auto list = tags(s, text(p, "subject"));
    for (const auto &name : p["tags"])
      for (const auto &tag : list)
        if (tag["name"] == name) {
          out["tag_resolution"].push_back(
              {{"input", name}, {"tag_id", tag["id"]}, {"name", name}, {"via", "exact"}, {"score", 1}});
          break;
        }
  }
  out["links"] = entity_links(s, "problem", id);
  out["related"] = related.is_null() ? Json::array() : related;
  out["change_logs"] = logs(s, id);
  return out;
}
void validate_svg(const std::string &svg) {
  // A small strict XML reader for geometry-only SVG. No DTDs, entities,
  // scripts, HTML, animation, stylesheets or external resources are allowed.
  static const std::set<std::string> allowed = {"svg",
                                                "g",
                                                "defs",
                                                "symbol",
                                                "use",
                                                "path",
                                                "rect",
                                                "circle",
                                                "ellipse",
                                                "line",
                                                "polyline",
                                                "polygon",
                                                "text",
                                                "tspan",
                                                "title",
                                                "desc",
                                                "marker",
                                                "pattern",
                                                "clippath",
                                                "mask",
                                                "lineargradient",
                                                "radialgradient",
                                                "stop",
                                                "filter",
                                                "fegaussianblur",
                                                "feoffset",
                                                "fecolormatrix",
                                                "feblend",
                                                "fecomposite",
                                                "femerge",
                                                "femergenode",
                                                "feflood",
                                                "femorphology",
                                                "feturbulence",
                                                "fedisplacementmap",
                                                "fecomponenttransfer",
                                                "fefuncr",
                                                "fefuncg",
                                                "fefuncb",
                                                "fefunca",
                                                "textpath"};
  auto reject = []() { throw Error(400, "配图必须是安全的 SVG 几何图形，不能包含脚本、HTML 或外部资源"); };
  auto isname = [](unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '-' || c == ':' || c == '.';
  };
  size_t i = 0;
  std::vector<std::string> stack;
  bool saw_root = false, closed_root = false;
  size_t nodes = 0;
  auto whitespace = [&]() {
    while (i < svg.size() && std::isspace(static_cast<unsigned char>(svg[i])))
      ++i;
  };
  whitespace();
  if (svg.compare(i, 5, "<?xml") == 0) {
    auto end = svg.find("?>", i + 5);
    if (end == std::string::npos)
      reject();
    i = end + 2;
  }
  while (i < svg.size()) {
    if (svg[i] != '<') {
      if (stack.empty() && !std::isspace(static_cast<unsigned char>(svg[i])))
        reject();
      ++i;
      continue;
    }
    if (svg.compare(i, 4, "<!--") == 0) {
      auto end = svg.find("-->", i + 4);
      if (end == std::string::npos)
        reject();
      i = end + 3;
      continue;
    }
    ++i;
    if (i >= svg.size() || svg[i] == '!' || svg[i] == '?')
      reject();
    bool closing = svg[i] == '/';
    if (closing)
      ++i;
    size_t start = i;
    while (i < svg.size() && isname(svg[i]))
      ++i;
    auto tag = svg.substr(start, i - start), tag_lower = lower(tag);
    if (tag.empty() || !allowed.count(tag_lower))
      reject();
    if (closing) {
      whitespace();
      if (i >= svg.size() || svg[i] != '>' || stack.empty() || stack.back() != tag)
        reject();
      ++i;
      stack.pop_back();
      if (stack.empty())
        closed_root = true;
      continue;
    }
    if (closed_root || (!saw_root && tag_lower != "svg") || ++nodes > 20000)
      reject();
    saw_root = true;
    bool selfclosing = false;
    std::set<std::string> attrs;
    while (i < svg.size()) {
      whitespace();
      if (i >= svg.size())
        reject();
      if (svg[i] == '>') {
        ++i;
        break;
      }
      if (svg[i] == '/' && i + 1 < svg.size() && svg[i + 1] == '>') {
        i += 2;
        selfclosing = true;
        break;
      }
      start = i;
      while (i < svg.size() && isname(svg[i]))
        ++i;
      auto attr = lower(svg.substr(start, i - start));
      if (attr.empty() || !attrs.insert(attr).second)
        reject();
      whitespace();
      if (i >= svg.size() || svg[i] != '=')
        reject();
      ++i;
      whitespace();
      if (i >= svg.size() || (svg[i] != '\'' && svg[i] != '"'))
        reject();
      char quote = svg[i++];
      start = i;
      while (i < svg.size() && svg[i] != quote) {
        if (svg[i] == '<')
          reject();
        ++i;
      }
      if (i >= svg.size())
        reject();
      auto value = normalize(svg.substr(start, i - start));
      ++i;
      auto local = attr.substr(attr.find(':') == std::string::npos ? 0 : attr.find(':') + 1);
      auto value_lower = lower(value);
      if (local.rfind("on", 0) == 0 || attr == "xml:base" || local == "src")
        reject();
      if (attr == "xmlns" && value != "http://www.w3.org/2000/svg")
        reject();
      if (attr == "xmlns:xlink" && value != "http://www.w3.org/1999/xlink")
        reject();
      if (local == "href") {
        if (value.size() < 2 || value[0] != '#' || !std::all_of(value.begin() + 1, value.end(), isname))
          reject();
      }
      if (value_lower.find("javascript:") != std::string::npos ||
          value_lower.find("data:") != std::string::npos ||
          value_lower.find("expression(") != std::string::npos)
        reject();
      if (attr == "style" && (value.find('\\') != std::string::npos || value.find('&') != std::string::npos ||
                              value.find('@') != std::string::npos))
        reject();
      if (value_lower.find("url") != std::string::npos) {
        static const std::regex safe_url(R"(url\(\s*['"]?#[A-Za-z0-9_.:-]+['"]?\s*\))", std::regex::icase);
        auto scrubbed = std::regex_replace(value_lower, safe_url, "");
        if (scrubbed.find("url") != std::string::npos)
          reject();
      }
      if ((attr == "fill" || attr == "stroke" || attr == "filter" || attr == "clip-path" || attr == "mask" ||
           attr == "cursor") &&
          (value.find('&') != std::string::npos || value.find('\\') != std::string::npos))
        reject();
    }
    if (!selfclosing)
      stack.push_back(tag);
    else if (stack.empty())
      closed_root = true;
  }
  if (!saw_root || !closed_root || !stack.empty())
    reject();
}
Json diagrams(const Json &in) {
  if (!in.is_array() || in.size() > 100)
    throw Error(400, "diagrams must be an array of at most 100 diagrams");
  Json out = Json::array();
  int i = 0;
  for (const auto &d : in) {
    if (!d.is_object())
      throw Error(400, "invalid diagram");
    auto svg = required(d, "svg", 2000000);
    validate_svg(svg);
    if (d.contains("caption") && !d["caption"].is_null() && !d["caption"].is_string())
      throw Error(400, "caption must be a string or null");
    out.push_back(
        {{"id", i + 1}, {"sort_order", i}, {"svg", svg}, {"caption", d.value("caption", Json(nullptr))}});
    ++i;
  }
  return out;
}
void nullable_string(Json &out, const Json &body, const char *key) {
  if (!body.contains(key))
    return;
  if (!body[key].is_null() && !body[key].is_string())
    throw Error(400, std::string(key) + " must be a string or null");
  if (body[key].is_string() && body[key].get_ref<const std::string &>().size() > 1000000)
    throw Error(400, std::string(key) + " is too long");
  out[key] = body[key];
}
Json write_problem(Store &s, const Json &body, int64_t id = 0) {
  s.writable();
  if (!body.is_object())
    throw Error(400, "expected an object");
  Json p = id ? s.get("problem", id)
              : Json{{"source", nullptr},     {"difficulty", nullptr},     {"mistake_note", nullptr},
                     {"tags", Json::array()}, {"diagrams", Json::array()}, {"solution", Json::object()}};
  bool changed = !id;
  for (const auto &k : {"title", "stem_md", "subject", "tags", "diagrams", "approach_md", "answer_md",
                        "source", "difficulty", "mistake_note"})
    if (body.contains(k))
      changed = true;
  if (!changed)
    return problem_detail(s, id);
  validate_audit(body);
  for (const auto &k : {"title", "stem_md"})
    if (!id || body.contains(k))
      p[k] = required(body, k, k == std::string("title") ? 10000 : 2000000);
  if (!id || body.contains("subject"))
    p["subject"] = require_subject(s, required(body, "subject", 256));
  for (const auto &k : {"approach_md", "answer_md"})
    if (!id || body.contains(k))
      p["solution"][k] = required(body, k, 2000000);
  for (const auto &k : {"source", "difficulty", "mistake_note"})
    nullable_string(p, body, k);
  if (body.contains("diagrams"))
    p["diagrams"] = diagrams(body["diagrams"]);
  Json resolutions;
  if (!id || body.contains("tags") || body.contains("subject")) {
    auto requested = body.contains("tags") ? names(body, "tags") : p["tags"];
    resolutions = resolve_tags(s, text(p, "subject"), requested);
    p["tags"] = canonical_names(resolutions);
    p["tag_resolution"] = resolutions;
  }
  p = s.save("problem", p, id);
  id = number(p, "id");
  s.save("problem_log", {{"problem_id", id},
                         {"editor_tool", text(body, "editor_tool")},
                         {"change_summary", text(body, "change_summary")},
                         {"changed_at", text(body, "changed_at", now())}});
  auto related = entity_related(s, "problem", id, true);
  return problem_detail(s, id, resolutions, related);
}
struct Filters {
  std::string sql = "r.owner_id=?";
  Params params;
};
Filters filters(Store &s, const std::string &subject, const std::string &tag, std::optional<int64_t> tag_id,
                const std::string &target = "problems") {
  Filters f;
  f.params = {owner(s)};
  bool has_tag = !tag.empty() || tag_id.has_value();
  f.sql += (target == "problems" || has_tag)
               ? " AND r.type='problem'"
               : (target == "notes" ? " AND r.type='note'" : " AND r.type IN ('problem','note')");
  if (!subject.empty()) {
    f.sql += " AND r.subject=?";
    f.params.push_back(normalize(subject));
  }
  if (tag_id) {
    auto t = s.get("tag", *tag_id);
    f.sql += " AND r.subject=? AND EXISTS (SELECT 1 FROM json_each(r.data,'$.tags') j WHERE j.value=?)";
    f.params.push_back(text(t, "subject"));
    f.params.push_back(text(t, "name"));
  } else if (!tag.empty()) {
    // Accept canonical names and aliases, preserving each tag's subject.
    f.sql += " AND EXISTS (SELECT 1 FROM records t JOIN json_each(r.data,'$.tags') j ON j.value=t.name WHERE "
             "t.owner_id=r.owner_id AND t.type='tag' AND t.subject=r.subject AND (t.name=? OR EXISTS (SELECT "
             "1 FROM json_each(t.data,'$.aliases') a WHERE a.value=?)))";
    f.params.push_back(normalize(tag));
    f.params.push_back(normalize(tag));
  }
  return f;
}
std::optional<int64_t> query_tag_id(const Query &q) {
  auto v = param(q, "tag_id");
  if (v.empty())
    return {};
  return parse_id(v);
}
Json list_problems(Store &s, const Query &q) {
  int limit = integer(q, "limit", 50, 1, 200),
      offset = integer(q, "offset", 0, 0, std::numeric_limits<int>::max());
  auto f = filters(s, param(q, "subject"), param(q, "tag"), query_tag_id(q));
  auto count = s.db.one("SELECT count(*) AS n FROM records r WHERE " + f.sql, f.params);
  auto params = f.params;
  params.push_back(std::to_string(limit));
  params.push_back(std::to_string(offset));
  auto rows = s.db.query("SELECT r.id,r.data,r.created_at,r.updated_at FROM records r WHERE " + f.sql +
                             " ORDER BY r.created_at DESC,r.id DESC LIMIT ? OFFSET ?",
                         params);
  Json items = Json::array();
  for (const auto &row : rows)
    items.push_back(summary(hydrate(row)));
  return {{"items", items}, {"total", count.at("n")}, {"limit", limit}, {"offset", offset}};
}
Json merge_tags(Store &s, const Json &body) {
  s.writable();
  auto from_id = number(body, "source_tag_id"), to_id = number(body, "target_tag_id");
  if (from_id <= 0 || to_id <= 0 || from_id == to_id)
    throw Error(400, "source_tag_id and target_tag_id must be different positive IDs");
  auto from = s.get("tag", from_id), to = s.get("tag", to_id);
  if (from["subject"] != to["subject"])
    throw Error(400, "不能合并不同科目的标签");
  Json alias_body = {{"aliases", to.value("aliases", Json::array())}};
  alias_body["aliases"].push_back(from["name"]);
  for (const auto &a : from.value("aliases", Json::array()))
    alias_body["aliases"].push_back(a);
  to["aliases"] = names(alias_body, "aliases");
  to = s.save("tag", to, to_id);
  auto rows = s.db.query(
      "SELECT id,data,created_at,updated_at FROM records WHERE owner_id=? AND type='problem' AND subject=? "
      "AND EXISTS (SELECT 1 FROM json_each(records.data,'$.tags') WHERE value=? OR value=?)",
      {owner(s), text(from, "subject"), text(from, "name"), text(to, "name")});
  for (const auto &row : rows) {
    auto p = hydrate(row);
    Json ns = Json::array();
    std::set<std::string> seen;
    for (const auto &n : p["tags"]) {
      auto name = n == from["name"] ? text(to, "name") : n.get<std::string>();
      if (seen.insert(name).second)
        ns.push_back(name);
    }
    p["tags"] = ns;
    for (auto &r : p["tag_resolution"])
      if (number(r, "tag_id") == from_id) {
        r["tag_id"] = to_id;
        r["name"] = to["name"];
        r["via"] = "alias";
      }
    auto id = number(p, "id");
    s.save("problem", p, id);
    s.save("problem_log",
           {{"problem_id", id},
            {"editor_tool", text(body, "editor_tool", "web")},
            {"change_summary", "合并标签：“" + text(from, "name") + "” → “" + text(to, "name") + "”"},
            {"changed_at", now()}});
  }
  // Print-book filters must keep selecting the merged tag.
  for (const auto &row : s.db.query(
           "SELECT id,data,created_at,updated_at FROM records WHERE owner_id=? AND type='print_book' AND "
           "EXISTS (SELECT 1 FROM json_each(records.data,'$.tag_ids') WHERE value=CAST(? AS INTEGER))",
           {owner(s), std::to_string(from_id)})) {
    auto book = hydrate(row);
    std::set<int64_t> unique;
    Json ids = Json::array();
    for (const auto &n : book["tag_ids"]) {
      int64_t v = n.get<int64_t>();
      if (v == from_id)
        v = to_id;
      if (unique.insert(v).second)
        ids.push_back(v);
    }
    book["tag_ids"] = ids;
    s.save("print_book", book, number(book, "id"));
  }
  s.erase("tag", from_id);
  return {{"target", tag_ref(to)}, {"absorbed_name", from["name"]}, {"reindexed_problems", rows.size()}};
}
std::vector<std::string> split_chunks(const std::string &value, size_t max_chars = 800) {
  auto chars = characters(value);
  std::vector<std::string> out;
  std::string chunk;
  size_t length = 0;
  for (size_t i = 0; i < chars.size(); ++i) {
    chunk += chars[i];
    ++length;
    if (length >= max_chars ||
        (length >= 200 && chars[i] == "\n" && i + 1 < chars.size() && chars[i + 1] == "\n")) {
      if (!normalize(chunk).empty())
        out.push_back(normalize(chunk));
      chunk.clear();
      length = 0;
    }
  }
  if (!normalize(chunk).empty())
    out.push_back(normalize(chunk));
  return out;
}
struct Chunk {
  int64_t id = 0;
  Json hit;
  double score = 0;
};
std::vector<Chunk> chunks(const Json &p, const std::string &kind, const Json &all_tags) {
  std::vector<Chunk> out;
  int64_t index = 0, id = number(p, "id");
  Json entity = {
      {"kind", kind}, {"id", id}, {"title", p.at("title")}, {"subject", p.value("subject", Json(nullptr))}};
  if (kind == "note")
    entity["body_md"] = text(p, "body_md");
  else {
    entity["source"] = p.value("source", Json(nullptr));
    entity["difficulty"] = p.value("difficulty", Json(nullptr));
    entity["tags"] = p.value("tags", Json::array());
    entity["stem_md"] = text(p, "stem_md");
  }
  auto add = [&](const std::string &text_value, const std::string &chunk_kind, Json tag_id, Json tag_name) {
    ++index;
    int64_t cid = id * 100000 + index;
    out.push_back({cid,
                   {{"chunk_id", cid},
                    {"kind", chunk_kind},
                    {"text", text_value},
                    {"score", 0},
                    {"tag_id", tag_id},
                    {"tag_name", tag_name},
                    {"entity", entity}},
                   0});
  };
  if (kind == "note") {
    for (const auto &body : split_chunks(text(p, "body_md")))
      add(body, "note", nullptr, nullptr);
    if (out.empty())
      add(text(p, "title"), "note", nullptr, nullptr);
  } else {
    auto stem_chars = characters(text(p, "stem_md"));
    std::string stem;
    for (size_t i = 0; i < std::min<size_t>(stem_chars.size(), 240); ++i)
      stem += stem_chars[i];
    for (const auto &name : p.value("tags", Json::array())) {
      Json tid = nullptr;
      for (const auto &t : all_tags)
        if (t["subject"] == p["subject"] && t["name"] == name) {
          tid = t["id"];
          break;
        }
      add(text(p, "subject") + " " + name.get<std::string>() + " " + stem, "knowledge_point", tid, name);
    }
    auto sol = p.value("solution", Json::object());
    for (const auto &approach : split_chunks(text(sol, "approach_md")))
      add(approach, "approach", nullptr, nullptr);
    // Include the stem and answer so searchable persisted content has a matching hit.
    for (const auto &value : split_chunks(text(p, "stem_md")))
      add(value, "knowledge_point", nullptr, nullptr);
    for (const auto &value : split_chunks(text(sol, "answer_md")))
      add(value, "approach", nullptr, nullptr);
    if (out.empty())
      add(text(p, "title"), "knowledge_point", nullptr, nullptr);
  }
  return out;
}
std::vector<std::string> query_terms(const std::string &q) {
  std::istringstream in(lower(q));
  std::vector<std::string> out;
  std::string term;
  while (in >> term) {
    out.push_back(term);
    if (out.size() == 24)
      break;
  }
  return out;
}
void trim_ranked(std::vector<Chunk> &list, size_t keep) {
  auto cmp = [](const Chunk &a, const Chunk &b) {
    return a.score == b.score ? a.id < b.id : a.score > b.score;
  };
  if (list.size() > keep) {
    std::nth_element(list.begin(), list.begin() + keep, list.end(), cmp);
    list.resize(keep);
  }
  std::sort(list.begin(), list.end(), cmp);
}
std::string fts_query(const std::vector<std::string> &terms) {
  std::string out;
  for (const auto &term : terms) {
    if (characters(term).size() < 3)
      continue;
    if (!out.empty())
      out += " OR ";
    out += '"';
    for (char c : term) {
      out += c;
      if (c == '"')
        out += '"';
    }
    out += '"';
  }
  return out;
}
} // namespace

// Deterministic local feature hashing. This offers lexical similarity without
// requiring a remote embedding API, and never exposes vectors in public JSON.
static std::vector<float> compute_embedding(const std::string &value) {
  std::vector<float> vec(256, 0);
  auto chars = characters(lower(normalize(value)));
  auto add = [&](const std::string &gram, float weight) {
    auto h = hash64(gram);
    vec[h % vec.size()] += (h & (1ULL << 32)) ? weight : -weight;
  };
  for (size_t i = 0; i < chars.size(); ++i) {
    if (chars[i].size() == 1 && std::isspace(static_cast<unsigned char>(chars[i][0])))
      continue;
    add(chars[i], .4f);
    if (i + 1 < chars.size())
      add(chars[i] + chars[i + 1], 1.0f);
    if (i + 2 < chars.size())
      add(chars[i] + chars[i + 1] + chars[i + 2], 1.0f);
  }
  std::istringstream words(lower(value));
  std::string word;
  while (words >> word)
    if (word.size() > 2 && std::all_of(word.begin(), word.end(), [](unsigned char c) { return c < 128; }))
      add("word:" + word, 2.0f);
  double norm = 0;
  for (float v : vec)
    norm += v * v;
  if (norm > 0) {
    norm = std::sqrt(norm);
    for (auto &v : vec)
      v = static_cast<float>(v / norm);
  }
  return vec;
}
namespace {
struct EmbeddingCacheEntry {
  std::vector<float> vector;
  std::list<std::string>::iterator position;
  size_t bytes;
};
struct EmbeddingCache {
  std::mutex mutex;
  std::unordered_map<std::string, EmbeddingCacheEntry> entries;
  std::list<std::string> recent;
  size_t bytes = 0, hits = 0, misses = 0, evictions = 0, bypassed = 0;
  static constexpr size_t budget = 32 * 1024 * 1024, max_entries = 8192, max_key_bytes = 64 * 1024;
};
EmbeddingCache &embedding_cache() {
  static EmbeddingCache cache;
  return cache;
}
} // namespace
std::vector<float> hash_embedding(const std::string &value) {
  auto &cache = embedding_cache();
  // Do not copy unusually large note bodies into either cache key container.
  if (value.size() > EmbeddingCache::max_key_bytes) {
    {
      std::lock_guard lock(cache.mutex);
      ++cache.bypassed;
    }
    return compute_embedding(value);
  }
  {
    std::lock_guard lock(cache.mutex);
    auto found = cache.entries.find(value);
    if (found != cache.entries.end()) {
      ++cache.hits;
      cache.recent.splice(cache.recent.begin(), cache.recent, found->second.position);
      return found->second.vector;
    }
    ++cache.misses;
  }
  // Tokenization is the expensive part: allow unrelated callers to compute
  // outside the mutex, then handle concurrent insertion of the same text.
  auto result = compute_embedding(value);
  size_t bytes = 2 * (value.size() + 1) + result.capacity() * sizeof(float) + 256;
  std::lock_guard lock(cache.mutex);
  auto found = cache.entries.find(value);
  if (found != cache.entries.end()) {
    cache.recent.splice(cache.recent.begin(), cache.recent, found->second.position);
    return found->second.vector;
  }
  while (!cache.recent.empty() && (cache.bytes + bytes > EmbeddingCache::budget ||
                                   cache.entries.size() >= EmbeddingCache::max_entries)) {
    auto it = cache.entries.find(cache.recent.back());
    cache.bytes -= it->second.bytes;
    cache.entries.erase(it);
    cache.recent.pop_back();
    ++cache.evictions;
  }
  cache.recent.push_front(value);
  try {
    cache.entries.emplace(value, EmbeddingCacheEntry{result, cache.recent.begin(), bytes});
  } catch (...) {
    cache.recent.pop_front();
    throw;
  }
  cache.bytes += bytes;
  return result;
}
Json embedding_cache_stats() {
  auto &cache = embedding_cache();
  std::lock_guard lock(cache.mutex);
  return {{"entries", cache.entries.size()},
          {"estimated_bytes", cache.bytes},
          {"hits", cache.hits},
          {"misses", cache.misses},
          {"evictions", cache.evictions},
          {"bypassed", cache.bypassed},
          {"max_bytes", EmbeddingCache::budget},
          {"max_entries", EmbeddingCache::max_entries},
          {"max_key_bytes", EmbeddingCache::max_key_bytes}};
}
void clear_embedding_cache() {
  auto &cache = embedding_cache();
  std::lock_guard lock(cache.mutex);
  cache.entries.clear();
  cache.recent.clear();
  cache.bytes = cache.hits = cache.misses = cache.evictions = cache.bypassed = 0;
}
double text_similarity(const std::string &left, const std::string &right) {
  return dot(hash_embedding(left), hash_embedding(right));
}

Json content_search(Store &s, const Json &body) {
  auto query = normalize(required(body, "query", 2000)), mode = text(body, "mode", "hybrid"),
       target = text(body, "target", "all");
  if (mode != "hybrid" && mode != "keyword" && mode != "rag")
    throw Error(400, "mode must be hybrid, keyword or rag");
  if (target != "all" && target != "problems" && target != "notes")
    throw Error(400, "target must be all, problems or notes");
  int limit = integer(body, "limit", 20, 1, 100);
  std::optional<int64_t> tag_id;
  if (body.contains("tag_id") && !body["tag_id"].is_null()) {
    if (!body["tag_id"].is_number_integer() || number(body, "tag_id") <= 0)
      throw Error(400, "invalid tag_id");
    tag_id = number(body, "tag_id");
  }
  auto tag = text(body, "tag");
  if (target == "notes" && (!tag.empty() || tag_id))
    throw Error(400, "tag filters are not valid when target is notes");
  auto f = filters(s, text(body, "subject"), tag, tag_id, target);
  auto terms = query_terms(query);
  auto all_tags = tags(s);
  auto query_vector = hash_embedding(query);
  size_t keep = std::max(200, limit * 3);
  std::vector<Chunk> keyword_ranked, vector_ranked;
  if (mode != "rag") {
    auto match = fts_query(terms);
    bool short_term =
        std::any_of(terms.begin(), terms.end(), [](const auto &t) { return characters(t).size() < 3; });
    std::string sql;
    auto ps = f.params;
    if (!match.empty() && !short_term) {
      sql = "SELECT r.id,r.data,r.created_at,r.updated_at,r.type,bm25(records_fts) AS rank FROM records_fts "
            "JOIN records r ON r.id=records_fts.rowid WHERE " +
            f.sql + " AND records_fts MATCH ? ORDER BY rank LIMIT ?";
      ps.push_back(match);
    } else {
      sql = "SELECT r.id,r.data,r.created_at,r.updated_at,r.type FROM records r WHERE " + f.sql + " AND (";
      bool first = true;
      for (const auto &t : terms) {
        if (!first)
          sql += " OR ";
        first = false;
        sql += "instr(lower(coalesce(r.title,'')||' '||coalesce(json_extract(r.data,'$.stem_md'),'')||' "
               "'||coalesce(json_extract(r.data,'$.solution.approach_md'),'')||' "
               "'||coalesce(json_extract(r.data,'$.solution.answer_md'),'')||' "
               "'||coalesce(json_extract(r.data,'$.body_md'),'')||' "
               "'||coalesce(json_extract(r.data,'$.tags'),'')||' "
               "'||coalesce(json_extract(r.data,'$.mistake_note'),'')),?)>0";
        ps.push_back(t);
      }
      sql += ") ORDER BY r.updated_at DESC,r.id DESC LIMIT ?";
    }
    ps.push_back(std::to_string(keep));
    int entity_rank = 0;
    for (const auto &row : s.db.query(sql, ps)) {
      auto p = hydrate(row);
      auto items = chunks(p, text(row, "type"), all_tags);
      bool added = false;
      for (auto &c : items) {
        auto value = lower(text(c.hit, "text")), title = lower(text(p, "title"));
        double score = 0;
        for (const auto &term : terms) {
          if (value.find(term) != std::string::npos)
            score += 2;
          if (title.find(term) != std::string::npos)
            score += 1;
        }
        if (score <= 0)
          continue;
        c.score = score + 1.0 / (entity_rank + 1);
        keyword_ranked.push_back(std::move(c));
        added = true;
      }
      if (!added && !items.empty()) {
        auto c = std::move(items.front());
        c.score = .1 + 1.0 / (entity_rank + 1);
        keyword_ranked.push_back(std::move(c));
      }
      ++entity_rank;
    }
    trim_ranked(keyword_ranked, keep);
  }
  if (mode != "keyword") {
    // Read indexed tenant/filter ranges in bounded batches and retain only
    // the best vectors; neither unrelated tenants nor entire libraries are
    // loaded into memory. Exact local cosine ranking has linear scan cost.
    int64_t cursor = 0;
    for (;;) {
      auto ps = f.params;
      ps.push_back(std::to_string(cursor));
      auto rows = s.db.query("SELECT r.id,r.data,r.created_at,r.updated_at,r.type FROM records r WHERE " +
                                 f.sql + " AND r.id>? ORDER BY r.id LIMIT 128",
                             ps);
      if (rows.empty())
        break;
      for (const auto &row : rows) {
        cursor = number(row, "id");
        auto p = hydrate(row);
        for (auto &c : chunks(p, text(row, "type"), all_tags)) {
          c.score = dot(query_vector, hash_embedding(text(p, "title") + " " + text(c.hit, "text")));
          if (c.score > 0)
            vector_ranked.push_back(std::move(c));
        }
      }
      trim_ranked(vector_ranked, keep);
    }
  }
  std::vector<Chunk> ranked;
  if (mode == "keyword") {
    ranked = std::move(keyword_ranked);
    for (size_t i = 0; i < ranked.size(); ++i)
      ranked[i].score = 1.0 / (61 + i);
  } else if (mode == "rag")
    ranked = std::move(vector_ranked);
  else {
    std::map<int64_t, Chunk> combined;
    for (const auto *list : {&keyword_ranked, &vector_ranked})
      for (size_t i = 0; i < list->size(); ++i) {
        const auto &hit = (*list)[i];
        if (!combined.count(hit.id)) {
          combined[hit.id] = hit;
          combined[hit.id].score = 0;
        }
        combined[hit.id].score += 1.0 / (61 + i);
      }
    for (auto &[id, c] : combined)
      ranked.push_back(std::move(c));
  }
  trim_ranked(ranked, limit);
  Json hits = Json::array();
  for (auto &c : ranked) {
    c.hit["score"] = c.score;
    hits.push_back(std::move(c.hit));
  }
  return {{"query", query}, {"mode", mode}, {"hits", hits}};
}

std::optional<Json> content_route(Store &s, const std::string &method, const std::string &path,
                                  const Json &body, const Query &q) {
  const std::string base = "/api/v1/";
  if (path == base + "search") {
    if (method == "POST")
      return content_search(s, body);
    if (method == "GET") {
      Json in = {{"query", param(q, "q")},
                 {"mode", param(q, "mode", "hybrid")},
                 {"target", param(q, "target", "all")},
                 {"limit", integer(q, "limit", 20, 1, 100)}};
      for (const auto &key : {"subject", "tag"})
        if (q.count(key))
          in[key] = param(q, key);
      if (auto tid = query_tag_id(q))
        in["tag_id"] = *tid;
      return content_search(s, in);
    }
    return {};
  }
  if (path == base + "taxonomy" && method == "GET")
    return Json{{"subjects", subjects(s)}, {"tags", tags(s)}};
  if (path == base + "subjects") {
    if (method == "GET") {
      Json out = Json::array();
      for (const auto &n : subjects(s))
        out.push_back({{"name", n}});
      return Json{{"items", out}};
    }
    if (method == "POST") {
      s.writable();
      auto name = normalize(required(body, "name", 256));
      if (!s.db.one("SELECT id FROM records WHERE owner_id=? AND type='subject' AND name=?", {owner(s), name})
               .empty())
        throw Error(409, "科目已存在");
      s.save("subject", {{"name", name}});
      return Json{{"name", name}};
    }
    return {};
  }
  if (path.rfind(base + "subjects/", 0) == 0 && method == "DELETE") {
    s.writable();
    auto name = normalize(path.substr((base + "subjects/").size()));
    auto row =
        s.db.one("SELECT id FROM records WHERE owner_id=? AND type='subject' AND name=?", {owner(s), name});
    if (row.empty())
      throw Error(404, "科目不存在");
    if (!s.db.one("SELECT id FROM records WHERE owner_id=? AND type IN ('problem','tag','note') AND "
                  "subject=? LIMIT 1",
                  {owner(s), name})
             .empty())
      throw Error(409, "科目仍有错题、标签或笔记，无法删除");
    s.erase("subject", number(row, "id"));
    return Json{{"ok", true}};
  }
  if (path == base + "tags" && method == "GET")
    return Json{{"items", tags(s, param(q, "subject"))}};
  if (path == base + "tags/suggest" && method == "POST") {
    auto subject = require_subject(s, required(body, "subject", 256));
    auto inputs = names(body, "names");
    if (body.contains("name")) {
      Json temp = {{"names", inputs}};
      temp["names"].push_back(required(body, "name", 256));
      inputs = names(temp, "names");
    }
    int limit = integer(body, "limit", 5, 1, 20);
    auto existing = tags(s, subject);
    Json result = Json::array();
    for (const auto &n : inputs) {
      std::vector<Json> matches;
      for (const auto &t : existing) {
        auto name = n.get<std::string>();
        std::string via = "embedding";
        double score = text_similarity(name, text(t, "name"));
        if (t["name"] == n) {
          via = "exact";
          score = 1;
        } else if (std::find(t["aliases"].begin(), t["aliases"].end(), n) != t["aliases"].end()) {
          via = "alias";
          score = 1;
        }
        matches.push_back({{"tag_id", t["id"]}, {"name", t["name"]}, {"score", score}, {"via", via}});
      }
      std::sort(matches.begin(), matches.end(), [](const Json &a, const Json &b) {
        return a["score"].get<double>() > b["score"].get<double>();
      });
      if (matches.size() > static_cast<size_t>(limit))
        matches.resize(limit);
      result.push_back({{"input", n}, {"matches", matches}});
    }
    return Json{{"suggestions", result}};
  }
  if (path == base + "tags/duplicates" && method == "GET") {
    double threshold = merge_threshold();
    if (q.count("threshold")) {
      try {
        size_t n = 0;
        auto value = param(q, "threshold");
        threshold = std::stod(value, &n);
        if (n != value.size())
          throw Error(400, "invalid threshold");
      } catch (...) {
        throw Error(400, "invalid threshold");
      }
    }
    if (!std::isfinite(threshold) || threshold < 0 || threshold > 1)
      throw Error(400, "threshold must be between 0 and 1");
    auto existing = tags(s, param(q, "subject"));
    Json pairs = Json::array();
    for (size_t i = 0; i < existing.size(); ++i)
      for (size_t j = i + 1; j < existing.size(); ++j)
        if (existing[i]["subject"] == existing[j]["subject"]) {
          double score = text_similarity(text(existing[i], "name"), text(existing[j], "name"));
          if (score >= threshold)
            pairs.push_back({{"tag_a", existing[i]}, {"tag_b", existing[j]}, {"score", score}});
        }
    std::sort(pairs.begin(), pairs.end(), [](const Json &a, const Json &b) {
      return a["score"].get<double>() > b["score"].get<double>();
    });
    return Json{{"threshold", threshold}, {"pairs", pairs}};
  }
  if (path == base + "tags/merge" && method == "POST")
    return merge_tags(s, body);
  if (path == base + "problems") {
    if (method == "GET")
      return list_problems(s, q);
    if (method == "POST")
      return write_problem(s, body);
    return {};
  }
  if (path.rfind(base + "problems/", 0) == 0) {
    auto rest = path.substr((base + "problems/").size());
    auto slash = rest.find('/');
    auto id = parse_id(rest.substr(0, slash));
    auto suffix = slash == std::string::npos ? "" : rest.substr(slash);
    if (suffix == "/logs" && method == "GET")
      return Json{{"items", logs(s, id)}};
    if (!suffix.empty())
      return {};
    if (method == "GET")
      return problem_detail(s, id);
    if (method == "PATCH")
      return write_problem(s, body, id);
    if (method == "DELETE") {
      s.writable();
      s.get("problem", id);
      s.db.query("DELETE FROM records WHERE owner_id=? AND type='link' AND "
                 "((json_extract(data,'$.left.kind')='problem' AND json_extract(data,'$.left.id')=CAST(? AS "
                 "INTEGER)) OR (json_extract(data,'$.right.kind')='problem' AND "
                 "json_extract(data,'$.right.id')=CAST(? AS INTEGER)))",
                 {owner(s), std::to_string(id), std::to_string(id)});
      s.db.query("DELETE FROM records WHERE owner_id=? AND type='problem_log' AND "
                 "json_extract(data,'$.problem_id')=CAST(? AS INTEGER)",
                 {owner(s), std::to_string(id)});
      s.erase("problem", id);
      return Json{{"ok", true}};
    }
  }
  return {};
}
} // namespace mb
