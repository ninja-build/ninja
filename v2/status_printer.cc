#include "status_printer.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <cassert>
#include <cstdarg>
#include <print>

#include "../src/graph.h"
#include "../src/metrics.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

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
  --total_edges_;
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
                                      int64_t end_time_millis,
                                      ExitStatus exit_code,
                                      const std::string& output) {
  ++finished_edges_;
  int num_removed [[maybe_unused]] = running_edges_.erase(edge);
  assert(num_removed == 1);

  if (edge->use_console()) {
    console_locked_ = false;
    printer_.SetConsoleLocked(console_locked_);
    // TODO: should have stopped the timer and started it again here
  }

  if (exit_code != ExitSuccess) {
    ++failed_edges_;
  }

  if (failed_edges_ > 1) {
    return;
  }

  // Print the command that is spewing before printing its output.
  if (exit_code != ExitSuccess) {
    std::string outputs;
    for (auto o = edge->outputs_.cbegin(); o != edge->outputs_.cend(); ++o) {
      outputs += (*o)->path() + " ";
    }
    printer_.PrintOnNewLine(
        "\x1B[1;31m"
        "failed [" +
        std::to_string(exit_code) +
        "]: "
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
    std::print("ninja: \x1b[1;32mdone\x1b[0m\x1b]9;4;0;\007\n");
  }
}

void StatusPrinter::SetExplanations(Explanations*) {
  // assert(false); // TODO
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
  assert(!err || err == boost::system::errc::operation_canceled);
  PrintStatus();
  StartTimer();
}

#define BASE 65521L /* largest prime smaller than 65536 */
#define NMAX 5552
/* NMAX is the largest n such that 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1 */

#define DO1(buf, i) \
  {                 \
    s1 += buf[i];   \
    s2 += s1;       \
  }
#define DO2(buf, i) \
  DO1(buf, i);      \
  DO1(buf, i + 1);
#define DO4(buf, i) \
  DO2(buf, i);      \
  DO2(buf, i + 2);
#define DO8(buf, i) \
  DO4(buf, i);      \
  DO4(buf, i + 4);
#define DO16(buf) \
  DO8(buf, 0);    \
  DO8(buf, 8);

unsigned long adler32(const char* buf, size_t len) {
  unsigned long s1 = 0xffff;
  unsigned long s2 = (s1 >> 16) & 0xffff;
  int k;
  if (buf == nullptr)
    return 1L;

  while (len > 0) {
    k = len < NMAX ? len : NMAX;
    len -= k;
    while (k >= 16) {
      DO16(buf);
      buf += 16;
      k -= 16;
    }
    if (k != 0)
      do {
        s1 += *buf++;
        s2 += s1;
      } while (--k);
    s1 %= BASE;
    s2 %= BASE;
  }
  return (s2 << 16) | s1;
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

  std::string description = last_printed_edge_.first->GetBinding("description");
  const auto first_space = description.find_first_of(' ');
  const auto second_space = description.find_first_of(' ', first_space + 1);
  unsigned long hash_number = 0;
  if (first_space != second_space) {
    const auto third_space = description.find_first_of(' ', second_space + 1);
    auto first_word = description.substr(0, first_space);
    first_word += description[second_space + 1];
    hash_number = adler32(first_word.data(), first_word.size()) % 10;
  }

  const char* progress[] = {
    "⠀", "⠁", "⠉", "⠋", "⠛",
    "⠟", "⠿", "⡿", "⣿"
    // "⠀⠀",
    // "⠁⠀",
    // "⠉⠀",
    // "⠉⠁",
    // "⠉⠉",
    // "⠋⠉",
    // "⠛⠉",
    // "⠛⠋",
    // "⠛⠛",
    // "⠟⠛",
    // "⠿⠛",
    // "⠿⠟",
    // "⠿⠿",
    // "⡿⠿",
    // "⣿⠿",
    // "⣿⡿",
    // "⣿⣿"
  };

  uint8_t percentage = finished_edges_ *
                       (sizeof(progress) / sizeof(progress[0]) - 1) /
                       total_edges_;
  uint8_t percentage_with_running =
      (running_edges_.size() + finished_edges_) *
      (sizeof(progress) / sizeof(progress[0]) - 1) / total_edges_;

  if ((now.count() / 100) % 2 == 0) {
    if (percentage == percentage_with_running) {
      percentage += 1;
    } else {
      percentage = percentage_with_running;
    }
  }

  sstream << progress[percentage] << " \x1b[34m" << running_edges_.size()
          << "\x1b[0m \x1b[36m" << finished_edges_ << "\x1b[0m " << total_edges_
          << " \x1b[" << (hash_number > 4 ? 1 : 0) << ";3"
          << (hash_number % 5 + 2) << "m" << description << "\x1b[0m"
          << "\033]9;4;1;" << (finished_edges_ * 100 / total_edges_) << "\007";
  if (duration > 20) {
    sstream << " ⌛ " << (duration / 10) << '.' << (duration % 10) << "s";
  }

  printer_.Print(sstream.str(),
                 force_full_command ? LinePrinter::FULL : LinePrinter::ELIDE);
}
