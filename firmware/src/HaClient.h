#pragma once

#include <string>

// Blocking HTTP POST of a JSON body. Call only from loop().
namespace HaClient {

struct Response {
  int httpCode = -1;  // <= 0: transport error
  std::string body;
};

// 3 s connect/read timeouts; one retry after 300 ms on a transport error.
Response post(const std::string& url, const std::string& json);

}  // namespace HaClient
