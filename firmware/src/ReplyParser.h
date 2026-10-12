#pragma once

#include <string>

// Interprets the webhook's HTTP status and JSON body.
namespace ReplyParser {

struct Reply {
  bool ok = false;      // 2xx AND body {"ok":true}
  bool known = false;   // body "known" (present events only)
  std::string message;  // one line for the serial log
};

Reply parse(int httpCode, const std::string& body);

}  // namespace ReplyParser
