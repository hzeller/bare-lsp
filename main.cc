
#include "json-rpc-server.h"
#include <unistd.h>

int main() {
  JsonRpcServer server(STDIN_FILENO, std::cout);
  absl::Status status = server.Run();
  if (!status.ok())
    std::cerr << status.message() << std::endl;
  return status.ok() ? 0 : 1;
}
