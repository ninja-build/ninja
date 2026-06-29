// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "status_printer.h"

#ifdef _WIN32
#include "win32port.h"
#else
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <cinttypes>
#endif

#include <stdarg.h>
#include <stdlib.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "build.h"
#include "debug_flags.h"
#include "exit_status.h"
#include "lexer.h"
#include "metrics.h"
#include "util.h"

using namespace std;

namespace {
/// Env that resolves variables in a `--status` format string by asking
/// the StatusPrinter for their current value.
struct StatusFormatEnv : public Env {
  const StatusPrinter* printer;
  explicit StatusFormatEnv(const StatusPrinter* p) : printer(p) {}
  string LookupVariable(const string& var) override {
    return printer->FormatStatusVariable(var);
  }
};
}  // namespace

Status* Status::factory(const BuildConfig& config) {
  return new StatusPrinter(config);
}

StatusPrinter::StatusPrinter(const BuildConfig& config)
    : config_(config), started_edges_(0), finished_edges_(0), total_edges_(0),
      progress_status_format_(NULL),
      current_rate_(config.parallelism) {
  // Don't do anything fancy in verbose mode.
  if (config_.verbosity != BuildConfig::NORMAL)
    printer_.set_smart_terminal(false);

  // If explicitly enabled and the terminal supports ANSI codes, use
  // multi-line real-time status.
  if (config_.multiline_console && printer_.supports_color()) {
    multi_line_status_ = true;
    printer_.set_smart_terminal(false);

    // Limit status lines to terminal height - 1 (for the cursor line),
    // capped at 8.
    int term_lines = 0;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
      term_lines = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
      term_cols_ = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }
#else
    struct winsize ws = {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
      term_lines = ws.ws_row;
    if (ws.ws_col > 0)
      term_cols_ = ws.ws_col;
#endif
    if (term_lines > 1 && term_lines - 1 < max_status_lines_)
      max_status_lines_ = term_lines - 1;
  }

  if (config.progress_status_format) {
    // --status uses Ninja-style variable expansion ($var / ${var}).
    // Append a newline because Lexer::ReadVarValue terminates on \n.
    string input = string(config.progress_status_format) + "\n";
    Lexer lexer;
    lexer.Start("--status", input);
    status_eval_.reset(new EvalString());
    string err;
    if (!lexer.ReadVarValue(status_eval_.get(), &err))
      Fatal("invalid --status: %s", err.c_str());
    progress_status_format_ = NULL;
  } else {
    progress_status_format_ = getenv("NINJA_STATUS");
    if (!progress_status_format_)
      progress_status_format_ = "[%f/%t] ";
  }
}

void StatusPrinter::EdgeAddedToPlan(const Edge* edge) {
  ++total_edges_;

  // Do we know how long did this edge take last time?
  if (edge->prev_elapsed_time_millis != -1) {
    ++eta_predictable_edges_total_;
    ++eta_predictable_edges_remaining_;
    eta_predictable_cpu_time_total_millis_ += edge->prev_elapsed_time_millis;
    eta_predictable_cpu_time_remaining_millis_ +=
        edge->prev_elapsed_time_millis;
  } else
    ++eta_unpredictable_edges_remaining_;
}

void StatusPrinter::EdgeRemovedFromPlan(const Edge* edge) {
  --total_edges_;

  // Do we know how long did this edge take last time?
  if (edge->prev_elapsed_time_millis != -1) {
    --eta_predictable_edges_total_;
    --eta_predictable_edges_remaining_;
    eta_predictable_cpu_time_total_millis_ -= edge->prev_elapsed_time_millis;
    eta_predictable_cpu_time_remaining_millis_ -=
        edge->prev_elapsed_time_millis;
  } else
    --eta_unpredictable_edges_remaining_;
}

