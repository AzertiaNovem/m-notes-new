#pragma once
#include "core.hpp"
namespace mb {
Json entity_links(Store &, const std::string &kind, int64_t id);
Json entity_related(Store &, const std::string &kind, int64_t id, bool refresh = false);
std::vector<float> hash_embedding(const std::string &);
double text_similarity(const std::string &, const std::string &);
// Internal diagnostics for reproducible cache tests and benchmarks; not an API route.
Json embedding_cache_stats();
void clear_embedding_cache();
} // namespace mb
