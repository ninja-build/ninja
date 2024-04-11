// Copyright 2011 Google Inc. All Rights Reserved.
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

#include "metrics.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <chrono>

#include "util.h"

using namespace std;

Metrics* g_metrics = NULL;

namespace {

/// Compute a platform-specific high-res timer value that fits into an int64.
int64_t HighResTimer() {
  auto now = chrono::steady_clock::now();
  return chrono::duration_cast<chrono::steady_clock::duration>(
             now.time_since_epoch())
      .count();
}

constexpr int64_t GetFrequency() {
  // If numerator isn't 1 then we lose precision and that will need to be
  // assessed.
  static_assert(std::chrono::steady_clock::period::num == 1,
                "Numerator must be 1");
  return std::chrono::steady_clock::period::den /
         std::chrono::steady_clock::period::num;
}

int64_t TimerToMicros(int64_t dt) {
  // dt is in ticks.  We want microseconds.
  return chrono::duration_cast<chrono::microseconds>(
             std::chrono::steady_clock::duration{ dt })
      .count();
}

int64_t TimerToMicros(double dt) {
  // dt is in ticks.  We want microseconds.
  using DoubleSteadyClock =
      std::chrono::duration<double, std::chrono::steady_clock::period>;
  return chrono::duration_cast<chrono::microseconds>(DoubleSteadyClock{ dt })
      .count();
}

}  // anonymous namespace

ScopedMetric::ScopedMetric(Metric* metric) {
  metric_ = metric;
  if (!metric_)
    return;
  start_ = HighResTimer();
}
ScopedMetric::~ScopedMetric() {
  if (!metric_)
    return;
  metric_->count++;
  // Leave in the timer's natural frequency to avoid paying the conversion cost
  // on every measurement.
  int64_t dt = HighResTimer() - start_;
  metric_->sum += dt;
}

Metric* Metrics::NewMetric(const string& name) {
  Metric* metric = new Metric;
  metric->name = name;
  metric->count = 0;
  metric->sum = 0;
  metrics_.push_back(metric);
  return metric;
}

void Metrics::Report() {
  int width = 0;
  for (vector<Metric*>::iterator i = metrics_.begin();
       i != metrics_.end(); ++i) {
    width = max((int)(*i)->name.size(), width);
  }

  printf("%-*s\t%-6s\t%-9s\t%s\n", width,
         "metric", "count", "avg (us)", "total (ms)");
  for (vector<Metric*>::iterator i = metrics_.begin();
       i != metrics_.end(); ++i) {
    Metric* metric = *i;
    uint64_t micros = TimerToMicros(metric->sum);
    double total = micros / (double)1000;
    double avg = micros / (double)metric->count;
    printf("%-*s\t%-6d\t%-8.1f\t%.1f\n", width, metric->name.c_str(),
           metric->count, avg, total);
  }
}

double Stopwatch::Elapsed() const {
  // Convert to micros after converting to double to minimize error.
  return 1e-6 * TimerToMicros(static_cast<double>(NowRaw() - started_));
}

uint64_t Stopwatch::NowRaw() const {
  return HighResTimer();
}

int64_t GetTimeMillis() {
  return TimerToMicros(HighResTimer()) / 1000;
}
