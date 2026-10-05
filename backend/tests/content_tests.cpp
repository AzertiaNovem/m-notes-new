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
void highlight_similarity_tests() {
  clear_embedding_cache();
  const auto plain = hash_embedding("直角三角形勾股定理 $a^2+b^2=c^2$");
  for (const std::string prefix : {"", "yellow:", "red:", "green:", "blue:", "黄:", "红:", "绿:", "蓝:"})
    check(hash_embedding("==" + prefix + "直角三角形勾股定理 $a^2+b^2=c^2$==") == plain,
          "all highlight colors and aliases preserve exactly the same vector");
  auto cache = embedding_cache_stats();
  check(number(cache, "entries") == 1 && number(cache, "misses") == 1 && number(cache, "hits") == 9,
        "color-only changes share the semantic cache key");
  check(hash_embedding("==red:甲====green:乙==") == hash_embedding("甲乙"),
        "adjacent colored markers do not add separators");
  check(hash_embedding("题目 ==red:$a==b$==，使用 ==blue:`a==b`==") ==
            hash_embedding("题目 $a==b$，使用 `a==b`"),
        "outer highlight preserves inline math and code containing apparent closers");
  check(hash_embedding("==red:`==green:literal==`==") == hash_embedding("`==green:literal==`"),
        "a highlight can wrap code without interpreting its literal markers");
  check(hash_embedding("==blue: **重点** 和 $x$ ==") == hash_embedding(" **重点** 和 $x$ "),
        "highlight removal preserves body formatting and whitespace");
  for (const std::string literal : {"`==red:literal==`", "$==red:literal==$", "$$==red:literal==$$",
                                    "\\(==red:literal==\\)", "\\[==red:literal==\\]"})
    check(hash_embedding(literal) != hash_embedding("literal"), "code and math own literal highlight syntax");
  check(hash_embedding("$==red:literal==$") != hash_embedding("$literal$"),
        "inline math literal markers do not become highlights");
  check(hash_embedding("`==red:literal==`") != hash_embedding("`literal`"),
        "inline code literal markers do not become highlights");
  const std::string fenced = "```text\n==red:literal==\n```\n==green:正文==";
  check(hash_embedding(fenced) == hash_embedding("```text\n==red:literal==\n```\n正文"),
        "fenced code is literal while later ordinary highlights are removed");
  check(hash_embedding("~~~\n==red:literal==\n~~~\n==green:正文==") ==
            hash_embedding("~~~\n==red:literal==\n~~~\n正文"),
        "tilde-fenced code is protected");
  check(hash_embedding("$$\n==red:literal==\n$$\n==green:正文==") ==
            hash_embedding("$$\n==red:literal==\n$$\n正文"),
        "display math is protected");
  check(hash_embedding("    ==red:literal==\n==green:正文==") == hash_embedding("    ==red:literal==\n正文"),
        "indented code is protected");
  for (const std::string whitespace : {"\u00a0", "\u3000", "\u2003", "\ufeff"})
    check(hash_embedding("==red:" + whitespace + "==") != hash_embedding(whitespace),
          "Unicode whitespace-only markers remain literal like the frontend");
  check(hash_embedding("说明\n    ==red:重点==") == hash_embedding("说明\n    重点") &&
            hash_embedding("- 概念\n    ==red:条件==") == hash_embedding("- 概念\n    条件"),
        "indented paragraph and list continuations still strip actual highlights");
  for (const auto &example : std::vector<std::pair<std::string, std::string>>{
           {"> ```text\n> a ``` b\n> ==red:literal==\n> ```\n==green:正文==",
            "> ```text\n> a ``` b\n> ==red:literal==\n> ```\n正文"},
           {"- ```text\n  a ``` b\n  ==red:literal==\n  ```\n==green:正文==",
            "- ```text\n  a ``` b\n  ==red:literal==\n  ```\n正文"},
           {"1. ```text\n   a ``` b\n   ==red:literal==\n   ```\n==green:正文==",
            "1. ```text\n   a ``` b\n   ==red:literal==\n   ```\n正文"},
           {"> > ```text\n> > a ``` b\n> > ==red:literal==\n> > ```\n==green:正文==",
            "> > ```text\n> > a ``` b\n> > ==red:literal==\n> > ```\n正文"},
           {"> - ```text\n>   a ``` b\n>   ==red:literal==\n>   ```\n==green:正文==",
            "> - ```text\n>   a ``` b\n>   ==red:literal==\n>   ```\n正文"},
           {"> ```text\n> ==red:literal==\n==green:正文==", "> ```text\n> ==red:literal==\n正文"}})
    check(hash_embedding(example.first) == hash_embedding(example.second),
          "container fences protect code to their matching closure or container end");
  for (const std::string rejected :
       {"==purple:正文==", "==constructor:正文==", "==toString:正文==", "==RED:正文==", "==red:\n正文==",
        "==red:\r正文==", "==red:   ==", "====", "==red:正文"})
    check(hash_embedding(rejected) != hash_embedding("正文"),
          "unknown colors, empty, multiline and unclosed highlights remain literal");
  check(hash_embedding("====") != hash_embedding("") &&
            hash_embedding("==red:   ==") != hash_embedding("   "),
        "empty and whitespace-only markers are not erased");
  check(hash_embedding("==purple:正文==") != hash_embedding("purple:正文"),
        "unknown-color delimiters remain literal too");
  check(hash_embedding("==red:跨\n行==") != hash_embedding("跨\n行"),
        "multiline markers are not reduced to their body");
  check(hash_embedding("==purple:正文== ==red:重点==") == hash_embedding("==purple:正文== 重点"),
        "an unknown color does not consume the following valid marker");
  check(hash_embedding("==red:跨\n行== ==blue:重点==") == hash_embedding("==red:跨\n行== 重点"),
        "a multiline marker does not consume the following valid marker");
  check(hash_embedding("==red: \n `$x$` == ==blue:重点==") == hash_embedding("==red: \n `$x$` == 重点"),
        "a multiline marker wrapping inline nodes remains literal");
  for (const std::string body : {" red:正文", "red1:正文", "红：正文"})
    check(hash_embedding("==" + body + "==") == hash_embedding(body),
          "only the documented letter-plus-ASCII-colon grammar declares a color");
  const std::string left = "椭圆轨道中的天体运动与万有引力", right = "酸碱中和中的离子浓度与电荷守恒";
  check(text_similarity("==red:" + left + "==", "==red:" + right + "==") == text_similarity(left, right),
        "a shared color cannot create an artificial similarity");
  check(text_similarity("==red:相同正文==", "==blue:相同正文==") > .999,
        "changing colors does not reduce identical-content similarity");
  const std::string large(70000, 'x');
  check(hash_embedding("==red:" + large + "==") == hash_embedding(large),
        "the large-text cache bypass also strips display markers");
  std::string original = "==红:保留导出的原始标记==";
  hash_embedding(original);
  check(original == "==红:保留导出的原始标记==", "similarity preprocessing never mutates source text");
}
void highlight_chunk_tests() {
  Db db(":memory:");
  initialize(db);
  db.query("INSERT INTO users(id,username,password_hash,role,created_at) VALUES(1,'chunks','test','user',?)",
           {now()});
  Store store(db, User{1, 0, "chunks", "user", now()});
  std::string body;
  for (int i = 0; i < 200; ++i)
    body += "直角三角形勾股定理";
  auto plain_note = store.save("note", {{"title", "长笔记"}, {"body_md", body}});
  auto red_note = store.save("note", {{"title", "==red:长笔记=="}, {"body_md", "==red:" + body + "=="}});
  auto blue_note = store.save("note", {{"title", "==blue:长笔记=="}, {"body_md", "==蓝:" + body + "=="}});
  auto plain_problem = store.save("problem", {{"title", "长错题"},
                                              {"subject", "数学"},
                                              {"stem_md", body},
                                              {"tags", Json::array({"长考点"})},
                                              {"solution", {{"approach_md", body}, {"answer_md", body}}}});
  auto color_problem = store.save(
      "problem",
      {{"title", "==green:长错题=="},
       {"subject", "数学"},
       {"stem_md", "==red:" + body + "=="},
       {"tags", Json::array({"长考点"})},
       {"solution", {{"approach_md", "==green:" + body + "=="}, {"answer_md", "==blue:" + body + "=="}}}});
  const auto result = content_search(store, {{"query", "勾股定理"}, {"mode", "rag"}, {"limit", 100}});
  Json by_entity = Json::object();
  for (const auto &hit : result["hits"]) {
    const auto id = std::to_string(number(hit["entity"], "id"));
    by_entity[id][std::to_string(number(hit, "chunk_id") % 100000)] = {
        {"text", hit["text"]}, {"score", hit["score"]}, {"kind", hit["kind"]}};
    check(text(hit, "text").find("==") == std::string::npos,
          "long display annotations are removed before search chunking");
    if (hit["entity"]["id"] == red_note["id"])
      check(hit["entity"]["title"] == red_note["title"] && hit["entity"]["body_md"] == red_note["body_md"],
            "search note entity retains original highlighted Markdown");
    if (hit["entity"]["id"] == color_problem["id"])
      check(hit["entity"]["stem_md"] == color_problem["stem_md"],
            "search problem entity retains original highlighted stem");
  }
  const auto &notes = by_entity.at(std::to_string(number(plain_note, "id")));
  check(notes.size() > 1, "long highlighted regression really spans multiple search chunks");
  check(notes == by_entity.at(std::to_string(number(red_note, "id"))) &&
            notes == by_entity.at(std::to_string(number(blue_note, "id"))),
        "long note text and scores are identical across colors and plain content");
  check(by_entity.at(std::to_string(number(plain_problem, "id"))) ==
            by_entity.at(std::to_string(number(color_problem, "id"))),
        "problem stem summary, approach and answer are cleaned before truncation and chunking");
  for (const std::string delimiter : {"`", "$"}) {
    const auto literal_body = delimiter + std::string(805, 'x') + " ==red:literal== tail " + delimiter;
    const auto plain_body = delimiter + std::string(805, 'x') + " literal tail " + delimiter;
    auto literal = store.save("note", {{"title", "literal title"}, {"body_md", literal_body}});
    auto plain = store.save("note", {{"title", "literal title"}, {"body_md", plain_body}});
    auto hits = content_search(
        store, {{"query", "literal tail"}, {"target", "notes"}, {"mode", "rag"}, {"limit", 100}})["hits"];
    Json literal_tail, plain_tail;
    for (const auto &hit : hits)
      if (number(hit, "chunk_id") % 100000 == 2) {
        if (hit["entity"]["id"] == literal["id"])
          literal_tail = hit;
        if (hit["entity"]["id"] == plain["id"])
          plain_tail = hit;
      }
    check(!literal_tail.is_null() && !plain_tail.is_null(),
          "literal regression has searchable second chunks");
    check(text(literal_tail, "text").find("==red:literal==") != std::string::npos,
          "math and code literal markers survive search chunking");
    check(literal_tail["score"] != plain_tail["score"],
          "already-cleaned chunks do not reinterpret literal markers after losing their opener");
    check(store.get("note", number(literal, "id"))["body_md"] == literal_body,
          "chunk cleaning does not change stored code or math");
  }
}
} // namespace
int main() {
  try {
    highlight_similarity_tests();
    highlight_chunk_tests();
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
