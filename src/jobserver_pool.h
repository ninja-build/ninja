// Copyright 2025 Google Inc. All Rights Reserved.
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

#ifndef NINJA_JOBSERVER_POOL_H_
#define NINJA_JOBSERVER_POOL_H_

#include <cstddef>
#include <memory>
#include <string>

/// JobserverPool implements a jobserver pool of job slots according
/// to the GNU Make protocol. Usage is the following:
///
/// - Use Create() method to create new instances.
///
/// - Retrieve the value of the MAKEFLAGS environment variable, and
///   ensure it is passed to each client.
///
class JobserverPool {
 public:
  /// Destructor.
  virtual ~JobserverPool() {}

  /// Create new instance to use |num_slots| job slots, using a specific
  /// implementation mode. On failure, set |*error| and return null.
  ///
  /// It is an error to use a value of |num_slots| that is <= 1.
  static std::unique_ptr<JobserverPool> Create(size_t num_job_slots,
                                               std::string* error);

  /// Return the value of the MAKEFLAGS variable, corresponding to this
  /// instance, to pass to sub-processes.
  virtual std::string GetEnvMakeFlagsValue() const = 0;
};

#endif  // NINJA_JOBSERVER_POOL_H_
