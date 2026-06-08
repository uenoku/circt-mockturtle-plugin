#include "CIRCTQuery/Json.h"

#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

namespace circt_query {
namespace {

llvm::json::Object toJsonObject(const std::map<std::string, std::string> &map) {
  llvm::json::Object object;
  for (const auto &[key, value] : map)
    object[key] = value;
  return object;
}

llvm::json::Array toJsonArray(const std::vector<std::string> &values) {
  llvm::json::Array array;
  for (const auto &value : values)
    array.push_back(value);
  return array;
}

llvm::json::Array
toJsonBindings(const std::vector<std::pair<std::string, std::string>> &values) {
  llvm::json::Array array;
  for (const auto &[port, value] : values)
    array.push_back(llvm::json::Object{{"port", port}, {"value", value}});
  return array;
}

} // namespace

llvm::json::Value toJson(const PortInfo &port) {
  return llvm::json::Object{{"name", port.name},
                            {"type", port.type},
                            {"direction", toString(port.direction)},
                            {"attributes", toJsonObject(port.attributes)}};
}

llvm::json::Value toJson(const RegisterInfo &reg) {
  llvm::json::Object object{{"name", reg.name},
                            {"type", reg.type},
                            {"output_value", reg.outputValueName},
                            {"data_drivers", toJsonArray(reg.dataDrivers)}};
  if (reg.clockDriver)
    object["clock_driver"] = *reg.clockDriver;
  if (reg.resetDriver)
    object["reset_driver"] = *reg.resetDriver;
  if (reg.resetValueDriver)
    object["reset_value_driver"] = *reg.resetValueDriver;
  return object;
}

llvm::json::Value toJson(const MemoryPortInfo &port) {
  return llvm::json::Object{{"name", port.name},
                            {"type", port.type},
                            {"output_value", port.outputValueName},
                            {"data_drivers", toJsonArray(port.dataDrivers)}};
}

llvm::json::Value toJson(const InstanceInfo &instance) {
  return llvm::json::Object{
      {"name", instance.name},
      {"target_module", instance.targetModule},
      {"input_bindings", toJsonBindings(instance.inputBindings)},
      {"output_bindings", toJsonBindings(instance.outputBindings)}};
}

llvm::json::Value toJson(const ValueInfo &value) {
  return llvm::json::Object{{"name", value.name},
                            {"kind", toString(value.kind)},
                            {"type", value.type},
                            {"location", value.location},
                            {"defining_op", value.definingOp},
                            {"drivers", toJsonArray(value.drivers)},
                            {"users", toJsonArray(value.users)},
                            {"attributes", toJsonObject(value.attributes)}};
}

llvm::json::Value toJson(const ModuleInfo &module) {
  llvm::json::Array ports;
  for (const auto &port : module.ports)
    ports.push_back(toJson(port));
  llvm::json::Array registers;
  for (const auto &reg : module.registers)
    registers.push_back(toJson(reg));
  llvm::json::Array memoryPorts;
  for (const auto &port : module.memoryPorts)
    memoryPorts.push_back(toJson(port));
  llvm::json::Array instances;
  for (const auto &instance : module.instances)
    instances.push_back(toJson(instance));
  llvm::json::Array values;
  for (const auto &[name, value] : module.values)
    values.push_back(toJson(value));

  return llvm::json::Object{
      {"name", module.name},
      {"is_external", module.isExternal},
      {"operation_count", module.operationCount},
      {"register_count", module.registerCount},
      {"ports", std::move(ports)},
      {"registers", std::move(registers)},
      {"memory_ports", std::move(memoryPorts)},
      {"instances", std::move(instances)},
      {"values", std::move(values)},
      {"output_drivers", toJsonObject(module.outputDrivers)}};
}

llvm::json::Value toJson(const ModuleSummary &module) {
  return llvm::json::Object{
      {"name", module.name},
      {"is_external", module.isExternal},
      {"port_count", static_cast<int64_t>(module.portCount)},
      {"instance_count", static_cast<int64_t>(module.instanceCount)},
      {"operation_count", module.operationCount},
      {"register_count", module.registerCount}};
}

llvm::json::Value toJson(const DesignRecord &design) {
  llvm::json::Object object{{"design_id", design.id},
                            {"source_path", design.sourcePath},
                            {"ir_status", toString(design.irStatus)}};
  if (!design.selectedTop.empty())
    object["selected_top"] = design.selectedTop;
  return object;
}

llvm::json::Value toJson(const DesignObjectInfo &object) {
  llvm::json::Object result{{"path", object.path},
                            {"kind", toString(object.kind)},
                            {"design_id", object.designId},
                            {"module", object.moduleName},
                            {"name", object.localName}};
  if (!object.instancePath.empty())
    result["instance_path"] = object.instancePath;
  if (!object.targetModule.empty())
    result["target_module"] = object.targetModule;
  if (!object.type.empty())
    result["type"] = object.type;
  if (object.direction)
    result["direction"] = toString(*object.direction);
  return result;
}

llvm::json::Value toJson(const NameMatch &match) {
  llvm::json::Object result{{"name", match.name},
                            {"path", match.path},
                            {"kind", toString(match.kind)},
                            {"module", match.moduleName},
                            {"score", match.score}};
  if (!match.targetModule.empty())
    result["target_module"] = match.targetModule;
  return result;
}

llvm::json::Value toJson(const NameSearchResult &result) {
  llvm::json::Array matches;
  for (const auto &match : result.matches)
    matches.push_back(toJson(match));
  return llvm::json::Object{{"design_id", result.designId},
                            {"query", result.query},
                            {"match_mode", toString(result.matchMode)},
                            {"matches", std::move(matches)}};
}

bool fromJSON(const llvm::json::Value &value, MatchMode &mode,
              llvm::json::Path path) {
  if (auto text = value.getAsString()) {
    if (auto parsed = parseMatchMode(text->str())) {
      mode = *parsed;
      return true;
    }
  }
  path.report("expected match mode: wildcard, fuzzy, or regex");
  return false;
}

bool fromJSON(const llvm::json::Value &value, ObjectKind &kind,
              llvm::json::Path path) {
  if (auto text = value.getAsString()) {
    if (auto parsed = parseObjectKind(text->str())) {
      kind = *parsed;
      return true;
    }
  }
  path.report("expected object kind: port, register, memory_port, or instance");
  return false;
}

bool fromJSON(const llvm::json::Value &value, IrStatus &status,
              llvm::json::Path path) {
  if (auto text = value.getAsString()) {
    if (auto parsed = parseIrStatus(text->str())) {
      status = *parsed;
      return true;
    }
  }
  path.report("expected IR status: core, synthesized, post-emission, or "
              "post-verilog-emission");
  return false;
}

std::string stringify(const llvm::json::Value &value) {
  std::string text;
  llvm::raw_string_ostream os(text);
  os << value;
  return os.str();
}

std::string stringifyPretty(const llvm::json::Value &value) {
  return llvm::formatv("{0:2}", value).str();
}

} // namespace circt_query
