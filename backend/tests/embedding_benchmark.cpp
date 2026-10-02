#include "core.hpp"
#include "notes.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

using namespace mb;
using Clock = std::chrono::steady_clock;

template <class F> double milliseconds(F &&run) {
  const auto begin = Clock::now();
  run();
  return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}
double median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  return values.at(values.size() / 2);
}

int main() {
  try {
    constexpr int count = 1500, rounds = 5;
    const std::string text = "直角三角形的两条直角边分别为三和四，使用勾股定理计算斜边长度。先识别三角形的直"
                             "角，再写出两直角边平方和等于斜边平方，最后检验计算结果和单位。";
    std::vector<std::string> corpus;
    for (int i = 0; i < count; ++i) {
      std::string value = "合成笔记 " + std::to_string(i) + "\n";
      for (int j = 0; j < 6; ++j)
        value += text + "\n";
      corpus.push_back(std::move(value));
    }
    double checksum = 0;
    auto encode = [&]() {
      for (const auto &value : corpus)
        checksum += hash_embedding(value).at(0);
    };
    clear_embedding_cache();
    const auto encode_cold = milliseconds(encode);
    std::vector<double> encode_warm;
    for (int round = 0; round < rounds; ++round)
      encode_warm.push_back(milliseconds(encode));
    const auto encode_stats = embedding_cache_stats();

    Db db(":memory:");
    initialize(db);
    db.query("INSERT INTO users(id,username,password_hash,role,created_at) "
             "VALUES(1,'benchmark_synthetic','unused','user',?)",
             {now()});
    Store store(db, User{1, 0, "benchmark_synthetic", "user", now()});
    db.exec("BEGIN IMMEDIATE");
    for (int i = 0; i < count; ++i)
      store.save("note", {{"title", "合成笔记 " + std::to_string(i)},
                          {"body_md", corpus[i]},
                          {"subject", "数学"},
                          {"parent_id", nullptr},
                          {"sort_order", i}});
    db.exec("COMMIT");
    Json query{{"query", "勾股定理 斜边 直角三角形"}, {"mode", "rag"}, {"target", "notes"}, {"limit", 20}};
    std::vector<double> search_cold, search_warm;
    Json expected;
    for (int round = 0; round < rounds; ++round) {
      clear_embedding_cache();
      Json result;
      search_cold.push_back(milliseconds([&]() { result = content_search(store, query); }));
      if (round == 0)
        expected = result;
      else if (result != expected)
        throw std::runtime_error("cold query changed results");
    }
    for (int round = 0; round < rounds; ++round) {
      Json result;
      search_warm.push_back(milliseconds([&]() { result = content_search(store, query); }));
      if (result != expected)
        throw std::runtime_error("warm cache changed results");
    }
    auto search_stats = embedding_cache_stats();
    std::vector<double> related_cold, related_warm;
    Json expected_related;
    for (int round = 0; round < rounds; ++round) {
      clear_embedding_cache();
      Json result;
      related_cold.push_back(milliseconds([&]() { result = entity_related(store, "note", 1, false); }));
      if (round == 0)
        expected_related = result;
      else if (result != expected_related)
        throw std::runtime_error("cold related results changed");
    }
    for (int round = 0; round < rounds; ++round) {
      Json result;
      related_warm.push_back(milliseconds([&]() { result = entity_related(store, "note", 1, false); }));
      if (result != expected_related)
        throw std::runtime_error("warm related results changed");
    }
    Json output{{"benchmark", "mistakebook-native synthetic embedding cache benchmark"},
                {"created_at", now()},
                {"compiler", __VERSION__},
                {"corpus_notes", count},
                {"body_bytes_per_note", corpus.front().size()},
                {"warm_rounds", rounds},
                {"scope", "Same C++ implementation, synthetic in-memory SQLite corpus. Cold means embedding "
                          "cache cleared; SQLite page cache is warm. No comparison against the original Node "
                          "implementation; not a disk-I/O or concurrent-load benchmark."},
                {"encoding",
                 {{"cold_ms", encode_cold},
                  {"warm_median_ms", median(encode_warm)},
                  {"ratio", encode_cold / median(encode_warm)},
                  {"cache", encode_stats}}},
                {"rag_search",
                 {{"cold_median_ms", median(search_cold)},
                  {"warm_median_ms", median(search_warm)},
                  {"ratio", median(search_cold) / median(search_warm)},
                  {"identical_results", true},
                  {"cache", search_stats}}},
                {"related_candidates",
                 {{"cold_median_ms", median(related_cold)},
                  {"warm_median_ms", median(related_warm)},
                  {"ratio", median(related_cold) / median(related_warm)},
                  {"identical_results", true},
                  {"cache", embedding_cache_stats()}}},
                {"checksum", checksum}};
    std::cout << output.dump(2) << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
