
#include "json-rpc-server.h"
#include <unistd.h>

int main() {
  JsonRpcServer server(std::cout);

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