void StatusPrinter::BuildEdgeStarted(Edge* edge,
                                     int64_t start_time_millis) {
  edges_.push_back(edge);
  ++started_edges_;
  edge->sequence_ = started_edges_;
  time_millis_ = start_time_millis;

  if (!multi_line_status_ && (edge->use_console() || printer_.is_smart_terminal()))
    PrintStatus(edge, start_time_millis);

  if (edge->use_console())
    printer_.SetConsoleLocked(true);
}

void StatusPrinter::RecalculateProgressPrediction() {
  time_predicted_percentage_ = 0.0;

  // Sometimes, the previous and actual times may be wildly different.
  // For example, the previous build may have been fully recovered from ccache,
  // so it was blazing fast, while the new build no longer gets hits from ccache
  // for whatever reason, so it actually compiles code, which takes much longer.
  // We should detect such cases, and avoid using "wrong" previous times.

  // Note that we will only use the previous times if there are edges with
  // previous time knowledge remaining.
  bool use_previous_times = eta_predictable_edges_remaining_ &&
                            eta_predictable_cpu_time_remaining_millis_;

  // Iff we have sufficient statistical information for the current run,
  // that is, if we have took at least 15 sec AND finished at least 5% of edges,
  // we can check whether our performance so far matches the previous one.
  if (use_previous_times && total_edges_ && finished_edges_ &&
      (time_millis_ >= 15 * 1e3) &&
      (((double)finished_edges_ / total_edges_) >= 0.05)) {
    // Over the edges we've just run, how long did they take on average?
    double actual_average_cpu_time_millis =
        (double)cpu_time_millis_ / finished_edges_;
    // What is the previous average, for the edges with such knowledge?
    double previous_average_cpu_time_millis =
        (double)eta_predictable_cpu_time_total_millis_ /
        eta_predictable_edges_total_;

    double ratio = std::max(previous_average_cpu_time_millis,
                            actual_average_cpu_time_millis) /
                   std::min(previous_average_cpu_time_millis,
                            actual_average_cpu_time_millis);

    // Let's say that the average times should differ by less than 10x
    use_previous_times = ratio < 10;
  }

  int edges_with_known_runtime = finished_edges_;
  if (use_previous_times)
    edges_with_known_runtime += eta_predictable_edges_remaining_;
  if (edges_with_known_runtime == 0)
    return;

  int edges_with_unknown_runtime = use_previous_times
                                       ? eta_unpredictable_edges_remaining_
                                       : (total_edges_ - finished_edges_);

  // Given the time elapsed on the edges we've just run,
  // and the runtime of the edges for which we know previous runtime,
  // what's the edge's average runtime?
  int64_t edges_known_runtime_total_millis = cpu_time_millis_;
  if (use_previous_times)
    edges_known_runtime_total_millis +=
        eta_predictable_cpu_time_remaining_millis_;

  double average_cpu_time_millis =
      (double)edges_known_runtime_total_millis / edges_with_known_runtime;

  // For the edges for which we do not have the previous runtime,
  // let's assume that their average runtime is the same as for the other edges,
  // and we therefore can predict their remaining runtime.
  double unpredictable_cpu_time_remaining_millis =
      average_cpu_time_millis * edges_with_unknown_runtime;

  // And therefore we can predict the remaining and total runtimes.
  double total_cpu_time_remaining_millis =
      unpredictable_cpu_time_remaining_millis;
  if (use_previous_times)
    total_cpu_time_remaining_millis +=
        eta_predictable_cpu_time_remaining_millis_;
  double total_cpu_time_millis =
      cpu_time_millis_ + total_cpu_time_remaining_millis;
  if (total_cpu_time_millis == 0.0)
    return;

  // After that we can tell how much work we've completed, in time units.
  time_predicted_percentage_ = cpu_time_millis_ / total_cpu_time_millis;
}

