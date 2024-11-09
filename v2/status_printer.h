#pragma once

#include <boost/asio.hpp>
#include <map>

#include "../src/line_printer.h"
#include "../src/status.h"

class StatusPrinter : public Status {
  void EdgeAddedToPlan(const Edge* edge) override;
  void EdgeRemovedFromPlan(const Edge* edge) override;
  void BuildEdgeStarted(const Edge* edge, int64_t start_time_millis) override;
  void BuildEdgeFinished(Edge* edge, int64_t start_time_millis,
                         int64_t end_time_millis, bool success,
                         const std::string& output) override;
  void BuildStarted() override;
  void BuildFinished() override;

  void SetExplanations(Explanations*) override;

  void Info(const char* msg, ...) override;
  void Warning(const char* msg, ...) override;
  void Error(const char* msg, ...) override;

  void Print(FILE* stream, const char* msg, va_list ap);

  void StartTimer();
  void TimerCallback(boost::system::error_code);
  void PrintStatus();

  uint32_t total_edges_ = 0;
  uint32_t finished_edges_ = 0;
  /// when was this Edge started?
  std::map<const Edge*, std::chrono::milliseconds> running_edges_;

  /// when did we first print this Edge?
  std::pair<const Edge*, std::chrono::milliseconds> last_printed_edge_{ nullptr, 0 };

  uint32_t failed_edges_ = 0;

  LinePrinter printer_;
  bool console_locked_ = false;
  boost::asio::deadline_timer timer_;

public:
  StatusPrinter();
};
