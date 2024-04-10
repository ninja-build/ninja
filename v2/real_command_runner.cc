// Copyright 2024 Google Inc. All Rights Reserved.
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
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/process/v2.hpp>
#include <cassert>

#include "../src/build.h"

namespace bp = boost::process::v2;

boost::asio::io_context gContext;

class RealCommandRunner : public CommandRunner {
  const BuildConfig& config_;
  struct Subprocess {
    bp::process process;
    Edge* edge;
    std::optional<std::string> output;
  };
  std::vector<std::unique_ptr<Subprocess>> running_;
  std::vector<std::unique_ptr<Subprocess>> finished_;
  std::vector<std::unique_ptr<Subprocess>> graveyard_;  // TODO: Shouldn't exist

  size_t CanRunMore() const override {
    int capacity = config_.parallelism - static_cast<int>(running_.size());
    assert(capacity >= 0);
    return capacity;
  }

  bool StartCommand(Edge* edge) override {
    std::unique_ptr<Subprocess> subprocess;
    if (edge->use_console()) {
      subprocess = std::make_unique<Subprocess>(Subprocess{
          bp::process(gContext, "/bin/sh", { "-c", edge->EvaluateCommand() }),
          edge, std::string{} });
    } else {
      auto pipe_stdout = std::make_unique<boost::asio::readable_pipe>(gContext);
      auto pipe_stderr = std::make_unique<boost::asio::readable_pipe>(gContext);
      subprocess = std::make_unique<Subprocess>(Subprocess{
          bp::process(gContext, "/bin/sh", { "-c", edge->EvaluateCommand() },
                      bp::process_stdio{ nullptr, *pipe_stdout, *pipe_stderr }),
          edge, std::string{} });
      for (auto* pipe : { &pipe_stdout, &pipe_stderr }) {
        boost::asio::co_spawn(
            gContext,
            [pipe = std::move(*pipe),
             output = &subprocess->output]() -> boost::asio::awaitable<void> {
              while (true) {
                std::array<char, 1024> buf;
                size_t len = co_await pipe->async_read_some(
                    boost::asio::buffer(buf), boost::asio::use_awaitable);
                if (len == 0 && !pipe->is_open()) {
                  co_return;
                }
                assert(*output);
                (*output)->append(buf.data(), len);
              }
            },
            boost::asio::detached);
      }
    }
    if (!subprocess->process.is_open()) {
      return false;
    }
    subprocess->process.async_wait(
        [this, p = subprocess.get()](bp::error_code ec, int exit_code) {
          assert(!ec);
          auto it = std::ranges::find_if(
              running_, [p](const auto& up) { return up.get() == p; });
          assert(it != running_.end());
          finished_.emplace_back(std::move(*it));
          running_.erase(it);
        });
    running_.emplace_back(std::move(subprocess));
    return true;
  }

  bool WaitForCommand(Result* result) override {
    while (!running_.empty() && finished_.empty()) {
      gContext.run_one();
    }
    if (finished_.empty()) {
      return false;
    }
    if (finished_.back()->process.exit_code() == 0) {
      result->status = ExitSuccess;
    } else {
      result->status = ExitFailure;
    }
    result->edge = finished_.back()->edge;
    if (finished_.back()->output) {
      result->output = std::move(*finished_.back()->output);
      finished_.back()->output = std::nullopt;
    }
    graveyard_.emplace_back(std::move(finished_.back()));
    finished_.pop_back();
    return true;
  }

  std::vector<Edge*> GetActiveEdges() override {
    std::vector<Edge*> edges;
    for (const auto& subprocess : running_) {
      edges.push_back(subprocess->edge);
    }
    return edges;
  }

  void Abort() override {
    for (const auto& subprocess : running_) {
      if (!subprocess->edge->use_console()) {
        subprocess->process.interrupt();
      }
    }
    while (!running_.empty()) {
      gContext.run_one();
    }
    running_.clear();
  }

 public:
  RealCommandRunner(const BuildConfig& config) : config_(config) {}
};

CommandRunner* CommandRunner::factory(const BuildConfig& config) {
  return new RealCommandRunner(config);
}
