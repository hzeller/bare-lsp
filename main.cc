
#include "json-rpc-server.h"
#include <unistd.h>

int main() {
  JsonRpcServer server(STDIN_FILENO, std::cout);
  absl::Status status = server.Run();
  if (!status.ok())
    std::cerr << status.message() << std::endl;

  fprintf(stderr, "-- method call count stats --\n");
  for (const auto &stats : server.GetMethodCallStats()) {
    fprintf(stderr, "%-32s %5d\n", stats.first.c_str(), stats.second);
  }
  return status.ok() ? 0 : 1;
}
