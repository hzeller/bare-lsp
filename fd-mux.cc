// Copyright 2014 Henner Zeller <h.zeller@acm.org>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fd-mux.h"

#include <sys/select.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

bool FDMultiplexer::RunOnReadable(int fd, const Handler &handler) {
  return read_handlers_.insert({fd, handler}).second;
}

void FDMultiplexer::RunOnIdle(const Handler &handler) {
  idle_handlers_.push_back(handler);
}

static void CallHandlers(fd_set *to_call_fd_set, int available_fds,
                         std::map<int, FDMultiplexer::Handler> *handlers) {
  for (auto it = handlers->begin(); available_fds && it != handlers->end();) {
    bool keep_handler = true;
    if (FD_ISSET(it->first, to_call_fd_set)) {
      --available_fds;
      keep_handler = it->second();
    }
    it = keep_handler ? std::next(it) : handlers->erase(it);
  }
}

bool FDMultiplexer::SingleCycle(unsigned int timeout_ms) {
  fd_set read_fds;

  struct timeval timeout;
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  int maxfd = -1;
  FD_ZERO(&read_fds);

  for (const auto &it : read_handlers_) {
    maxfd = std::max(maxfd, it.first);
    FD_SET(it.first, &read_fds);
  }

  if (maxfd < 0) {
    // file descriptors only can be registred from within handlers
    // or before running the Loop(). So if no descriptors are left,
    // there is no chance for any to re-appear, so we can exit.
    return false;
  }

  int fds_ready = select(maxfd + 1, &read_fds, nullptr, nullptr, &timeout);
  if (fds_ready < 0) {
    perror("select() failed");
    return false;
  }

  if (fds_ready == 0) {  // No FDs ready: timeout situation.
    for (auto it = idle_handlers_.begin(); it != idle_handlers_.end(); /**/) {
      const bool keep_handler = (*it)();
      it = keep_handler ? std::next(it) : idle_handlers_.erase(it);
    }
    return true;
  }

  CallHandlers(&read_fds, fds_ready, &read_handlers_);

  return true;
}

void FDMultiplexer::Loop() {
  const unsigned timeout = idle_ms_;

  while (SingleCycle(timeout)) {
  }
}
