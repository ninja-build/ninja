#include "public/logger.h"

#include <iostream>

namespace ninja {
namespace {
const char kLogError[] = "ninja: error: ";
const char kLogInfo[] = "ninja: ";
const char kLogWarning[] = "ninja: warning: ";
}  // namespace

void LoggerBasic::OnMessage(Logger::Level level, const std::string& message) {
    const char* prefix = kLogError;
    if(level == Logger::Level::INFO) {
      prefix = kLogInfo;
    }
    else if(level == Logger::Level::WARNING) {
      prefix = kLogWarning;
    }
    std::cerr << prefix << message << std::endl;
}

void LoggerNull::OnMessage(Logger::Level level, const std::string& message) {}

}  // namespace ninja
