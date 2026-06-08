#include "CIRCTQuery/Json.h"
#include "CIRCTQuery/Service.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

#include <optional>
#include <string>

namespace {

namespace cl = llvm::cl;

void printUsageAndExit() {
  llvm::errs() << "usage: circt-query --design <file> [--top <module>] "
                  "<command> [args]\n"
               << "commands:\n"
               << "  designs\n"
               << "  modules\n"
               << "  module <module>\n"
               << "  ports <module>\n"
               << "  instances <module>\n"
               << "  objects [query]\n"
               << "  search <query>\n";
  std::exit(2);
}

template <typename T>
llvm::json::Array serializeArray(const std::vector<T> &values) {
  llvm::json::Array array;
  for (const auto &value : values)
    array.push_back(circt_query::toJson(value));
  return array;
}

} // namespace

int main(int argc, char **argv) {
  llvm::InitLLVM init(argc, argv);

  cl::OptionCategory category("circt-query options");
  cl::opt<std::string> designPath("design", cl::desc("Design MLIR file"),
                                  cl::value_desc("path"), cl::cat(category));
  cl::opt<std::string> status("status", cl::desc("IR status for --design"),
                              cl::value_desc("core|synthesized|post-emission"),
                              cl::init("core"), cl::cat(category));
  cl::opt<std::string> top("top", cl::desc("Selected top module"),
                           cl::value_desc("module"), cl::init(""),
                           cl::cat(category));
  cl::opt<std::string> kind(
      "kind", cl::desc("Object kind filter"),
      cl::value_desc("port|register|memory_port|instance"), cl::init(""),
      cl::cat(category));
  cl::opt<std::string> matchMode("match-mode", cl::desc("Name match mode"),
                                 cl::value_desc("wildcard|fuzzy|regex"),
                                 cl::init("wildcard"), cl::cat(category));
  cl::list<std::string> command(cl::Positional, cl::desc("<command> [args]"),
                                cl::ZeroOrMore, cl::cat(category));
  cl::HideUnrelatedOptions(category);
  cl::ParseCommandLineOptions(argc, argv, "CIRCT structural query tool\n");

  circt_query::QueryService service;
  if (!designPath.empty()) {
    auto parsedStatus = circt_query::parseIrStatus(status);
    if (!parsedStatus) {
      llvm::errs() << "error: invalid --status '" << status << "'\n";
      return 2;
    }
    auto loaded = service.loadDesign({designPath, parsedStatus});
    if (!loaded.ok) {
      llvm::errs() << "error: " << loaded.message << "\n";
      return 2;
    }
    if (!top.empty()) {
      auto set = service.setTop(loaded.value.id, top);
      if (!set.ok) {
        llvm::errs() << "error: " << set.message << "\n";
        return 2;
      }
    }
  }

  if (command.empty())
    printUsageAndExit();

  std::string designId = "design-1";
  llvm::StringRef verb = command[0];
  llvm::json::Value output = nullptr;

  if (verb == "designs") {
    output =
        llvm::json::Object{{"designs", serializeArray(service.listDesigns())}};
  } else if (verb == "modules") {
    auto modules = service.listModules(designId);
    if (!modules.ok) {
      llvm::errs() << "error: " << modules.message << "\n";
      return 2;
    }
    output = llvm::json::Object{{"modules", serializeArray(modules.value)}};
  } else if (verb == "module") {
    if (command.size() < 2)
      printUsageAndExit();
    auto module = service.getModule(designId, command[1]);
    if (!module.ok) {
      llvm::errs() << "error: " << module.message << "\n";
      return 2;
    }
    output = llvm::json::Object{{"module", circt_query::toJson(module.value)}};
  } else if (verb == "ports") {
    if (command.size() < 2)
      printUsageAndExit();
    auto ports = service.getModulePorts(designId, command[1]);
    if (!ports.ok) {
      llvm::errs() << "error: " << ports.message << "\n";
      return 2;
    }
    output = llvm::json::Object{{"ports", serializeArray(ports.value)}};
  } else if (verb == "instances") {
    if (command.size() < 2)
      printUsageAndExit();
    auto instances = service.getModuleInstances(designId, command[1]);
    if (!instances.ok) {
      llvm::errs() << "error: " << instances.message << "\n";
      return 2;
    }
    output = llvm::json::Object{{"instances", serializeArray(instances.value)}};
  } else if (verb == "objects") {
    auto parsedMode = circt_query::parseMatchMode(matchMode);
    if (!parsedMode) {
      llvm::errs() << "error: invalid --match-mode '" << matchMode << "'\n";
      return 2;
    }
    std::optional<circt_query::ObjectKind> parsedKind;
    if (!kind.empty()) {
      parsedKind = circt_query::parseObjectKind(kind);
      if (!parsedKind) {
        llvm::errs() << "error: invalid --kind '" << kind << "'\n";
        return 2;
      }
    }
    std::string query = command.size() >= 2 ? command[1] : "";
    auto objects =
        service.listObjects(designId, query, *parsedMode, parsedKind);
    if (!objects.ok) {
      llvm::errs() << "error: " << objects.message << "\n";
      return 2;
    }
    output = llvm::json::Object{{"objects", serializeArray(objects.value)}};
  } else if (verb == "search") {
    if (command.size() < 2)
      printUsageAndExit();
    auto parsedMode = circt_query::parseMatchMode(matchMode);
    if (!parsedMode) {
      llvm::errs() << "error: invalid --match-mode '" << matchMode << "'\n";
      return 2;
    }
    auto result = service.searchNames(designId, command[1], *parsedMode);
    if (!result.ok) {
      llvm::errs() << "error: " << result.message << "\n";
      return 2;
    }
    output = llvm::json::Object{{"result", circt_query::toJson(result.value)}};
  } else {
    printUsageAndExit();
  }

  llvm::outs() << circt_query::stringifyPretty(output) << "\n";
  return 0;
}