void StatusPrinter::BuildEdgeFinished(Edge* edge, int64_t start_time_millis,
                                      int64_t end_time_millis, ExitStatus exit_code,
                                      const string& output) {
  time_millis_ = end_time_millis;
  ++finished_edges_;

  int64_t elapsed = end_time_millis - start_time_millis;
  cpu_time_millis_ += elapsed;

  // Do we know how long did this edge take last time?
  if (edge->prev_elapsed_time_millis != -1) {
    --eta_predictable_edges_remaining_;
    eta_predictable_cpu_time_remaining_millis_ -=
        edge->prev_elapsed_time_millis;
  } else
    --eta_unpredictable_edges_remaining_;

  if (edge->use_console())
    printer_.SetConsoleLocked(false);

  if (config_.verbosity == BuildConfig::QUIET)
    return;

  if (!multi_line_status_ && !edge->use_console())
    PrintStatus(edge, end_time_millis);

  // In multi-line mode, overwrite the display block in place to avoid flicker.
  if (multi_line_status_) {
    // Move cursor to the top of the entire display block.
    int total_up = last_reported_ + (has_kept_line_ ? 1 : 0);
    if (total_up > 0)
      printf("\x1B[%dA", total_up);
    // Print the finished edge as the new kept line.
    PrintStatus(edge, end_time_millis);
    has_kept_line_ = true;
    last_reported_ = 0;
  }

  edges_.remove(edge);

  // If there's error/output, clear the old status lines below the kept line
  // and let the next Report() redraw them after the output.
  if (multi_line_status_ && (exit_code != ExitSuccess || !output.empty())) {
    printf("\x1B[J");
    fflush(stdout);
    has_kept_line_ = false;
  }

  // Print the command that is spewing before printing its output.
  if (exit_code != ExitSuccess) {
    string outputs;
    for (vector<Node*>::const_iterator o = edge->outputs_.begin();
         o != edge->outputs_.end(); ++o)
      outputs += (*o)->path() + " ";

    string failed = "FAILED: [code=" + std::to_string(exit_code) + "] ";
    if (printer_.supports_color()) {
        printer_.PrintOnNewLine("\x1B[31m" + failed + "\x1B[0m" + outputs + "\n");
    } else {
        printer_.PrintOnNewLine(failed + outputs + "\n");
    }
    printer_.PrintOnNewLine(edge->EvaluateCommand() + "\n");
  }

  if (!output.empty()) {
#ifdef _WIN32
    // Fix extra CR being added on Windows, writing out CR CR LF (#773)
    fflush(stdout);  // Begin Windows extra CR fix
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    // ninja sets stdout and stderr of subprocesses to a pipe, to be able to
    // check if the output is empty. Some compilers, e.g. clang, check
    // isatty(stderr) to decide if they should print colored output.
    // To make it possible to use colored output with ninja, subprocesses should
    // be run with a flag that forces them to always print color escape codes.
    // To make sure these escape codes don't show up in a file if ninja's output
    // is piped to a file, ninja strips ansi escape codes again if it's not
    // writing to a |smart_terminal_|.
    // (Launching subprocesses in pseudo ttys doesn't work because there are
    // only a few hundred available on some systems, and ninja can launch
    // thousands of parallel compile commands.)
    if (printer_.supports_color() || output.find('\x1b') == std::string::npos) {
      printer_.PrintOnNewLine(output);
    } else {
      std::string final_output = StripAnsiEscapeCodes(output);
      printer_.PrintOnNewLine(final_output);
    }

#ifdef _WIN32
    fflush(stdout);
    _setmode(_fileno(stdout), _O_TEXT);  // End Windows extra CR fix
#endif
  }

  // Immediately redraw status area to avoid flicker.
  if (multi_line_status_ && has_kept_line_)
    Report();
}

void StatusPrinter::BuildStarted() {
  started_edges_ = 0;
  finished_edges_ = 0;
  edges_.clear();
  last_reported_ = 0;
}

void StatusPrinter::BuildFinished() {
  ClearReport();
  printer_.SetConsoleLocked(false);
  printer_.PrintOnNewLine("");
}

