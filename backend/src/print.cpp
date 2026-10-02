#include "core.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace mb {
namespace {
Json hydrate(const Json &row) {
  auto out = Json::parse(row.at("data").get<std::string>());
  for (const auto key : {"id", "created_at", "updated_at"})
    out[key] = row.at(key);
  return out;
}
Json book_summary(Json book) {
  book.erase("pages");
  return book;
}
Json source_problems(Store &s, const Json &book) {
  std::set<std::string> subjects;
  for (const auto &subject : book.at("subjects"))
    subjects.insert(subject.get<std::string>());
  std::set<int64_t> tags;
  for (const auto &tag : book.at("tag_ids"))
    tags.insert(tag.get<int64_t>());
  Json out = Json::array();
  for (auto problem : s.list("problem")) {
    if (!subjects.empty() && !subjects.contains(text(problem, "subject")))
      continue;
    if (!tags.empty()) {
      bool matched = false;
      for (const auto &resolution : problem.value("tag_resolution", Json::array()))
        if (tags.contains(number(resolution, "tag_id"))) {
          matched = true;
          break;
        }
      if (!matched)
        continue;
    }
    problem["links"] = Json::array();
    problem["related"] = Json::array();
    problem["change_logs"] = Json::array();
    out.push_back(std::move(problem));
  }
  std::map<std::string, int> ranks;
  int rank = 0;
  for (const auto &subject : book.at("subjects"))
    ranks[subject.get<std::string>()] = rank++;
  std::sort(out.begin(), out.end(), [&](const Json &a, const Json &b) {
    auto as = text(a, "subject"), bs = text(b, "subject");
    if (as != bs) {
      if (!ranks.empty())
        return ranks.at(as) < ranks.at(bs);
      return as < bs;
    }
    return number(a, "id") < number(b, "id");
  });
  return out;
}
Json archived_pages(Store &s, int64_t book_id) {
  Json out = Json::array();
  for (const auto &row : s.db.query(
           "SELECT id,data,created_at,updated_at FROM records WHERE owner_id=? AND type='print_page' AND "
           "json_extract(data,'$.book_id')=CAST(? AS INTEGER) ORDER BY json_extract(data,'$.page_no')",
           {std::to_string(s.user.tenant()), std::to_string(book_id)}))
    out.push_back(hydrate(row));
  std::sort(out.begin(), out.end(),
            [](const Json &a, const Json &b) { return number(a, "page_no") < number(b, "page_no"); });
  return out;
}
std::string fingerprint(Json problem) {
  for (const auto key : {"created_at", "updated_at", "links", "related", "change_logs", "tag_resolution"})
    problem.erase(key);
  return problem.dump();
}
std::string page_fingerprint(const Json &page) {
  std::string out = text(page, "subject");
  for (const auto &problem : page.at("problems"))
    out += "\n" + fingerprint(problem);
  return out;
}
Json page_ids(const Json &page) {
  Json out = Json::array();
  for (const auto &problem : page.at("problems"))
    out.push_back(problem.at("id"));
  return out;
}
Json page_titles(const Json &page) {
  Json out = Json::array();
  for (const auto &problem : page.at("problems"))
    out.push_back(problem.at("title"));
  return out;
}
Json logical_pages(Store &s, const Json &book) {
  Json pages = Json::array();
  const auto problems = source_problems(s, book);
  std::string previous;
  auto blank = [&]() {
    pages.push_back(Json{{"page_no", pages.size() + 1}, {"subject", previous}, {"problems", Json::array()}});
  };
  for (const auto &problem : problems) {
    auto subject = text(problem, "subject");
    if (!pages.empty() && subject != previous && pages.size() % 2)
      blank();
    pages.push_back(
        Json{{"page_no", pages.size() + 1}, {"subject", subject}, {"problems", Json::array({problem})}});
    previous = subject;
  }
  if (pages.size() % 2)
    blank();
  return pages;
}
Json plan(Store &s, int64_t id, Json *out_pages = nullptr) {
  auto book = s.get("print_book", id);
  auto archived = archived_pages(s, id);
  auto pages = logical_pages(s, book);
  std::map<int64_t, Json> old;
  std::map<int64_t, int64_t> old_position, new_position;
  for (const auto &page : archived) {
    old[number(page, "page_no")] = page;
    for (const auto &pid : page_ids(page))
      old_position[pid.get<int64_t>()] = number(page, "page_no");
  }
  std::set<int64_t> dirty_sheets, new_sheets, dirty_pages, reprint, moved;
  for (auto &page : pages) {
    const auto pn = number(page, "page_no"), sheet = (pn + 1) / 2;
    bool dirty = !old.contains(pn) || page_fingerprint(page) != page_fingerprint(old.at(pn));
    if (dirty) {
      dirty_pages.insert(pn);
      if (pn > static_cast<int64_t>(archived.size()))
        new_sheets.insert(sheet);
      else
        dirty_sheets.insert(sheet);
    }
    for (const auto &pid : page_ids(page)) {
      auto p = pid.get<int64_t>();
      new_position[p] = pn;
      if (old_position.contains(p) && old_position.at(p) != pn)
        moved.insert(p);
    }
  }
  for (const auto &[pn, page] : old)
    if (pn > static_cast<int64_t>(pages.size()))
      dirty_sheets.insert((pn + 1) / 2);
  for (const auto &page : pages) {
    auto pn = number(page, "page_no");
    if (dirty_sheets.contains((pn + 1) / 2) || new_sheets.contains((pn + 1) / 2))
      reprint.insert(pn);
  }
  Json summaries = Json::array();
  for (const auto &page : pages)
    summaries.push_back(Json{{"page_no", page.at("page_no")},
                             {"subject", page.at("subject")},
                             {"problem_ids", page_ids(page)},
                             {"titles", page_titles(page)},
                             {"dirty", reprint.contains(number(page, "page_no"))},
                             {"blank", page.at("problems").empty()}});
  if (out_pages)
    *out_pages = pages;
  return Json{{"book", book_summary(book)},
              {"first_generation", archived.empty()},
              {"page_count", pages.size()},
              {"archived_page_count", archived.size()},
              {"dirty_sheets", dirty_sheets},
              {"new_sheets", new_sheets},
              {"dirty_page_nos", dirty_pages},
              {"reprint_page_nos", reprint},
              {"moved_problem_ids", moved},
              {"cost", reprint.size()},
              {"pages", summaries},
              {"pagination", "logical"}};
}
Json detail(Store &s, int64_t id) {
  auto book = s.get("print_book", id);
  auto archived = archived_pages(s, id);
  Json pages = Json::array();
  for (const auto &page : archived) {
    bool stale = false;
    Json titles = Json::array();
    for (const auto &problem : page.at("problems")) {
      try {
        auto current = s.get("problem", number(problem, "id"));
        titles.push_back(current.at("title"));
        if (fingerprint(current) != fingerprint(problem))
          stale = true;
      } catch (const Error &e) {
        if (e.status != 404)
          throw;
        titles.push_back(problem.at("title"));
        stale = true;
      }
    }
    pages.push_back(Json{{"page_no", page.at("page_no")},
                         {"subject", page.at("subject")},
                         {"problem_ids", page_ids(page)},
                         {"titles", titles},
                         {"stale", stale},
                         {"sheet", (number(page, "page_no") + 1) / 2}});
  }
  book["pages"] = pages;
  book["pagination"] = "logical";
  return book;
}
std::vector<std::string> path_parts(const std::string &path) {
  std::vector<std::string> v;
  std::stringstream ss(path);
  std::string p;
  while (std::getline(ss, p, '/'))
    if (!p.empty())
      v.push_back(p);
  return v;
}
std::set<int64_t> requested_pages(const Json &value, int64_t max) {
  if (!value.is_array() || value.empty())
    throw Error(400, "请选择需要重印的逻辑页");
  std::set<int64_t> pages;
  for (const auto &item : value) {
    if (!item.is_number_integer())
      throw Error(400, "页码必须是整数");
    auto pn = item.get<int64_t>();
    if (pn < 1 || pn > max)
      throw Error(400, "页码超出归档范围");
    pages.insert(pn);
  }
  return pages;
}
} // namespace

