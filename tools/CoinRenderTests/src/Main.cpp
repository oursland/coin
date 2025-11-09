#include "CompareCommand.h"
#include "RunCommand.h"
#include "SnapshotCommand.h"

#include <iostream>
#include <string>

namespace {

void print_usage() {
  std::cout << "Usage: CoinRenderTests <subcommand> [args]\n"
            << "Subcommands:\n"
            << "  snapshot   Render a scene or spec to PNG.\n"
            << "  compare    Compare two images with tolerances.\n"
            << "  run        Run the full spec-based suite.\n";
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  const std::string cmd = argv[1];
  if (cmd == "snapshot") {
    return CoinRenderTests::RunSnapshotCommand(argc - 1, argv + 1);
  }
  if (cmd == "compare") {
    return CoinRenderTests::RunCompareCommand(argc - 1, argv + 1);
  }
  if (cmd == "run") {
    return CoinRenderTests::RunRunCommand(argc - 1, argv + 1);
  }

  std::cerr << "Unknown subcommand: " << cmd << '\n';
  print_usage();
  return 1;
}