void StatusPrinter::Report() {
  if (!multi_line_status_ || edges_.empty()) {
    if (last_reported_ > 0)
      ClearReport();
    return;
  }

  if (config_.verbosity == BuildConfig::QUIET
      || config_.verbosity == BuildConfig::NO_STATUS_UPDATE)
    return;

  const int64_t now = GetTimeMillis() - start_time_millis_;

  if (last_reported_ > 0)
    printf("\x1B[%dA", last_reported_);

  int lines = 0;
  list<const Edge*>::const_iterator edge = edges_.begin();
  for (; edge != edges_.end(); ++edge) {
    if (lines >= max_status_lines_)
      break;
    PrintStatus(*edge, now);
    ++lines;
  }
  const int hidden_edges = int(edges_.size()) - max_status_lines_;
  if (hidden_edges == 1 && edge != edges_.end()) {
    PrintStatus(*edge, now);
    ++lines;
  } else if (hidden_edges > 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "  ... and %d more", hidden_edges);
    printf("%s\x1B[K\n", buf);
    ++lines;
  }

  // Clear any remaining lines from previous report or overwritten block.
  printf("\x1B[J");

  last_reported_ = lines;
  fflush(stdout);
}

void StatusPrinter::ClearReport() {
  if (last_reported_ > 0) {
    printf("\x1B[%dA\x1B[J", last_reported_);
    fflush(stdout);
    last_reported_ = 0;
  }
}

string StatusPrinter::FormatProgressStatus(const char* progress_status_format,
                                           int64_t time_millis) const {
  string out;
  char buf[32];
  for (const char* s = progress_status_format; *s != '\0'; ++s) {
    if (*s == '%') {
      ++s;
      switch (*s) {
      case '%':
        out.push_back('%');
        break;

        // Started edges.
      case 's':
        snprintf(buf, sizeof(buf), "%d", started_edges_);
        out += buf;
        break;

        // Total edges.
      case 't':
        snprintf(buf, sizeof(buf), "%d", total_edges_);
        out += buf;
        break;

        // Running edges.
      case 'r': {
        snprintf(buf, sizeof(buf), "%d", int(edges_.size()));
        out += buf;
        break;
      }

        // Unstarted edges.
      case 'u':
        snprintf(buf, sizeof(buf), "%d", total_edges_ - started_edges_);
        out += buf;
        break;

        // Finished edges.
      case 'f':
        snprintf(buf, sizeof(buf), "%d", finished_edges_);
        out += buf;
        break;

        // Overall finished edges per second.
      case 'o':
        SnprintfRate(finished_edges_ / (time_millis_ / 1e3), buf, "%.1f");
        out += buf;
        break;

        // Current rate, average over the last '-j' jobs.
      case 'c':
        current_rate_.UpdateRate(finished_edges_, time_millis_);
        SnprintfRate(current_rate_.rate(), buf, "%.1f");
        out += buf;
        break;

        // Percentage of edges completed
      case 'p': {
        int percent = 0;
        if (finished_edges_ != 0 && total_edges_ != 0)
          percent = (100 * finished_edges_) / total_edges_;
        snprintf(buf, sizeof(buf), "%3i%%", percent);
        out += buf;
        break;
      }

#define FORMAT_TIME_HMMSS(t)                                                \
  "%" PRId64 ":%02" PRId64 ":%02" PRId64 "", (t) / 3600, ((t) % 3600) / 60, \
      (t) % 60
#define FORMAT_TIME_MMSS(t) "%02" PRId64 ":%02" PRId64 "", (t) / 60, (t) % 60

        // Wall time
      case 'e':  // elapsed, seconds
      case 'w':  // elapsed, human-readable
      case 'E':  // ETA, seconds
      case 'W':  // ETA, human-readable
      {
        double elapsed_sec = time_millis_ / 1e3;
        double eta_sec = -1;  // To be printed as "?".
        if (time_predicted_percentage_ != 0.0) {
          // So, we know that we've spent time_millis_ wall clock,
          // and that is time_predicted_percentage_ percent.
          // How much time will we need to complete 100%?
          double total_wall_time = time_millis_ / time_predicted_percentage_;
          // Naturally, that gives us the time remaining.
          eta_sec = (total_wall_time - time_millis_) / 1e3;
        }

        const bool print_with_hours =
            elapsed_sec >= 60 * 60 || eta_sec >= 60 * 60;

        double sec = -1;
        switch (*s) {
        case 'e':  // elapsed, seconds
        case 'w':  // elapsed, human-readable
          sec = elapsed_sec;
          break;
        case 'E':  // ETA, seconds
        case 'W':  // ETA, human-readable
          sec = eta_sec;
          break;
        }

        if (sec < 0)
          snprintf(buf, sizeof(buf), "?");
        else {
          switch (*s) {
          case 'e':  // elapsed, seconds
          case 'E':  // ETA, seconds
            snprintf(buf, sizeof(buf), "%.3f", sec);
            break;
          case 'w':  // elapsed, human-readable
          case 'W':  // ETA, human-readable
            if (print_with_hours)
              snprintf(buf, sizeof(buf), FORMAT_TIME_HMMSS((int64_t)sec));
            else
              snprintf(buf, sizeof(buf), FORMAT_TIME_MMSS((int64_t)sec));
            break;
          }
        }
        out += buf;
        break;
      }

      // Percentage of time spent out of the predicted time total
      case 'P': {
        snprintf(buf, sizeof(buf), "%3i%%",
                 (int)(100. * time_predicted_percentage_));
        out += buf;
        break;
      }

      default:
        Fatal("unknown placeholder '%%%c' in $NINJA_STATUS", *s);
        return "";
      }
    } else {
      out.push_back(*s);
    }
  }

  return out;
}