std::optional<Json> print_route(Store &s, const std::string &method, const std::string &path,
                                const Json &body, const Query &query) {
  if (path != "/api/v1/print/books" && !path.starts_with("/api/v1/print/books/"))
    return std::nullopt;
  const auto parts = path_parts(path);
  const bool read_reprint = method == "POST" && parts.size() == 6 && parts.at(5) == "reprint";
  if (method != "GET" && !read_reprint)
    s.writable();
  if (parts.size() == 4) {
    if (method == "GET") {
      auto books = s.list("print_book");
      std::sort(books.begin(), books.end(), [](const Json &a, const Json &b) {
        return text(a, "updated_at") != text(b, "updated_at") ? text(a, "updated_at") > text(b, "updated_at")
                                                              : number(a, "id") > number(b, "id");
      });
      Json out = Json::array();
      for (auto book : books)
        out.push_back(book_summary(book));
      return Json{{"items", out}};
    }
    if (method == "POST") {
      Json subjects = body.value("subjects", Json::array()), tags = body.value("tag_ids", Json::array());
      if (!subjects.is_array() || !tags.is_array())
        throw Error(400, "学科和标签必须是数组");
      std::set<std::string> known;
      for (const auto &subject : s.list("subject"))
        known.insert(text(subject, "name"));
      std::set<std::string> seen;
      Json unique_subjects = Json::array();
      for (const auto &subject : subjects) {
        if (!subject.is_string() || !known.contains(subject.get<std::string>()))
          throw Error(400, "指定的学科不存在");
        if (seen.insert(subject.get<std::string>()).second)
          unique_subjects.push_back(subject);
      }
      std::set<int64_t> unique_tags;
      for (const auto &tag : tags) {
        if (!tag.is_number_integer() || tag.get<int64_t>() <= 0)
          throw Error(400, "标签 ID 必须为正整数");
        s.get("tag", tag.get<int64_t>());
        unique_tags.insert(tag.get<int64_t>());
      }
      auto title = text(body, "title");
      if (title.empty()) {
        for (const auto &subject : unique_subjects) {
          if (!title.empty())
            title += "、";
          title += subject.get<std::string>();
        }
        if (title.empty())
          title = "全科";
      }
      if (title.size() > 1000)
        throw Error(400, "打印本标题过长");
      auto book = s.save(
          "print_book",
          Json{{"title", title}, {"subjects", unique_subjects}, {"tag_ids", unique_tags}, {"page_count", 0}});
      return detail(s, number(book, "id"));
    }
  }
  if (parts.size() < 5)
    throw Error(405, "不支持的打印操作");
  const auto id = parse_id(parts.at(4));
  auto book = s.get("print_book", id);
  if (parts.size() == 5) {
    if (method == "GET")
      return detail(s, id);
    if (method == "DELETE") {
      for (const auto &page : archived_pages(s, id))
        s.erase("print_page", number(page, "id"));
      s.erase("print_book", id);
      return Json{{"ok", true}};
    }
  }
  if (parts.size() == 6 && parts.at(5) == "plan" && method == "GET")
    return plan(s, id);
  if (parts.size() == 6 && parts.at(5) == "apply" && method == "POST") {
    Json pages;
    auto result = plan(s, id, &pages);
    if (pages.empty())
      throw Error(400, "没有符合筛选条件的题目");
    if (result.at("reprint_page_nos").empty() &&
        number(result, "archived_page_count") == static_cast<int64_t>(pages.size()))
      throw Error(409, "没有需要更新的逻辑页");
    auto old = archived_pages(s, id);
    std::map<int64_t, int64_t> existing;
    for (const auto &page : old)
      existing[number(page, "page_no")] = number(page, "id");
    for (auto page : pages) {
      const auto pn = number(page, "page_no");
      page["book_id"] = id;
      s.save("print_page", page, existing.contains(pn) ? existing.at(pn) : 0);
    }
    for (const auto &[pn, pid] : existing)
      if (pn > static_cast<int64_t>(pages.size()))
        s.erase("print_page", pid);
    book["page_count"] = pages.size();
    s.save("print_book", book, id);
    bool full =
        result.at("first_generation").get<bool>() || result.at("reprint_page_nos").size() == pages.size();
    auto print_pages = result.at("reprint_page_nos");
    if (print_pages.empty())
      for (const auto &page : pages)
        print_pages.push_back(page.at("page_no"));
    return Json{{"kind", full ? "full" : "patch"},
                {"pages", print_pages},
                {"book", detail(s, id)},
                {"pagination", "logical"}};
  }
  if (read_reprint) {
    auto pages = requested_pages(body.value("page_nos", Json::array()), number(book, "page_count"));
    auto archived = archived_pages(s, id);
    std::set<int64_t> available;
    for (const auto &page : archived)
      available.insert(number(page, "page_no"));
    for (auto pn : pages)
      if (!available.contains(pn))
        throw Error(404, "该逻辑页尚未归档");
    return Json{{"kind", "reprint"}, {"pages", pages}, {"pagination", "logical"}};
  }
  if (parts.size() == 6 && parts.at(5) == "document" && method == "GET") {
    auto archived = archived_pages(s, id);
    if (archived.empty())
      throw Error(404, "打印本尚未生成，请先归档打印内容");
    std::set<int64_t> selected;
    auto it = query.find("page_nos");
    if (it != query.end()) {
      if (it->second.empty())
        throw Error(400, "页码不能为空");
      Json requested = Json::array();
      std::stringstream ss(it->second);
      std::string value;
      while (std::getline(ss, value, ','))
        requested.push_back(parse_id(value));
      selected = requested_pages(requested, number(book, "page_count"));
    }
    Json pages = Json::array();
    for (auto page : archived) {
      auto pn = number(page, "page_no");
      if (!selected.empty() && !selected.contains(pn))
        continue;
      page.erase("id");
      page.erase("book_id");
      page.erase("created_at");
      page.erase("updated_at");
      page["sheet"] = (pn + 1) / 2;
      pages.push_back(std::move(page));
    }
    return Json{{"book", book_summary(book)}, {"pages", pages}, {"pagination", "logical"}};
  }
  throw Error(404, "打印接口不存在");
}
} // namespace mb
