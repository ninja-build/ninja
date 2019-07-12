// Copyright 2019 Google Inc. All Rights Reserved.
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

#include "status.h"

#include <cassert>
#include <errno.h>
#include <stdlib.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "debug_flags.h"

StatusPrinter::StatusPrinter(const BuildConfig& config)
    : config_(config),
      started_edges_(0), finished_edges_(0), total_edges_(0), running_edges_(0),
      time_millis_(0), progress_status_format_(NULL),
      current_rate_(config.parallelism) {

  // Don't do anything fancy in verbose mode.
  if (config_.verbosity != BuildConfig::NORMAL)
    printer_.set_smart_terminal(false);

  progress_status_format_ = getenv("NINJA_STATUS");
  if (!progress_status_format_)
    progress_status_format_ = "[%f/%t] ";
}

void StatusPrinter::PlanHasTotalEdges(int total) {
  total_edges_ = total;
}

void StatusPrinter::BuildEdgeStarted(Edge* edge, int64_t start_time_millis) {
  assert(running_edges_.find(edge) == running_edges_.end());
  ++started_edges_;
  time_millis_ = start_time_millis;

  if (edge->use_console() || printer_.is_smart_terminal())
    PrintStatus(edge, start_time_millis);

  if (edge->use_console())
    printer_.SetConsoleLocked(true);
}

void StatusPrinter::BuildEdgeFinished(Edge* edge,
                                    int64_t end_time_millis,
                                    const CommandRunner::Result* result) {
  time_millis_ = end_time_millis;
  ++finished_edges_;

  if (edge->use_console())
    printer_.SetConsoleLocked(false);

  if (config_.verbosity == BuildConfig::QUIET)
    return;

  if (!edge->use_console())
    PrintStatus(edge, end_time_millis);

  // Update running_edges_ after PrintStatus so that the number of running
  // edges doesn't oscillate between config.parallelism_ and
  // config.parallelism_ - 1.
  --running_edges_;

  // Print the command that is spewing before printing its output.
  if (!result->success()) {
    string outputs;
    for (vector<Node*>::const_iterator o = edge->outputs_.begin();
         o != edge->outputs_.end(); ++o)
      outputs += (*o)->path() + " ";

    printer_.PrintOnNewLine("FAILED: " + outputs + "\n");
    printer_.PrintOnNewLine(edge->EvaluateCommand() + "\n");
  }

  if (!result->output.empty()) {
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
    string final_output;
    if (!printer_.supports_color())
      final_output = StripAnsiEscapeCodes(result->output);
    else
      final_output = result->output;

#ifdef _WIN32
    // Fix extra CR being added on Windows, writing out CR CR LF (#773)
    _setmode(_fileno(stdout), _O_BINARY);  // Begin Windows extra CR fix
#endif

    printer_.PrintOnNewLine(final_output);

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_TEXT);  // End Windows extra CR fix
#endif
  }
}

void StatusPrinter::BuildLoadDyndeps() {
  // The DependencyScan calls EXPLAIN() to print lines explaining why
  // it considers a portion of the graph to be out of date.  Normally
  // this is done before the build starts, but our caller is about to
  // load a dyndep file during the build.  Doing so may generate more
  // exlanation lines (via fprintf directly to stderr), but in an
  // interactive console the cursor is currently at the end of a status
  // line.  Start a new line so that the first explanation does not
  // append to the status line.  After the explanations are done a
  // new build status line will appear.
  if (g_explaining)
    printer_.PrintOnNewLine("");
}

void StatusPrinter::BuildStarted() {
  started_edges_ = 0;
  finished_edges_ = 0;
  running_edges_ = 0;
}

void StatusPrinter::BuildFinished() {
  printer_.SetConsoleLocked(false);
  printer_.PrintOnNewLine("");
}