string StatusPrinter::FormatStatusVariable(const string& name) const {
  char buf[32];

  if (name == "started") {
    snprintf(buf, sizeof(buf), "%d", started_edges_);
    return buf;
  }
  if (name == "total") {
    snprintf(buf, sizeof(buf), "%d", total_edges_);
    return buf;
  }
  if (name == "running") {
    snprintf(buf, sizeof(buf), "%d", int(edges_.size()));
    return buf;
  }
  if (name == "remaining") {
    snprintf(buf, sizeof(buf), "%d", total_edges_ - started_edges_);
    return buf;
  }
  if (name == "finished") {
    snprintf(buf, sizeof(buf), "%d", finished_edges_);
    return buf;
  }
  if (name == "rate") {
    SnprintfRate(finished_edges_ / (time_millis_ / 1e3), buf, "%.1f");
    return buf;
  }
  if (name == "current_rate") {
    current_rate_.UpdateRate(finished_edges_, time_millis_);
    SnprintfRate(current_rate_.rate(), buf, "%.1f");
    return buf;
  }
  if (name == "progress") {
    int percent = 0;
    if (finished_edges_ != 0 && total_edges_ != 0)
      percent = (100 * finished_edges_) / total_edges_;
    snprintf(buf, sizeof(buf), "%3i%%", percent);
    return buf;
  }
  if (name == "predicted_progress") {
    snprintf(buf, sizeof(buf), "%3i%%",
             (int)(100. * time_predicted_percentage_));
    return buf;
  }

  if (name == "elapsed" || name == "elapsed_seconds" ||
      name == "eta" || name == "eta_seconds") {
    double elapsed_sec = time_millis_ / 1e3;
    double eta_sec = -1;
    if (time_predicted_percentage_ != 0.0) {
      double total_wall_time = time_millis_ / time_predicted_percentage_;
      eta_sec = (total_wall_time - time_millis_) / 1e3;
    }
    const bool print_with_hours =
        elapsed_sec >= 60 * 60 || eta_sec >= 60 * 60;
    const bool is_eta = (name == "eta" || name == "eta_seconds");
    double sec = is_eta ? eta_sec : elapsed_sec;
    if (sec < 0)
      return "?";
    if (name == "elapsed_seconds" || name == "eta_seconds") {
      snprintf(buf, sizeof(buf), "%.3f", sec);
    } else if (print_with_hours) {
      snprintf(buf, sizeof(buf), FORMAT_TIME_HMMSS((int64_t)sec));
    } else {
      snprintf(buf, sizeof(buf), FORMAT_TIME_MMSS((int64_t)sec));
    }
    return buf;
  }

  Fatal("unknown variable '%s' in --status format", name.c_str());
  return "";
}

