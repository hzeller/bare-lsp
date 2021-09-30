
#include "json-rpc-server.h"
#include <unistd.h>
#include <sys/uio.h>

static void StringViewWritev(int fd,
                             std::initializer_list<absl::string_view> content) {
  struct iovec iov[content.size()];
  struct iovec *iov_it = iov;
  for (const auto &s : content) {
    iov_it->iov_base = (char*)s.data();
    iov_it->iov_len = s.size();
    iov_it++;
  }
  writev(fd, iov, content.size());
}

int main() {
  JsonRpcServer server([](std::initializer_list<absl::string_view> content) {
    StringViewWritev(STDOUT_FILENO, content);
  });

  absl::Status status = absl::OkStatus();
  while (status.ok()) {
    status = server.ProcessInput([](char *buf, int size) -> int {
      return read(STDIN_FILENO, buf, size);
    });
  }
  if (!status.ok())
    std::cerr << status.message() << std::endl;

  fprintf(stderr, "--------------- Statistic Counters Stats ---------------\n");
  int longest = 0;
  for (const auto &stats : server.GetStatCounters()) {
    longest = std::max(longest, (int)stats.first.length());
  }
  for (const auto &stats : server.GetStatCounters()) {
    fprintf(stderr, "%*s %9d\n", longest, stats.first.c_str(), stats.second);
  }
  return status.ok() ? 0 : 1;
}
