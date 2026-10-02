#include "core.hpp"
#include "notes.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace mb;
namespace {
int checked = 0;
void check(bool ok, const char *message) {
  ++checked;
  if (!ok)
    throw std::runtime_error(message);
}
Json request(Store &store, const std::string &method, const std::string &route,
             const Json &body = Json::object(), const Query &query = {}) {
  bool write = method != "GET";
  if (write)
    store.db.exec("BEGIN IMMEDIATE");
  try {
    auto response = content_route(store, method, "/api/v1/" + route, body, query);
    if (!response)
      throw std::runtime_error("unhandled route: " + route);
    if (write)
      store.db.exec("COMMIT");
    return *response;
  } catch (...) {
    if (write)
      store.db.exec("ROLLBACK");
    throw;
  }
}
void fails(int status, const std::function<void()> &fn, const char *message) {
  try {
    fn();
  } catch (const Error &error) {
    check(error.status == status, message);
    return;
  }
  throw std::runtime_error(message);
}
Json create_body(std::string title = "勾股定理练习", std::string tag = "勾股定理") {
  return {{"title", title},
          {"subject", "数学"},
          {"stem_md", "直角三角形的两条直角边分别是 3 和 4，求斜边。"},
          {"approach_md", "利用勾股定理：斜边平方等于两直角边平方之和。"},
          {"answer_md", "c² = 3² + 4² = 25，所以 c = 5。"},
          {"tags", Json::array({tag})},
          {"diagrams",
           Json::array(
               {Json{{"svg", "<svg xmlns=\"http://www.w3.org/2000/svg\"><path d=\"M0 0L3 0L0 4Z\"/></svg>"},
                     {"caption", "三角形"}}})},
          {"editor_tool", "test"},
          {"change_summary", "录入一道错题"}};
}
std::string pid(const Json &p) {
  return "problems/" + std::to_string(number(p, "id"));
}
} // namespace
int main() {
  try {
    Db db(":memory:");
    initialize(db);
    db.query("INSERT INTO users(id,username,password_hash,role,created_at) "
             "VALUES(1,'owner_a','test','user',?),(2,'owner_b','test','user',?)",
             {now(), now()});
    db.query("INSERT INTO users(id,username,password_hash,role,owner_id,created_at) "
             "VALUES(3,'reader','test','readonly',1,?)",
             {now()});
    Store a(db, User{1, 0, "owner_a", "user", now()}), b(db, User{2, 0, "owner_b", "user", now()}),
        reader(db, User{3, 1, "reader", "readonly", now()});
    for (auto *store : {&a, &b})
      request(*store, "POST", "subjects", {{"name", "数学"}});
    check(request(a, "GET", "subjects")["items"].size() == 1, "subject list");
    fails(409, [&] { request(a, "POST", "subjects", {{"name", "数学"}}); }, "duplicate subject");
    auto p = request(a, "POST", "problems", create_body());
    auto id = number(p, "id"), tid = number(p["tag_resolution"][0], "tag_id");
    check(p["solution"]["answer_md"] == create_body()["answer_md"], "solution persistence");
    check(p["diagrams"].size() == 1 && p["diagrams"][0]["sort_order"] == 0, "diagram persistence");
    check(p["tag_resolution"][0]["via"] == "created", "tag creation resolution");
    check(p["change_logs"].size() == 1, "creation audit");
    check(request(reader, "GET", pid(p))["id"] == p["id"], "reader sees owner content");
    check(request(b, "GET", "problems")["total"] == 0, "owner isolation list");
    fails(404, [&] { request(b, "GET", pid(p)); }, "foreign content fetch");
    fails(
        404,
        [&] {
          request(b, "PATCH", pid(p),
                  {{"title", "stolen"}, {"editor_tool", "test"}, {"change_summary", "forbidden"}});
        },
        "foreign update");
    fails(404, [&] { request(b, "DELETE", pid(p)); }, "foreign delete");
    fails(403, [&] { request(reader, "POST", "problems", create_body()); }, "readonly create");
    fails(
        403,
        [&] {
          request(reader, "PATCH", pid(p),
                  {{"title", "forbidden"}, {"editor_tool", "test"}, {"change_summary", "forbidden"}});
        },
        "readonly update");
    fails(403, [&] { request(reader, "DELETE", pid(p)); }, "readonly delete");
    fails(422, [&] { request(a, "PATCH", pid(p), {{"title", "missing audit"}}); }, "audit required");
    for (const std::string svg :
         {"<img src='data:image/png,AAAA' />",
          "<!DOCTYPE svg [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><svg>&x;</svg>",
          "<svg><script>alert(1)</script></svg>", "<svg onload='alert(1)'/>",
          "<svg><foreignObject><div/></foreignObject></svg>",
          "<svg><use href='https://example.com/a.svg#id'/></svg>",
          "<svg><image href='data:image/png;base64,AAAA'/></svg>",
          "<svg><rect fill='url(https://example.com/a)'/></svg>", "<svg><g></svg>", "<svg/><svg/>"}) {
      fails(
          400,
          [&] {
            auto bad = create_body();
            bad["diagrams"] = Json::array({Json{{"svg", svg}}});
            request(a, "POST", "problems", bad);
          },
          "unsafe or invalid SVG rejected");
    }
    auto safe_svg =
        "<svg xmlns='http://www.w3.org/2000/svg'><defs><linearGradient id='shade'><stop offset='0%' "
        "stop-color='red'/></linearGradient></defs><rect width='10' height='10' fill='url(#shade)'/></svg>";
    auto with_svg = request(a, "PATCH", pid(p),
                            {{"diagrams", Json::array({Json{{"svg", safe_svg}}})},
                             {"editor_tool", "test"},
                             {"change_summary", "安全配图"}});
    check(with_svg["diagrams"][0]["svg"] == safe_svg, "safe local SVG definitions preserved");
    fails(
        400,
        [&] {
          auto in = create_body();
          in["subject"] = "unknown";
          request(a, "POST", "problems", in);
        },
        "unknown subject rejected");
    fails(409, [&] { request(a, "DELETE", "subjects/数学"); }, "used subject protected");
    auto patch = request(a, "PATCH", pid(p),
                         {{"approach_md", "先识别直角三角形，然后使用勾股定理计算斜边。"},
                          {"diagrams", Json::array()},
                          {"editor_tool", "test"},
                          {"change_summary", "调整思路与配图"}});
    check(patch["solution"]["answer_md"] == p["solution"]["answer_md"] && patch["diagrams"].empty(),
          "partial update preserves answer");
    check(patch["change_logs"].size() == 3, "update audit");
    check(request(a, "GET", pid(p) + "/logs")["items"].size() == 3, "logs endpoint");
    auto second_body = create_body("函数零点练习", "二次函数");
    second_body["stem_md"] = "求函数 x² - 2x - 3 的零点。";
    second_body["approach_md"] = "因式分解得到 (x-3)(x+1)，令两个因式分别为零。";
    second_body["answer_md"] = "x = 3 或 x = -1。";
    auto p2 = request(a, "POST", "problems", second_body);
    auto tid2 = number(p2["tag_resolution"][0], "tag_id");
    auto foreign = request(b, "POST", "problems", create_body("私有几何资料", "秘密标签"));
    auto foreign_tid = number(foreign["tag_resolution"][0], "tag_id");
    auto page = request(a, "GET", "problems", Json::object(), {{"limit", "1"}, {"offset", "1"}});
    check(page["total"] == 2 && page["items"].size() == 1 && page["items"][0]["id"] == p["id"],
          "stable SQL pagination");
    check(request(a, "GET", "problems", Json::object(), {{"tag_id", std::to_string(tid)}})["total"] == 1,
          "tag ID filter");
    fails(
        404, [&] { request(b, "GET", "problems", Json::object(), {{"tag_id", std::to_string(tid)}}); },
        "foreign tag filter");
    for (const std::string mode : {"keyword", "hybrid", "rag"}) {
      auto hits = request(a, "POST", "search", {{"query", "勾股定理"}, {"mode", mode}})["hits"];
      check(!hits.empty(), "search mode finds content");
      for (const auto &hit : hits)
        check(hit["entity"]["id"] != foreign["id"], "search never crosses owner");
    }
    check(!request(a, "POST", "search", {{"query", "斜边"}, {"mode", "keyword"}})["hits"].empty(),
          "two Chinese characters searchable");
    check(request(a, "POST", "search", {{"query", "私有几何资料"}, {"mode", "keyword"}})["hits"].empty(),
          "keyword isolation");
    check(request(a, "POST", "search", {{"query", "id"}, {"mode", "keyword"}})["hits"].empty(),
          "JSON keys not searchable text");
    auto filtered =
        request(a, "POST", "search", {{"query", "勾股定理"}, {"mode", "hybrid"}, {"tag_id", tid}})["hits"];
    for (const auto &hit : filtered)
      check(hit["entity"]["id"] == p["id"], "search filter respected");
    fails(
        404, [&] { request(a, "POST", "search", {{"query", "私有"}, {"tag_id", foreign_tid}}); },
        "foreign search filter");
    fails(
        400,
        [&] { request(a, "POST", "search", {{"query", "函数"}, {"target", "notes"}, {"tag", "二次函数"}}); },
        "invalid target filter");
    fails(
        400, [&] { request(a, "POST", "search", {{"query", "函数"}, {"mode", "sql"}}); },
        "invalid search mode");
    fails(
        400, [&] { request(a, "POST", "search", {{"query", "函数"}, {"limit", 500}}); },
        "search limit bound");
    fails(
        404,
        [&] { request(a, "POST", "tags/merge", {{"source_tag_id", tid}, {"target_tag_id", foreign_tid}}); },
        "foreign merge blocked");
    fails(
        403,
        [&] { request(reader, "POST", "tags/merge", {{"source_tag_id", tid}, {"target_tag_id", tid2}}); },
        "readonly merge blocked");
    auto suggested =
        request(reader, "POST", "tags/suggest", {{"subject", "数学"}, {"names", Json::array({"勾股定理"})}});
    check(suggested["suggestions"][0]["matches"][0]["via"] == "exact", "readonly suggestion exact");
    check(request(a, "GET", "tags/duplicates", Json::object(), {{"threshold", "0"}})["pairs"].size() == 1,
          "duplicate candidates");
    fails(
        400, [&] { request(a, "GET", "tags/duplicates", Json::object(), {{"threshold", "nan"}}); },
        "NaN threshold rejected");
    auto book = a.save(
        "print_book",
        {{"title", "数学"}, {"subjects", Json::array({"数学"})}, {"tag_ids", Json::array({tid, tid2})}});
    auto merged = request(a, "POST", "tags/merge", {{"source_tag_id", tid2}, {"target_tag_id", tid}});
    check(merged["reindexed_problems"] == 2, "merge updates all affected problems");
    check(request(a, "GET", pid(p2))["tags"] == Json::array({"勾股定理"}), "merged problem tags canonical");
    check(a.get("print_book", number(book, "id"))["tag_ids"] == Json::array({tid}),
          "print filter IDs follow merge");
    auto alias_created = request(a, "POST", "problems", second_body);
    check(alias_created["tag_resolution"][0]["via"] == "alias" &&
              alias_created["tags"] == Json::array({"勾股定理"}),
          "merged alias resolves");
    check(request(a, "GET", "problems", Json::object(), {{"tag", "二次函数"}})["total"] == 3,
          "alias list filter");
    check(request(b, "GET", pid(foreign))["tags"] == Json::array({"秘密标签"}),
          "other owner preserved by merge");
    auto link = a.save("link", {{"left", {{"kind", "problem"}, {"id", id}}},
                                {"right", {{"kind", "problem"}, {"id", number(p2, "id")}}},
                                {"via", "manual"},
                                {"score", nullptr},
                                {"label", "same topic"}});
    check(!request(a, "GET", pid(p))["links"].empty(), "problem links included");
    request(a, "DELETE", pid(p));
    fails(404, [&] { a.get("link", number(link, "id")); }, "links cascade on problem deletion");
    check(db.one("SELECT count(*) AS n FROM records WHERE type='problem_log' AND owner_id=1 AND "
                 "json_extract(data,'$.problem_id')=CAST(? AS INTEGER)",
                 {std::to_string(id)})["n"] == 0,
          "logs cascade on problem deletion");
    check(request(a, "POST", "search", {{"query", "勾股定理练习"}, {"mode", "keyword"}})["hits"].empty(),
          "FTS removes deleted problem");
    check(text_similarity("勾股定理", "勾股定理") > .999, "normalized identical local vectors");
    std::cout << "content tests: " << checked << " assertions passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "content test failed after " << checked << " checks: " << e.what() << "\n";
    return 1;
  }
}
