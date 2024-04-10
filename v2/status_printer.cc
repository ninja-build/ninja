#include "status_printer.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <cassert>
#include <cstdarg>
#include <print>

#include "../src/graph.h"
#include "../src/metrics.h"

extern boost::asio::io_context gContext;

Status* Status::factory(const BuildConfig& config) {
  return new StatusPrinter;
}

StatusPrinter::StatusPrinter()
    : timer_(gContext, boost::posix_time::seconds(0)) {}

void StatusPrinter::EdgeAddedToPlan(const Edge* edge) {
  ++total_edges_;
}

void StatusPrinter::EdgeRemovedFromPlan(const Edge* edge) {
  assert(false);
}

void StatusPrinter::BuildEdgeStarted(const Edge* edge,
                                     int64_t start_time_millis) {
  auto result = running_edges_.emplace(edge, GetTimeMillis());
  assert(result.second);
  if (edge->use_console()) {
    PrintStatus();
    console_locked_ = true;
    printer_.SetConsoleLocked(console_locked_);
  }
}

void StatusPrinter::BuildEdgeFinished(Edge* edge, int64_t start_time_millis,
                                      int64_t end_time_millis, bool success,
                                      const std::string& output) {
  ++finished_edges_;
  int num_removed = running_edges_.erase(edge);
  assert(num_removed == 1);

  if (edge->use_console()) {
    console_locked_ = false;
    printer_.SetConsoleLocked(console_locked_);
    // TODO: should have stopped the timer and started it again here
  }

  if (!success) {
    ++failed_edges_;
  }

  if (failed_edges_ > 1) {
    return;
  }

  // Print the command that is spewing before printing its output.
  if (!success) {
    std::string outputs;
    for (auto o = edge->outputs_.cbegin(); o != edge->outputs_.cend(); ++o) {
      outputs += (*o)->path() + " ";
    }
    printer_.PrintOnNewLine(
        "\x1B[1;31m"
        "failed: "
        "\x1B[0m" +
        outputs + "\n");
    // printer_.PrintOnNewLine(edge->EvaluateCommand() + "\n");
  }

  if (!output.empty()) {
#ifdef _WIN32
    // Fix extra CR being added on Windows, writing out CR CR LF (#773)
    _setmode(_fileno(stdout), _O_BINARY);  // Begin Windows extra CR fix
#endif

    printer_.PrintOnNewLine(output);

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_TEXT);  // End Windows extra CR fix
#endif
  }
}

void StatusPrinter::BuildLoadDyndeps() {
  assert(false);
}

void StatusPrinter::BuildStarted() {
  finished_edges_ = 0;
  assert(failed_edges_ == 0);
  assert(running_edges_.empty());
  StartTimer();
}

void StatusPrinter::BuildFinished() {
  console_locked_ = false;
  printer_.SetConsoleLocked(console_locked_);
  printer_.PrintOnNewLine("");
  if (failed_edges_ > 0) {
    std::print("ninja: \x1b[1;31m{} job{} failed.\x1b[0m\n", failed_edges_,
               failed_edges_ == 1 ? "" : "s");
  } else {
    std::print("ninja: \x1b[1;32mdone\x1b[0m\n");
  }
}

void StatusPrinter::Info(const char* msg, ...) {
  va_list ap;
  va_start(ap, msg);
  Print(stdout, msg, ap);
  va_end(ap);
}

void StatusPrinter::Warning(const char* msg, ...) {
  assert(false);
}

void StatusPrinter::Error(const char* msg, ...) {
  va_list ap;
  va_start(ap, msg);
  fprintf(stderr, "ninja: \x1b[1;31merror:\x1b[0m ");
  Print(stderr, msg, ap);
  va_end(ap);
}

void StatusPrinter::Print(FILE* stream, const char* msg, va_list ap) {
  vfprintf(stream, msg, ap);
  fprintf(stream, "\n");
}

void StatusPrinter::StartTimer() {
  timer_.expires_at(timer_.expires_at() + boost::posix_time::seconds(1) / 60);
  timer_.async_wait(
      [this](boost::system::error_code err) { TimerCallback(err); });
}

void StatusPrinter::TimerCallback(boost::system::error_code err) {
  assert(!err);
  PrintStatus();
  StartTimer();
}

void StatusPrinter::PrintStatus() {
  if (console_locked_ || running_edges_.empty()) {
    return;
  }
  bool force_full_command = false;
  const auto now = std::chrono::milliseconds(GetTimeMillis());
  auto it = running_edges_.find(last_printed_edge_.first);
  if (it == running_edges_.end()) {
    it = running_edges_.begin();
  } else if ((now - last_printed_edge_.second) > std::chrono::seconds(2)) {
    ++it;
    if (it == running_edges_.end()) {
      it = running_edges_.begin();
    }
  }
  if (last_printed_edge_.first != it->first) {
    last_printed_edge_.first = it->first;
    last_printed_edge_.second = now;
  }
  std::ostringstream sstream;
  const auto duration = (now - it->second).count() / 100;
  sstream << "[" << finished_edges_ << "/" << total_edges_
          << " running: " << running_edges_.size() << "] " << (duration / 10)
          << '.' << (duration % 10) << "s | "
          << last_printed_edge_.first->GetBinding("description");
  printer_.Print(sstream.str(),
                 force_full_command ? LinePrinter::FULL : LinePrinter::ELIDE);
}
