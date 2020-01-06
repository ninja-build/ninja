#include "public/logger.h"

#include <iostream>

namespace ninja {
namespace {
const char kLogError[] = "ninja: error: ";
const char kLogInfo[] = "ninja: ";
const char kLogWarning[] = "ninja: warning: ";
}  // namespace

void Logger::OnMessage(Logger::Level level, const std::string& message) {
    const char* prefix = kLogError;
    if(level == Logger::Level::INFO) {
      prefix = kLogInfo;
    }
    else if(level == Logger::Level::WARNING) {
      prefix = kLogWarning;
    }
    cerr() << prefix << message << std::endl;
}

std::ostream& LoggerBasic::cout() {
  return std::cout;
}
std::ostream& LoggerBasic::cerr() {
  return std::cerr;
}

LoggerNull::LoggerNull() :
  null_buffer(new NullBuffer()),
  null_stream(null_buffer) {}

LoggerNull::~LoggerNull() {
  delete null_buffer;
}

std::ostream& LoggerNull::cerr() {
  return null_stream;
}
std::ostream& LoggerNull::cout() {
  return null_stream;
}

}  // namespace ninja
