#pragma once

#include <string>

#include "AppState.h"

// Blocking HTTP POST to the Home Assistant webhook. Call only from loop().
namespace HaClient {
// POSTs `payload` as application/json to `url`. 5 s timeout, one retry after 500 ms.
HaResult send(const std::string& url, const std::string& payload);
}