void StatusPrinter::PrintStatus(const Edge* edge, int64_t time_millis) {
  if (explanations_) {
    explanations_->ExplainEdge(edge);
  }

  if (config_.verbosity == BuildConfig::QUIET
      || config_.verbosity == BuildConfig::NO_STATUS_UPDATE)
    return;

  RecalculateProgressPrediction();

  bool force_full_command = config_.verbosity == BuildConfig::VERBOSE;

  string to_print = edge->GetBinding("description");
  if (to_print.empty() || force_full_command)
    to_print = edge->GetBinding("command");

  if (status_eval_) {
    StatusFormatEnv env(this);
    to_print = status_eval_->Evaluate(&env) + to_print;
  } else if (multi_line_status_) {
    // In multi-line mode, show each edge's own start order number.
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "[%d/%d] ", edge->sequence_,
             total_edges_);
    to_print = string(prefix) + to_print;
  } else {
    to_print = FormatProgressStatus(progress_status_format_, time_millis)
        + to_print;
  }

  if (multi_line_status_) {
    // Add dot-padding and elapsed time for multi-line display.
    const int ds = static_cast<int>(time_millis - edge->start_time_) / 100;
    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), " %d.%ds", ds / 10, ds % 10);
    const int time_len = static_cast<int>(strlen(tbuf));

    // Clamp visible width to 60 to keep the multiline block compact.
    int width = term_cols_ > 0 ? min(term_cols_, 60) : 60;
    int text_width = width - time_len;
    if (text_width < 0)
      text_width = 0;

    size_t prefix_len = 0;
    if (!status_eval_) {
      char prefix[32];
      snprintf(prefix, sizeof(prefix), "[%d/%d] ", edge->sequence_, total_edges_);
      prefix_len = strlen(prefix);
    }

    if (int(to_print.size()) > text_width) {
      if (text_width > static_cast<int>(prefix_len) + 3) {
        string prefix = to_print.substr(0, prefix_len);
        int title_width = text_width - static_cast<int>(prefix_len);
        string title = to_print.substr(prefix_len);
        if (int(title.size()) > title_width) {
          title = "..." + title.substr(title.size() - (title_width - 3));
        }
        to_print = prefix + title;
      } else if (text_width > 3) {
        to_print = "..." + to_print.substr(to_print.size() - (text_width - 3));
      } else {
        to_print = to_print.substr(to_print.size() - text_width);
      }
    }

    if (int(to_print.size()) < text_width) {
      to_print += '.';
      int pad = text_width - int(to_print.size());
      if (pad > 0)
        to_print += string(pad, '.');
    }
    to_print += tbuf;
    to_print += "\x1B[K";
  }

  printer_.Print(to_print,
                 force_full_command ? LinePrinter::FULL : LinePrinter::ELIDE);
}

void StatusPrinter::NewLine() {
  printer_.PrintOnNewLine("");
}

void StatusPrinter::Warning(const char* msg, ...) {
  va_list ap;
  va_start(ap, msg);
  ::Warning(msg, ap);
  va_end(ap);
}

void StatusPrinter::Error(const char* msg, ...) {
  va_list ap;
  va_start(ap, msg);
  ::Error(msg, ap);
  va_end(ap);
}

void StatusPrinter::Info(const char* msg, ...) {
  va_list ap;
  va_start(ap, msg);
  ::Info(msg, ap);
  va_end(ap);
}
