#pragma once
#include "core.hpp"
#include "httplib.h"

namespace mb {
void initialize_oauth(Db &);
std::optional<User> oauth_authenticate(Db &, const std::string &token);
bool oauth_route(Db &, const httplib::Request &, httplib::Response &);
} // namespace mb
