#include "notes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <set>
#include <sstream>

namespace mb {
namespace {
Json hydrate(const Json &row) {
  auto result = Json::parse(row.at("data").get<std::string>());
  for (const auto key : {"id", "created_at", "updated_at"})
    result[key] = row.at(key);
  return result;
}
std::string owner(Store &s) {
  return std::to_string(s.user.tenant());
}
double link_threshold() {
  const char *raw = std::getenv("NOTE_LINK_THRESHOLD");
  if (!raw)
    return .8;
  try {
    double value = std::stod(raw);
    if (std::isfinite(value) && value >= 0 && value <= 1)
      return value;
  } catch (...) {
  }
  return .8;
}
Json ref(Store &s, const Json &input) {
  if (!input.is_object())
    throw Error(400, "关联端点必须是对象");
  const auto kind = required(input, "kind", 16);
  if (kind != "note" && kind != "problem")
    throw Error(400, "关联类型必须是 note 或 problem");
  const auto id = number(input, "id");
  if (id <= 0)
    throw Error(400, "关联 ID 必须为正整数");
  const auto row = s.get(kind, id);
  return Json{{"kind", kind},
              {"id", id},
              {"title", row.at("title")},
              {"subject", row.value("subject", Json(nullptr))}};
}
bool same(const Json &a, const Json &b) {
  return a.at("kind") == b.at("kind") && a.at("id") == b.at("id");
}
Json links(Store &s, const std::string &kind = "", int64_t id = 0) {
  Json out = Json::array();
  auto sql =
      std::string("SELECT id,data,created_at,updated_at FROM records WHERE owner_id=? AND type='link'");
  Params params{owner(s)};
  if (!kind.empty()) {
    sql += " AND ((json_extract(data,'$.left.kind')=? AND json_extract(data,'$.left.id')=CAST(? AS INTEGER)) "
           "OR (json_extract(data,'$.right.kind')=? AND json_extract(data,'$.right.id')=CAST(? AS INTEGER)))";
    params.insert(params.end(), {kind, std::to_string(id), kind, std::to_string(id)});
  }
  sql += " ORDER BY id";
  for (const auto &record : s.db.query(sql, params)) {
    auto row = hydrate(record);
    try {
      row["left"] = ref(s, row.at("left"));
      row["right"] = ref(s, row.at("right"));
      out.push_back(std::move(row));
    } catch (const Error &e) {
      if (e.status != 404)
        throw;
    }
  }
  std::sort(out.begin(), out.end(),
            [](const Json &a, const Json &b) { return number(a, "id") < number(b, "id"); });
  return out;
}
Json logs(Store &s, int64_t id, bool versions = false) {
  s.get("note", id);
  Json out = Json::array();
  for (const auto &row : s.db.query("SELECT id,data,created_at,updated_at FROM records WHERE owner_id=? AND "
                                    "type='note_log' AND json_extract(data,'$.note_id')=CAST(? AS INTEGER) "
                                    "ORDER BY json_extract(data,'$.changed_at') DESC,id DESC",
                                    {owner(s), std::to_string(id)})) {
    auto log = hydrate(row);
    Json v{{"id", log.at("id")},
           {"editor_tool", log.at("editor_tool")},
           {"change_summary", log.at("change_summary")},
           {"changed_at", log.at("changed_at")}};
    if (versions) {
      v["title"] = log.at("title");
      v["parent_id"] = log.at("parent_id");
      v["sort_order"] = log.at("sort_order");
      v["has_snapshot"] = true;
    }
    out.push_back(std::move(v));
  }
  std::sort(out.begin(), out.end(), [](const Json &a, const Json &b) {
    return text(a, "changed_at") != text(b, "changed_at") ? text(a, "changed_at") > text(b, "changed_at")
                                                          : number(a, "id") > number(b, "id");
  });
  return out;
}
void audit(Store &s, const Json &node, const Json &body) {
  validate_audit(body);
  s.save("note_log", Json{{"note_id", node.at("id")},
                          {"editor_tool", required(body, "editor_tool", 64)},
                          {"change_summary", required(body, "change_summary", 2000)},
                          {"changed_at", text(body, "changed_at", now())},
                          {"title", node.at("title")},
                          {"body_md", node.at("body_md")},
                          {"parent_id", node.at("parent_id")},
                          {"sort_order", node.at("sort_order")}});
}
Json detail(Store &s, int64_t id) {
  auto node = s.get("note", id);
  node["links"] = links(s, "note", id);
  node["change_logs"] = logs(s, id);
  return node;
}
Json summary(Json node) {
  node.erase("body_md");
  return node;
}
Json parent(Store &s, const Json &p, int64_t self = 0) {
  if (p.is_null())
    return nullptr;
  if (!p.is_number_integer() || p.get<int64_t>() <= 0)
    throw Error(400, "parent_id 必须是正整数或 null");
  int64_t id = p.get<int64_t>();
  std::set<int64_t> seen;
  while (id) {
    if (id == self || !seen.insert(id).second)
      throw Error(400, "目录不能形成循环");
    auto node = s.db.one("SELECT id,parent_id FROM records WHERE owner_id=? AND type='note' AND id=?",
                         {owner(s), std::to_string(id)});
    if (node.is_null())
      throw Error(404, "父笔记不存在");
    id = number(node, "parent_id");
    if (seen.size() > 256)
      throw Error(400, "目录层级不能超过 256 层");
  }
  return p;
}
int64_t next_order(Store &s, const Json &p) {
  auto row = s.db.one("SELECT coalesce(max(json_extract(data,'$.sort_order'))+1,0) AS next_order FROM "
                      "records WHERE owner_id=? AND type='note' AND parent_id IS ?",
                      {owner(s), p.is_null() ? std::optional<std::string>{}
                                             : std::optional<std::string>{std::to_string(p.get<int64_t>())}});
  return number(row, "next_order");
}
void check_subject(Store &s, const Json &value) {
  if (value.is_null())
    return;
  if (!value.is_string() || value.get<std::string>().empty())
    throw Error(400, "学科不能为空");
  if (s.db.one("SELECT id FROM records WHERE owner_id=? AND type='subject' AND name=?",
               {owner(s), value.get<std::string>()})
          .is_null())
    throw Error(400, "学科不存在，请先创建该学科");
}
void patch_fields(Store &s, Json &node, const Json &body, int64_t id = 0) {
  if (body.contains("title"))
    node["title"] = required(body, "title", 1000);
  if (body.contains("body_md")) {
    if (!body.at("body_md").is_string() || body.at("body_md").get_ref<const std::string &>().size() > 4000000)
      throw Error(400, "笔记正文必须是字符串且不超过 4 MB");
    node["body_md"] = body.at("body_md");
  }
  if (body.contains("subject")) {
    check_subject(s, body.at("subject"));
    node["subject"] = body.at("subject");
  }
  if (body.contains("parent_id"))
    node["parent_id"] = parent(s, body.at("parent_id"), id);
  if (body.contains("sort_order")) {
    if (!body.at("sort_order").is_number_integer())
      throw Error(400, "sort_order 必须是整数");
    node["sort_order"] = number(body, "sort_order");
  }
}
Json toc(Store &s) {
  auto rows = s.db.query("SELECT id,title,subject,parent_id,json_extract(data,'$.sort_order') AS "
                         "sort_order,created_at,updated_at FROM records WHERE owner_id=? AND type='note' "
                         "ORDER BY parent_id,sort_order,id",
                         {owner(s)});
  std::sort(rows.begin(), rows.end(), [](const Json &a, const Json &b) {
    return number(a, "sort_order") != number(b, "sort_order")
               ? number(a, "sort_order") < number(b, "sort_order")
               : number(a, "id") < number(b, "id");
  });
  std::map<int64_t, std::vector<Json>> children;
  for (auto row : rows)
    children[number(row, "parent_id")].push_back(summary(row));
  std::function<Json(int64_t, size_t)> walk = [&](int64_t id, size_t depth) {
    if (depth > 256)
      throw Error(409, "目录层级损坏");
    Json out = Json::array();
    for (auto node : children[id]) {
      node["children"] = walk(number(node, "id"), depth + 1);
      out.push_back(std::move(node));
    }
    return out;
  };
  return walk(0, 0);
}
Json write_response(Store &s, int64_t id, bool refresh) {
  auto related = entity_related(s, "note", id, refresh);
  auto node = detail(s, id);
  Json embedding = Json::array();
  for (auto link : node.at("links"))
    if (text(link, "via") == "embedding")
      embedding.push_back(std::move(link));
  return Json{{"node", node}, {"related", related}, {"embedding_links", embedding}};
}
Json version(Store &s, int64_t note_id, int64_t version_id) {
  s.get("note", note_id);
  auto row = s.get("note_log", version_id);
  if (number(row, "note_id") != note_id)
    throw Error(404, "笔记版本不存在");
  row.erase("note_id");
  row.erase("created_at");
  row.erase("updated_at");
  row["has_snapshot"] = true;
  return row;
}
Json snapshot(Json row, const std::string &source) {
  Json out{{"body_md", text(row, "body_md")},
           {"ref", Json{{"id", source == "log" ? row.at("id") : Json(nullptr)},
                        {"source", source},
                        {"editor_tool", source == "log" ? row.at("editor_tool") : Json(nullptr)},
                        {"change_summary", source == "log" ? row.at("change_summary") : Json(nullptr)},
                        {"changed_at", source == "log"
                                           ? row.at("changed_at")
                                           : (source == "current" ? row.at("updated_at") : Json(nullptr))},
                        {"title", text(row, "title")},
                        {"parent_id", row.value("parent_id", Json(nullptr))},
                        {"sort_order", number(row, "sort_order")}}}};
  return out;
}
std::vector<std::string> lines(const std::string &text) {
  std::vector<std::string> out;
  size_t start = 0;
  if (text.empty())
    return out;
  while (true) {
    auto end = text.find('\n', start);
    std::string line = text.substr(start, end == std::string::npos ? end : end - start);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    out.push_back(std::move(line));
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return out;
}
Json diff(const std::string &a, const std::string &b) {
  auto left = lines(a), right = lines(b);
  Json hunks = Json::array();
  auto emit = [&](const std::string &kind, const std::vector<std::string> &l,
                  const std::vector<std::string> &r) {
    if (l.empty() && r.empty())
      return;
    if (!hunks.empty() && text(hunks.back(), "kind") == kind) {
      for (auto &v : l)
        hunks.back()["left"].push_back(v);
      for (auto &v : r)
        hunks.back()["right"].push_back(v);
    } else
      hunks.push_back(Json{{"kind", kind}, {"left", l}, {"right", r}});
  };
  // Bound memory for unusually large documents while retaining a truthful diff.
  if (left.size() > 2000 || right.size() > 2000 || left.size() * right.size() > 2000000) {
    size_t p = 0, e = 0;
    while (p < left.size() && p < right.size() && left[p] == right[p])
      ++p;
    while (e < left.size() - p && e < right.size() - p &&
           left[left.size() - e - 1] == right[right.size() - e - 1])
      ++e;
    emit("equal", {left.begin(), left.begin() + p}, {right.begin(), right.begin() + p});
    auto l = std::vector<std::string>(left.begin() + p, left.end() - e),
         r = std::vector<std::string>(right.begin() + p, right.end() - e);
    emit(l.empty() ? "add" : r.empty() ? "remove" : "replace", l, r);
    emit("equal", {left.end() - e, left.end()}, {right.end() - e, right.end()});
    return hunks;
  }
  const auto width = right.size() + 1;
  std::vector<uint32_t> lcs((left.size() + 1) * width);
  for (size_t i = left.size(); i-- > 0;)
    for (size_t j = right.size(); j-- > 0;)
      lcs[i * width + j] = left[i] == right[j] ? 1 + lcs[(i + 1) * width + j + 1]
                                               : std::max(lcs[(i + 1) * width + j], lcs[i * width + j + 1]);
  size_t i = 0, j = 0;
  std::vector<std::string> removed, added;
  auto flush = [&]() {
    emit(removed.empty() ? "add" : added.empty() ? "remove" : "replace", removed, added);
    removed.clear();
    added.clear();
  };
  while (i < left.size() || j < right.size()) {
    if (i < left.size() && j < right.size() && left[i] == right[j]) {
      flush();
      emit("equal", {left[i++]}, {right[j++]});
    } else if (j == right.size() || (i < left.size() && lcs[(i + 1) * width + j] >= lcs[i * width + j + 1]))
      removed.push_back(left[i++]);
    else
      added.push_back(right[j++]);
  }
  flush();
  return hunks;
}
Json diff_result(int64_t id, const Json &from, const Json &to) {
  return Json{{"note_id", id},
              {"from", from.at("ref")},
              {"to", to.at("ref")},
              {"title_changed", from.at("ref").at("title") != to.at("ref").at("title")},
              {"parent_changed", from.at("ref").at("parent_id") != to.at("ref").at("parent_id")},
              {"sort_changed", from.at("ref").at("sort_order") != to.at("ref").at("sort_order")},
              {"hunks", diff(text(from, "body_md"), text(to, "body_md"))}};
}
std::vector<std::string> split_path(const std::string &path) {
  std::vector<std::string> v;
  std::stringstream ss(path);
  std::string part;
  while (std::getline(ss, part, '/'))
    if (!part.empty())
      v.push_back(part);
  return v;
}
std::string q(const Query &query, const std::string &key, const std::string &fallback = "") {
  auto it = query.find(key);
  return it == query.end() ? fallback : it->second;
}
} // namespace

Json entity_links(Store &s, const std::string &kind, int64_t id) {
  s.get(kind, id);
  return links(s, kind, id);
}

Json entity_related(Store &s, const std::string &kind, int64_t id, bool refresh) {
  const auto source = s.get(kind, id);
  auto searchable = [](const Json &row, const std::string &k) {
    std::string value =
        text(row, "title") + "\n" + (k == "note" ? text(row, "body_md") : text(row, "stem_md"));
    if (k == "problem" && row.contains("solution"))
      value += "\n" + text(row.at("solution"), "approach_md");
    return value;
  };
  const auto query_vector = hash_embedding(searchable(source, kind));
  auto current = links(s, kind, id);
  Json hits = Json::array();
  const auto subject = text(source, "subject");
  for (const auto &candidate_kind : {std::string("note"), std::string("problem")}) {
    int64_t cursor = 0;
    while (true) {
      std::string sql =
          "SELECT id,data,created_at,updated_at FROM records WHERE owner_id=? AND type=? AND id>?";
      Params params{owner(s), candidate_kind, std::to_string(cursor)};
      if (!subject.empty()) {
        sql += " AND (subject=? OR subject IS NULL)";
        params.push_back(subject);
      }
      sql += " ORDER BY id LIMIT 128";
      auto rows = s.db.query(sql, params);
      if (rows.empty())
        break;
      for (const auto &record : rows) {
        auto row = hydrate(record);
        cursor = number(row, "id");
        const auto candidate_id = number(row, "id");
        if (candidate_kind == kind && candidate_id == id)
          continue;
        auto vector = hash_embedding(searchable(row, candidate_kind));
        double score = 0;
        for (size_t i = 0; i < query_vector.size(); ++i)
          score += query_vector[i] * vector[i];
        if (score < 0.15)
          continue;
        Json candidate{{"kind", candidate_kind},
                       {"id", candidate_id},
                       {"title", row.at("title")},
                       {"subject", row.value("subject", Json(nullptr))}};
        bool linked = false;
        for (const auto &link : current)
          if (same(link.at("left"), candidate) || same(link.at("right"), candidate)) {
            linked = true;
            break;
          }
        hits.push_back(Json{{"entity", candidate}, {"score", score}, {"linked", linked}});
      }
      std::sort(hits.begin(), hits.end(), [](const Json &a, const Json &b) {
        return a.at("score").get<double>() > b.at("score").get<double>();
      });
      if (hits.size() > 12)
        hits.erase(hits.begin() + 12, hits.end());
    }
  }
  std::sort(hits.begin(), hits.end(), [](const Json &a, const Json &b) {
    return a.at("score").get<double>() > b.at("score").get<double>();
  });
  if (hits.size() > 12)
    hits.erase(hits.begin() + 12, hits.end());
  if (refresh) {
    s.writable();
    for (const auto &link : current)
      if (text(link, "via") == "embedding")
        s.erase("link", number(link, "id"));
    auto source_ref = ref(s, Json{{"kind", kind}, {"id", id}});
    for (auto &hit : hits) {
      const auto candidate = hit.at("entity");
      bool manually_linked = false;
      for (const auto &link : current)
        if (text(link, "via") == "manual" &&
            (same(link.at("left"), candidate) || same(link.at("right"), candidate))) {
          manually_linked = true;
          break;
        }
      if (hit.at("score").get<double>() >= link_threshold()) {
        auto left = source_ref, right = candidate;
        if (text(left, "kind") > text(right, "kind") ||
            (left.at("kind") == right.at("kind") && number(left, "id") > number(right, "id")))
          std::swap(left, right);
        s.save("link", Json{{"left", left},
                            {"right", right},
                            {"via", "embedding"},
                            {"score", hit.at("score")},
                            {"label", nullptr}});
        hit["linked"] = true;
      } else
        hit["linked"] = manually_linked;
    }
  }
  return hits;
}

std::optional<Json> notes_route(Store &s, const std::string &method, const std::string &path,
                                const Json &body, const Query &query) {
  if (path != "/api/v1/notes" && !path.starts_with("/api/v1/notes/"))
    return std::nullopt;
  if (method != "GET")
    s.writable();
  if (path == "/api/v1/notes/toc") {
    if (method == "GET")
      return Json{{"items", toc(s)}};
    if (method == "POST") {
      validate_audit(body);
      if (!body.contains("nodes") || !body.at("nodes").is_array())
        throw Error(400, "nodes 必须是数组");
      std::map<int64_t, Json> existing;
      for (auto node : s.db.query("SELECT id,parent_id,json_extract(data,'$.sort_order') AS sort_order FROM "
                                  "records WHERE owner_id=? AND type='note'",
                                  {owner(s)}))
        existing.emplace(number(node, "id"), node);
      std::set<int64_t> seen;
      std::vector<Json> updated;
      std::function<void(const Json &, Json, size_t)> walk = [&](const Json &nodes, Json pid, size_t depth) {
        if (depth > 256)
          throw Error(400, "目录层级不能超过 256 层");
        if (!nodes.is_array())
          throw Error(400, "children 必须是数组");
        int64_t order = 0;
        for (const auto &node : nodes) {
          auto id = number(node, "id");
          if (!existing.contains(id))
            throw Error(404, "目录包含不存在的笔记");
          if (!seen.insert(id).second)
            throw Error(400, "目录不能重复包含同一笔记");
          auto row = existing.at(id);
          if (row.at("parent_id") != pid || number(row, "sort_order") != order) {
            row = s.get("note", id);
            row["parent_id"] = pid;
            row["sort_order"] = order;
            updated.push_back(std::move(row));
          }
          ++order;
          walk(node.value("children", Json::array()), id, depth + 1);
        }
      };
      walk(body.at("nodes"), nullptr, 0);
      if (seen.size() != existing.size())
        throw Error(400, "目录必须包含所有笔记且每篇恰好一次");
      for (auto &row : updated) {
        auto saved = s.save("note", row, number(row, "id"));
        audit(s, saved, body);
      }
      return Json{{"items", toc(s)}};
    }
  }
  if (path == "/api/v1/notes/links") {
    if (method == "GET") {
      auto kind = q(query, "kind"), id = q(query, "id");
      if (!kind.empty() && kind != "note" && kind != "problem")
        throw Error(400, "未知关联类型");
      if (!kind.empty() && !id.empty()) {
        auto n = parse_id(id);
        s.get(kind, n);
        return Json{{"items", links(s, kind, n)}};
      }
      return Json{{"items", links(s)}};
    }
    if (method == "POST" || method == "DELETE") {
      if (!body.contains("from") || !body.contains("to"))
        throw Error(400, "from 和 to 必填");
      auto a = ref(s, body.at("from")), b = ref(s, body.at("to"));
      if (same(a, b))
        throw Error(400, "不能关联到自身");
      if (text(a, "kind") > text(b, "kind") ||
          (a.at("kind") == b.at("kind") && number(a, "id") > number(b, "id")))
        std::swap(a, b);
      auto via = method == "POST" ? "manual" : text(body, "via", "manual");
      if (via != "manual" && via != "embedding")
        throw Error(400, "未知关联来源");
      int64_t existing = 0;
      Json label = body.value("label", Json(nullptr));
      if (!label.is_null() && (!label.is_string() || label.get_ref<const std::string &>().size() > 2000))
        throw Error(400, "关联说明不能超过 2000 字节");
      for (const auto &link : s.list("link"))
        if (text(link, "via") == via && ((same(link.at("left"), a) && same(link.at("right"), b)) ||
                                         (same(link.at("left"), b) && same(link.at("right"), a)))) {
          existing = number(link, "id");
          if (label.is_null())
            label = link.value("label", Json(nullptr));
          break;
        }
      if (method == "DELETE") {
        if (existing)
          s.erase("link", existing);
        return Json{{"ok", true}};
      }
      return s.save("link",
                    Json{{"left", a}, {"right", b}, {"via", "manual"}, {"score", 1.0}, {"label", label}},
                    existing);
    }
  }
  if (path == "/api/v1/notes/graph" && method == "GET") {
    bool include = q(query, "problems") != "0" && q(query, "problems") != "false";
    Json nodes = Json::array(), edges = Json::array();
    std::set<int64_t> problems;
    for (const auto &row : s.db.query(
             "SELECT id,title,subject FROM records WHERE owner_id=? AND type='note' ORDER BY id", {owner(s)}))
      nodes.push_back(Json{{"kind", "note"},
                           {"id", row.at("id")},
                           {"title", row.at("title")},
                           {"subject", row.value("subject", Json(nullptr))}});
    for (const auto &link : links(s)) {
      const auto &a = link.at("left");
      const auto &b = link.at("right");
      if (!include && (text(a, "kind") == "problem" || text(b, "kind") == "problem"))
        continue;
      for (const auto &x : {a, b})
        if (text(x, "kind") == "problem")
          problems.insert(number(x, "id"));
      edges.push_back(Json{{"from", Json{{"kind", a.at("kind")}, {"id", a.at("id")}}},
                           {"to", Json{{"kind", b.at("kind")}, {"id", b.at("id")}}},
                           {"via", link.at("via")},
                           {"score", link.value("score", Json(nullptr))},
                           {"label", link.value("label", Json(nullptr))}});
    }
    for (auto id : problems)
      nodes.push_back(ref(s, Json{{"kind", "problem"}, {"id", id}}));
    return Json{{"nodes", nodes}, {"edges", edges}};
  }
  if (path == "/api/v1/notes" && method == "POST") {
    validate_audit(body);
    required(body, "title", 1000);
    Json node{
        {"title", ""}, {"body_md", ""}, {"subject", nullptr}, {"parent_id", nullptr}, {"sort_order", 0}};
    patch_fields(s, node, body);
    if (!body.contains("sort_order"))
      node["sort_order"] = next_order(s, node.at("parent_id"));
    node = s.save("note", node);
    audit(s, node, body);
    return write_response(s, number(node, "id"), true);
  }
  const auto parts = split_path(path);
  if (parts.size() < 4)
    throw Error(405, "不支持的笔记操作");
  auto id = parse_id(parts.at(3));
  if (parts.size() == 4) {
    if (method == "GET")
      return detail(s, id);
    if (method == "PATCH") {
      auto node = s.get("note", id);
      bool changed = false;
      for (auto key : {"title", "body_md", "subject", "parent_id", "sort_order"})
        changed |= body.contains(key);
      if (!changed)
        return write_response(s, id, false);
      validate_audit(body);
      patch_fields(s, node, body, id);
      node = s.save("note", node, id);
      audit(s, node, body);
      return write_response(s, id,
                            body.contains("body_md") || body.contains("title") || body.contains("subject"));
    }
    if (method == "DELETE") {
      s.get("note", id);
      if (!s.db.one("SELECT id FROM records WHERE owner_id=? AND type='note' AND parent_id=? LIMIT 1",
                    {owner(s), std::to_string(id)})
               .is_null())
        throw Error(409, "请先移动或删除子笔记");
      for (const auto &link : links(s, "note", id))
        s.erase("link", number(link, "id"));
      for (const auto &log : logs(s, id))
        s.erase("note_log", number(log, "id"));
      s.erase("note", id);
      return Json{{"ok", true}};
    }
  }
  if (parts.size() == 5 && parts.at(4) == "move" && method == "POST") {
    validate_audit(body);
    if (!body.contains("parent_id"))
      throw Error(400, "parent_id 必填");
    auto node = s.get("note", id);
    node["parent_id"] = parent(s, body.at("parent_id"), id);
    node["sort_order"] =
        body.contains("sort_order") ? number(body, "sort_order") : next_order(s, node.at("parent_id"));
    node = s.save("note", node, id);
    audit(s, node, body);
    return detail(s, id);
  }
  if (method == "GET" && parts.size() == 5 && parts.at(4) == "logs")
    return Json{{"items", logs(s, id)}};
  if (method == "GET" && parts.size() == 5 && parts.at(4) == "versions")
    return Json{{"items", logs(s, id, true)}};
  if (method == "GET" && parts.size() == 6 && parts.at(4) == "versions")
    return version(s, id, parse_id(parts.at(5)));
  if (method == "GET" && parts.size() == 5 && parts.at(4) == "diff") {
    auto from = snapshot(version(s, id, parse_id(q(query, "from"))), "log");
    auto to = q(query, "to") == "current" ? snapshot(s.get("note", id), "current")
                                          : snapshot(version(s, id, parse_id(q(query, "to"))), "log");
    return diff_result(id, from, to);
  }
  if (method == "GET" && parts.size() == 7 && parts.at(4) == "versions" && parts.at(6) == "diff") {
    auto vid = parse_id(parts.at(5));
    auto selected = version(s, id, vid);
    auto against = q(query, "against", "previous");
    Json from, to;
    if (against == "current") {
      from = snapshot(selected, "log");
      to = snapshot(s.get("note", id), "current");
    } else if (against == "previous" || against.empty()) {
      auto all = logs(s, id, true);
      from = snapshot(Json::object(), "empty");
      to = snapshot(selected, "log");
      for (size_t i = 0; i < all.size(); ++i)
        if (number(all[i], "id") == vid && i + 1 < all.size()) {
          from = snapshot(version(s, id, number(all[i + 1], "id")), "log");
          break;
        }
    } else {
      auto other = version(s, id, parse_id(against));
      bool newer = text(selected, "changed_at") != text(other, "changed_at")
                       ? text(selected, "changed_at") > text(other, "changed_at")
                       : number(selected, "id") > number(other, "id");
      from = snapshot(newer ? other : selected, "log");
      to = snapshot(newer ? selected : other, "log");
    }
    return diff_result(id, from, to);
  }
  throw Error(404, "笔记接口不存在");
}
} // namespace mb