string StatusPrinter::FormatProgressStatus(
    const char* progress_status_format, int64_t time) const {
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
      case 'r':
        snprintf(buf, sizeof(buf), "%d", running_edges_);
        out += buf;
        break;

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

        // Percentage
      case 'p': {
        int percent = (100 * finished_edges_) / total_edges_;
        snprintf(buf, sizeof(buf), "%3i%%", percent);
        out += buf;
        break;
      }

      case 'e': {
        snprintf(buf, sizeof(buf), "%.3f", time_millis_ / 1e3);
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

void StatusPrinter::PrintStatus(Edge* edge, int64_t time) {
  if (config_.verbosity == BuildConfig::QUIET)
    return;

  bool force_full_command = config_.verbosity == BuildConfig::VERBOSE;

  string to_print = edge->GetBinding("description");
  if (to_print.empty() || force_full_command)
    to_print = edge->GetBinding("command");

  to_print = FormatProgressStatus(progress_status_format_, time) + to_print;

  printer_.Print(to_print,
                 force_full_command ? LinePrinter::FULL : LinePrinter::ELIDE);
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

#ifndef _WIN32

#include <unistd.h>

#include "frontend.pb.h"
#include "proto.h"

StatusSerializer::StatusSerializer(const BuildConfig& config) :
    config_(config), subprocess_(NULL), total_edges_(0) {
  int output_pipe[2];
  if (pipe(output_pipe) < 0)
    Fatal("pipe: %s", strerror(errno));
  SetCloseOnExec(output_pipe[1]);

  f_ = fdopen(output_pipe[1], "wb");
  setvbuf(f_, NULL, _IONBF, 0);
  filebuf_ = new ofilebuf(f_);
  out_ = new std::ostream(filebuf_);

  // Launch the frontend as a subprocess with write-end of the pipe as fd 3
  subprocess_ = subprocess_set_.Add(config.frontend, /*use_console=*/true,
                                    output_pipe[0]);
  close(output_pipe[0]);
}

StatusSerializer::~StatusSerializer() {
  delete out_;
  delete filebuf_;
  fclose(f_);
  subprocess_->Finish();
  subprocess_set_.Clear();
}

void StatusSerializer::Send() {
  // Send the proto as a length-delimited message
  WriteVarint32NoTag(out_, proto_.ByteSizeLong());
  proto_.SerializeToOstream(out_);
  proto_.Clear();
  out_->flush();
}

void StatusSerializer::PlanHasTotalEdges(int total) {
  if (total_edges_ != total) {
    total_edges_ = total;
    ninja::Status::TotalEdges *total_edges = proto_.mutable_total_edges();
    total_edges->set_total_edges(total_edges_);
    Send();
  }
}

void StatusSerializer::BuildEdgeStarted(Edge* edge, int64_t start_time_millis) {
  ninja::Status::EdgeStarted* edge_started = proto_.mutable_edge_started();

  edge_started->set_id(edge->id_);
  edge_started->set_start_time(start_time_millis);

  edge_started->mutable_inputs()->reserve(edge->inputs_.size());
  for (vector<Node*>::iterator it = edge->inputs_.begin();
       it != edge->inputs_.end(); ++it) {
    edge_started->add_inputs((*it)->path());
  }

  edge_started->mutable_outputs()->reserve(edge->inputs_.size());
  for (vector<Node*>::iterator it = edge->outputs_.begin();
       it != edge->outputs_.end(); ++it) {
    edge_started->add_outputs((*it)->path());
  }

  edge_started->set_desc(edge->GetBinding("description"));

  edge_started->set_command(edge->GetBinding("command"));

  edge_started->set_console(edge->use_console());

  Send();
}

void StatusSerializer::BuildEdgeFinished(Edge* edge, int64_t end_time_millis,
                                         const CommandRunner::Result* result) {
  ninja::Status::EdgeFinished* edge_finished = proto_.mutable_edge_finished();

  edge_finished->set_id(edge->id_);
  edge_finished->set_end_time(end_time_millis);
  edge_finished->set_status(result->status);
  edge_finished->set_output(result->output);

  Send();
}

void StatusSerializer::BuildLoadDyndeps() {
}

void StatusSerializer::BuildStarted() {
  ninja::Status::BuildStarted* build_started = proto_.mutable_build_started();

  build_started->set_parallelism(config_.parallelism);
  build_started->set_verbose((config_.verbosity == BuildConfig::VERBOSE));

  Send();
}

void StatusSerializer::BuildFinished() {
  proto_.mutable_build_finished();
  Send();
}

void StatusSerializer::Message(ninja::Status::Message::Level level,
                               const char* msg, va_list ap) {
  va_list ap2;
  va_copy(ap2, ap);

  int len = vsnprintf(NULL, 0, msg, ap2);
  if (len < 0) {
    Fatal("vsnprintf failed");
  }

  va_end(ap2);

  string buf;
  buf.resize(len + 1);

  len = vsnprintf(&buf[0], len + 1, msg, ap);
  buf.resize(len);

  ninja::Status::Message* message = proto_.mutable_message();

  message->set_level(level);
  message->set_message(buf);

  Send();
}

void StatusSerializer::Info(const char* msg, ...) {
  va_list ap;
  va_start(ap, msg);
  Message(ninja::Status::Message::INFO, msg, ap);
  va_end(ap);
}

void StatusSerializer::Warning(const char* msg, ...) {
  va_list ap;
  va_start(ap, msg);
  Message(ninja::Status::Message::WARNING, msg, ap);
  va_end(ap);
}

void StatusSerializer::Error(const char* msg, ...) {
  va_list ap;
  va_start(ap, msg);
  Message(ninja::Status::Message::ERROR, msg, ap);
  va_end(ap);
}
#endif // !_WIN32
