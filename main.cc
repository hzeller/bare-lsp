
#include "json-rpc-server.h"
#include <unistd.h>

int main() {
  JsonRpcServer server(STDIN_FILENO, std::cout);
  absl::Status status = server.Run();
  if (!status.ok())
    std::cerr << status.message() << std::endl;

  fprintf(stderr, "--------------- Statistic Counters Stats ---------------\n");
  int longest = 0;
  for (const auto &stats : server.GetStatCounters()) {
    longest = std::max(longest, (int)stats.first.length());
  }
  for (const auto &stats : server.GetStatCounters()) {
    fprintf(stderr, "%*s %7d\n", longest, stats.first.c_str(), stats.second);
  }
  return status.ok() ? 0 : 1;
}
