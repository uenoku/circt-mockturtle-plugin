#ifndef CIRCT_QUERY_TYPES_H
#define CIRCT_QUERY_TYPES_H

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace circt_query {

struct Diagnostic {
  std::string message;
};

template <typename T> struct Result {
  bool ok = false;
  std::string message;
  T value;

  static Result success(T value) {
    Result result;
    result.ok = true;
    result.value = std::move(value);
    return result;
  }

  static Result failure(std::string message) {
    Result result;
    result.ok = false;
    result.message = std::move(message);
    return result;
  }
};

struct Status {
  bool ok = false;
  std::string message;

  static Status success() { return {true, ""}; }
  static Status failure(std::string message) {
    return {false, std::move(message)};
  }
};

enum class PortDirection { Input, Output, InOut };
enum class MatchMode { Wildcard, Fuzzy, Regex };
enum class ObjectKind { Port, Register, MemoryPort, Instance };
enum class EntityKind { Design, Module, Port, Register, MemoryPort, Instance };
enum class ValueKind {
  InputPort,
  OutputPort,
  Register,
  InstanceResult,
  Wire,
  OperationResult,
};
enum class IrStatus { Unknown, Core, Synthesized, PostVerilogEmission };

std::string toString(PortDirection direction);
std::string toString(MatchMode mode);
std::string toString(ObjectKind kind);
std::string toString(EntityKind kind);
std::string toString(ValueKind kind);
std::string toString(IrStatus status);

std::optional<MatchMode> parseMatchMode(const std::string &text);
std::optional<ObjectKind> parseObjectKind(const std::string &text);
std::optional<IrStatus> parseIrStatus(const std::string &text);

struct PortInfo {
  std::string name;
  std::string type;
  PortDirection direction = PortDirection::Input;
  std::map<std::string, std::string> attributes;
};

struct RegisterInfo {
  std::string name;
  std::string type;
  std::string outputValueName;
  std::vector<std::string> dataDrivers;
  std::optional<std::string> clockDriver;
  std::optional<std::string> resetDriver;
  std::optional<std::string> resetValueDriver;
};

struct MemoryPortInfo {
  std::string name;
  std::string type;
  std::string outputValueName;
  std::vector<std::string> dataDrivers;
};

struct InstanceInfo {
  std::string name;
  std::string targetModule;
  std::vector<std::pair<std::string, std::string>> inputBindings;
  std::vector<std::pair<std::string, std::string>> outputBindings;
};

struct ValueInfo {
  std::string name;
  ValueKind kind = ValueKind::OperationResult;
  std::string type;
  std::string location;
  std::string definingOp;
  std::vector<std::string> drivers;
  std::vector<std::string> users;
  std::map<std::string, std::string> attributes;
};

struct ModuleInfo {
  std::string name;
  std::vector<PortInfo> ports;
  std::vector<RegisterInfo> registers;
  std::vector<MemoryPortInfo> memoryPorts;
  std::vector<InstanceInfo> instances;
  std::map<std::string, ValueInfo> values;
  std::map<std::string, std::string> outputDrivers;
  int operationCount = 0;
  int registerCount = 0;
  bool isExternal = false;
};

struct ModuleSummary {
  std::string name;
  size_t portCount = 0;
  size_t instanceCount = 0;
  int operationCount = 0;
  int registerCount = 0;
  bool isExternal = false;
};

struct DesignModel {
  std::map<std::string, ModuleInfo> modules;
  std::vector<std::string> moduleOrder;
  bool hasValueIndex = false;
};

struct DesignRecord {
  std::string id;
  std::string sourcePath;
  std::string selectedTop;
  IrStatus irStatus = IrStatus::Unknown;
};

struct DesignObjectInfo {
  std::string path;
  ObjectKind kind = ObjectKind::Port;
  std::string designId;
  std::string moduleName;
  std::string localName;
  std::string instancePath;
  std::string targetModule;
  std::string type;
  std::optional<PortDirection> direction;
};

struct NameMatch {
  std::string name;
  std::string path;
  EntityKind kind = EntityKind::Module;
  std::string moduleName;
  std::string targetModule;
  int score = 0;
};

struct NameSearchResult {
  std::string designId;
  std::string query;
  MatchMode matchMode = MatchMode::Fuzzy;
  std::vector<NameMatch> matches;
};

} // namespace circt_query

#endif // CIRCT_QUERY_TYPES_H
