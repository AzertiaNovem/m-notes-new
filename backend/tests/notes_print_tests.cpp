#include "core.hpp"
#include "notes.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#define CHECK(condition)                                                                                     \
  do {                                                                                                       \
    if (!(condition))                                                                                        \
      throw std::runtime_error("check failed at line " + std::to_string(__LINE__) + ": " #condition);        \
  } while (false)
using namespace mb;
Json notes(Store &s, std::string m, std::string p, Json b = Json::object(), Query q = {}) {
  return notes_route(s, m, "/api/v1/notes" + p, b, q).value();
}
Json prints(Store &s, std::string m, std::string p, Json b = Json::object(), Query q = {}) {
  return print_route(s, m, "/api/v1/print/books" + p, b, q).value();
}
void denied(const std::function<void()> &f, int status) {
  bool caught = false;
  try {
    f();
  } catch (const Error &e) {
    CHECK(e.status == status);
    caught = true;
  }
  CHECK(caught);
}
void cache_tests() {
  clear_embedding_cache();
  auto first = hash_embedding("勾股定理：两直角边平方和等于斜边平方。");
  CHECK(first == hash_embedding("勾股定理：两直角边平方和等于斜边平方。"));
  CHECK(embedding_cache_stats().at("hits") == 1);
  CHECK(first != hash_embedding("牛顿第二定律：合外力等于质量乘以加速度。"));
  std::vector<std::thread> workers;
  for (int i = 0; i < 8; ++i)
    workers.emplace_back([&]() {
      for (int j = 0; j < 100; ++j)
        CHECK(first == hash_embedding("勾股定理：两直角边平方和等于斜边平方。"));
    });
  for (auto &worker : workers)
    worker.join();
  CHECK(embedding_cache_stats().at("hits") == 801);
  auto before = embedding_cache_stats();
  hash_embedding(std::string(65537, 'x'));
  auto after = embedding_cache_stats();
  CHECK(after.at("bypassed") == 1);
  CHECK(after.at("entries") == before.at("entries"));
  clear_embedding_cache();
  for (int i = 0; i < 8193; ++i)
    hash_embedding("unique vector cache key " + std::to_string(i));
  auto bounded = embedding_cache_stats();
  CHECK(bounded.at("entries") == 8192);
  CHECK(bounded.at("evictions") == 1);
  auto misses = number(bounded, "misses");
  hash_embedding("unique vector cache key 0");
  CHECK(number(embedding_cache_stats(), "misses") == misses + 1);
  clear_embedding_cache();
  for (int i = 0; i < 270; ++i) {
    std::string key(65520, 'a');
    key += std::to_string(i);
    hash_embedding(key);
  }
  bounded = embedding_cache_stats();
  CHECK(number(bounded, "estimated_bytes") <= number(bounded, "max_bytes"));
  CHECK(number(bounded, "evictions") > 0);
  CHECK(number(bounded, "entries") < 270);
  clear_embedding_cache();
}
int main() {
  Db db(":memory:");
  initialize(db);
  db.query("INSERT INTO users(id,username,password_hash,role,created_at) "
           "VALUES(1,'one','x','user','2026'),(2,'two','x','user','2026')");
  Store s(db, User{1, 0, "one", "user", "2026"}), other(db, User{2, 0, "two", "user", "2026"}),
      ro(db, User{3, 1, "viewer", "readonly", "2026"});
  s.save("subject", {{"name", "数学"}});
  other.save("subject", {{"name", "数学"}});
  Json audit{{"editor_tool", "test"}, {"change_summary", "测试"}};
  auto create = [&](std::string title, Json parent = nullptr) {
    auto b = audit;
    b["title"] = title;
    b["body_md"] = "第一行\n第二行";
    b["subject"] = "数学";
    b["parent_id"] = parent;
    return notes(s, "POST", "", b).at("node");
  };
  auto a = create("父节点"), b = create("子节点", a.at("id"));
  auto ai = number(a, "id"), bi = number(b, "id");
  CHECK(notes(s, "GET", "/toc").at("items").size() == 1);
  CHECK(notes(s, "GET", "/toc").at("items")[0].at("children").size() == 1);
  denied([&] { notes(s, "DELETE", "/" + std::to_string(ai)); }, 409);
  auto move = audit;
  move["parent_id"] = bi;
  denied([&] { notes(s, "POST", "/" + std::to_string(ai) + "/move", move); }, 400);
  denied([&] { notes(other, "GET", "/" + std::to_string(ai)); }, 404);
  denied([&] { notes(ro, "PATCH", "/" + std::to_string(ai), audit); }, 403);
  auto patch = audit;
  patch["body_md"] = "第一行\n修改第二行\n第三行";
  notes(s, "PATCH", "/" + std::to_string(ai), patch);
  auto versions = notes(s, "GET", "/" + std::to_string(ai) + "/versions").at("items");
  CHECK(versions.size() == 2);
  auto d =
      notes(s, "GET",
            "/" + std::to_string(ai) + "/versions/" + std::to_string(number(versions[0], "id")) + "/diff");
  bool diffFound = false;
  for (auto &h : d.at("hunks"))
    if (h.at("kind") == "replace")
      diffFound = true;
  CHECK(diffFound);
  auto first =
      notes(s, "GET",
            "/" + std::to_string(ai) + "/versions/" + std::to_string(number(versions[1], "id")) + "/diff");
  CHECK(first.at("from").at("source") == "empty");
  denied(
      [&] {
        notes(other, "GET",
              "/" + std::to_string(ai) + "/versions/" + std::to_string(number(versions[0], "id")));
      },
      404);
  denied(
      [&] {
        notes(s, "GET", "/" + std::to_string(bi) + "/versions/" + std::to_string(number(versions[0], "id")));
      },
      404);
  auto foreign = other.save("note", {{"title", "其他用户的笔记"},
                                     {"body_md", "保密"},
                                     {"subject", "数学"},
                                     {"parent_id", nullptr},
                                     {"sort_order", 0}});
  denied(
      [&] {
        notes(
            s, "POST", "/links",
            {{"from", {{"kind", "note"}, {"id", ai}}}, {"to", {{"kind", "note"}, {"id", foreign.at("id")}}}});
      },
      404);
  auto bad_parent = audit;
  bad_parent["parent_id"] = foreign.at("id");
  denied([&] { notes(s, "POST", "/" + std::to_string(ai) + "/move", bad_parent); }, 404);
  auto toc = audit;
  toc["nodes"] = Json::array({{{"id", ai}}, {{"id", bi}}});
  notes(s, "POST", "/toc", toc);
  CHECK(notes(s, "GET", "/toc").at("items").size() == 2);
  auto link = notes(s, "POST", "/links",
                    {{"from", {{"kind", "note"}, {"id", ai}}}, {"to", {{"kind", "note"}, {"id", bi}}}});
  CHECK(link.at("left").at("id") == ai);
  CHECK(notes(s, "GET", "/graph").at("nodes").size() == 2);
  Json problem = audit;
  problem.update({{"title", "平方"},
                  {"stem_md", "计算 $2^2$"},
                  {"subject", "数学"},
                  {"tags", Json::array({"幂"})},
                  {"approach_md", "使用平方"},
                  {"answer_md", "4"}});
  auto p = content_route(s, "POST", "/api/v1/problems", problem, {}).value();
  auto pi = number(p, "id");
  CHECK(pi > 0);
  auto book = prints(s, "POST", "", {{"title", "数学打印"}, {"subjects", Json::array({"数学"})}});
  auto book_id = number(book, "id");
  auto bookpath = "/" + std::to_string(book_id);
  auto plan = prints(s, "GET", bookpath + "/plan");
  CHECK(plan.at("page_count") == 2);
  auto apply = prints(s, "POST", bookpath + "/apply");
  CHECK(apply.at("kind") == "full");
  auto doc = prints(ro, "GET", bookpath + "/document");
  CHECK(doc.at("pages")[0].at("problems")[0].at("title") == "平方");
  CHECK(prints(ro, "POST", bookpath + "/reprint", {{"page_nos", {1}}}).at("pages").size() == 1);
  denied([&] { prints(ro, "POST", bookpath + "/apply"); }, 403);
  denied([&] { prints(other, "GET", bookpath + "/document"); }, 404);
  denied([&] { prints(s, "POST", bookpath + "/apply"); }, 409);
  problem = audit;
  problem["title"] = "新的平方";
  content_route(s, "PATCH", "/api/v1/problems/" + std::to_string(pi), problem, {});
  CHECK(prints(s, "GET", bookpath).at("pages")[0].at("stale") == true);
  CHECK(prints(s, "GET", bookpath + "/document").at("pages")[0].at("problems")[0].at("title") == "平方");
  CHECK(prints(s, "GET", bookpath + "/plan").at("reprint_page_nos").size() == 2);
  prints(s, "POST", bookpath + "/apply");
  CHECK(prints(s, "GET", bookpath + "/document").at("pages")[0].at("problems")[0].at("title") == "新的平方");
  std::cout << "PASS notes tree, cycle rejection, versions/diff, links, tenant isolation, readonly and print "
               "snapshot lifecycle\n";
  cache_tests();
  std::cout << "PASS embedding cache equality, invalidation, concurrent hits, entry/memory bounds and "
               "large-key bypass\n";
}
