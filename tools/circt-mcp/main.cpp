#include "CIRCTQuery/Json.h"
#include "CIRCTQuery/Mcp.h"
#include "CIRCTQuery/Service.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

namespace cl = llvm::cl;

} // namespace

int main(int argc, char **argv) {
  llvm::InitLLVM init(argc, argv);

  cl::OptionCategory category("circt-mcp options");
  cl::list<std::string> designPaths(
      "design", cl::desc("Load a design before starting the MCP server"),
      cl::value_desc("path"), cl::ZeroOrMore, cl::cat(category));
  cl::opt<std::string> status("status",
                              cl::desc("IR status for --design inputs"),
                              cl::value_desc("core|synthesized|post-emission"),
                              cl::init("core"), cl::cat(category));
  cl::opt<bool> dumpTools("dump-tools",
                          cl::desc("Print MCP tool definitions and exit"),
                          cl::init(false), cl::cat(category));
  cl::HideUnrelatedOptions(category);
  cl::ParseCommandLineOptions(argc, argv, "CIRCT query MCP server\n");

  circt_query::QueryService service;
  auto parsedStatus = circt_query::parseIrStatus(status);
  if (!parsedStatus) {
    llvm::errs() << "error: invalid --status '" << status << "'\n";
    return 2;
  }
  for (const auto &path : designPaths) {
    auto loaded = service.loadDesign({path, parsedStatus});
    if (!loaded.ok) {
      llvm::errs() << "error: " << loaded.message << "\n";
      return 2;
    }
  }

  circt_query::McpToolDispatcher dispatcher(&service);
  if (dumpTools) {
    llvm::json::Array tools;
    for (const auto &tool : dispatcher.listTools()) {
      llvm::json::Object dumped{{"name", tool.name},
                                {"description", tool.description}};
      dumped["inputSchema"] =
          llvm::json::Value(llvm::json::Object(tool.inputSchema));
      tools.push_back(std::move(dumped));
    }
    llvm::outs() << circt_query::stringifyPretty(
                        llvm::json::Object{{"tools", std::move(tools)}})
                 << "\n";
    return 0;
  }

  circt_query::StdioMcpServer server(&dispatcher);
  return server.run(std::cin, std::cout);
}
