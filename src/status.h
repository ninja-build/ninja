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

#ifndef NINJA_STATUS_H_
#define NINJA_STATUS_H_

#include <map>
#include <string>

#include "build.h"
#include "line_printer.h"

using namespace std;

/// Abstract interface to object that tracks the status of a build:
/// completion fraction, printing updates.
struct Status {
  virtual void PlanHasTotalEdges(int total) = 0;
  virtual void BuildEdgeStarted(Edge* edge) = 0;
  virtual void BuildEdgeFinished(Edge* edge, bool success, const string& output,
                         int* start_time, int* end_time) = 0;
  virtual void BuildLoadDyndeps() = 0;
  virtual void BuildStarted() = 0;
  virtual void BuildFinished() = 0;

  enum EdgeStatus {
    kEdgeStarted,
    kEdgeFinished,
  };
};

/// Implementation of the BuildStatus interface that prints the status as
/// human-readable strings to stdout
struct StatusPrinter : Status {
  explicit StatusPrinter(const BuildConfig& config);

  virtual void PlanHasTotalEdges(int total);
  virtual void BuildEdgeStarted(Edge* edge);
  virtual void BuildEdgeFinished(Edge* edge, bool success, const string& output,
                         int* start_time, int* end_time);
  virtual void BuildLoadDyndeps();
  virtual void BuildStarted();
  virtual void BuildFinished();

  virtual ~StatusPrinter() { }

  /// Format the progress status string by replacing the placeholders.
  /// See the user manual for more information about the available
  /// placeholders.
  /// @param progress_status_format The format of the progress status.
  /// @param status The status of the edge.
  string FormatProgressStatus(const char* progress_status_format,
                              EdgeStatus status) const;

 private:
  void PrintStatus(Edge* edge, EdgeStatus status);

  const BuildConfig& config_;

  /// Time the build started.
  int64_t start_time_millis_;

  int started_edges_, finished_edges_, total_edges_;

  /// Map of running edge to time the edge started running.
  typedef map<Edge*, int> RunningEdgeMap;
  RunningEdgeMap running_edges_;

  /// Prints progress output.
  LinePrinter printer_;

  /// The custom progress status format to use.
  const char* progress_status_format_;

  template<size_t S>
  void SnprintfRate(double rate, char(&buf)[S], const char* format) const {
    if (rate == -1)
      snprintf(buf, S, "?");
    else
      snprintf(buf, S, format, rate);
  }

  struct RateInfo {
    RateInfo() : rate_(-1) {}

    void Restart() { stopwatch_.Restart(); }
    double Elapsed() const { return stopwatch_.Elapsed(); }
    double rate() { return rate_; }

    void UpdateRate(int edges) {
      if (edges && stopwatch_.Elapsed())
        rate_ = edges / stopwatch_.Elapsed();
    }

  private:
    double rate_;
    Stopwatch stopwatch_;
  };

  struct SlidingRateInfo {
    SlidingRateInfo(int n) : rate_(-1), N(n), last_update_(-1) {}

    void Restart() { stopwatch_.Restart(); }
    double rate() { return rate_; }

    void UpdateRate(int update_hint) {
      if (update_hint == last_update_)
        return;
      last_update_ = update_hint;

      if (times_.size() == N)
        times_.pop();
      times_.push(stopwatch_.Elapsed());
      if (times_.back() != times_.front())
        rate_ = times_.size() / (times_.back() - times_.front());
    }

  private:
    double rate_;
    Stopwatch stopwatch_;
    const size_t N;
    queue<double> times_;
    int last_update_;
  };

  mutable RateInfo overall_rate_;
  mutable SlidingRateInfo current_rate_;
};

#endif  // NINJA_STATUS_H_
