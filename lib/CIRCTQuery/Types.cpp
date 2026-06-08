#include "CIRCTQuery/Types.h"

#include <algorithm>
#include <cctype>

namespace circt_query {
namespace {

std::string normalizeEnumText(std::string text) {
  std::replace(text.begin(), text.end(), '_', '-');
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return std::tolower(ch); });
  return text;
}

} // namespace

std::string toString(PortDirection direction) {
  switch (direction) {
  case PortDirection::Input:
    return "input";
  case PortDirection::Output:
    return "output";
  case PortDirection::InOut:
    return "inout";
  }
  return "unknown";
}

std::string toString(MatchMode mode) {
  switch (mode) {
  case MatchMode::Wildcard:
    return "wildcard";
  case MatchMode::Fuzzy:
    return "fuzzy";
  case MatchMode::Regex:
    return "regex";
  }
  return "unknown";
}

std::string toString(ObjectKind kind) {
  switch (kind) {
  case ObjectKind::Port:
    return "port";
  case ObjectKind::Register:
    return "register";
  case ObjectKind::MemoryPort:
    return "memory_port";
  case ObjectKind::Instance:
    return "instance";
  }
  return "unknown";
}

std::string toString(EntityKind kind) {
  switch (kind) {
  case EntityKind::Design:
    return "design";
  case EntityKind::Module:
    return "module";
  case EntityKind::Port:
    return "port";
  case EntityKind::Register:
    return "register";
  case EntityKind::MemoryPort:
    return "memory_port";
  case EntityKind::Instance:
    return "instance";
  }
  return "unknown";
}

std::string toString(ValueKind kind) {
  switch (kind) {
  case ValueKind::InputPort:
    return "input_port";
  case ValueKind::OutputPort:
    return "output_port";
  case ValueKind::Register:
    return "register";
  case ValueKind::InstanceResult:
    return "instance_result";
  case ValueKind::Wire:
    return "wire";
  case ValueKind::OperationResult:
    return "operation_result";
  }
  return "unknown";
}

std::string toString(IrStatus status) {
  switch (status) {
  case IrStatus::Unknown:
    return "unknown";
  case IrStatus::Core:
    return "core";
  case IrStatus::Synthesized:
    return "synthesized";
  case IrStatus::PostVerilogEmission:
    return "post-verilog-emission";
  }
  return "unknown";
}

std::optional<MatchMode> parseMatchMode(const std::string &text) {
  std::string normalized = normalizeEnumText(text);
  if (normalized == "wildcard")
    return MatchMode::Wildcard;
  if (normalized == "fuzzy")
    return MatchMode::Fuzzy;
  if (normalized == "regex")
    return MatchMode::Regex;
  return std::nullopt;
}

std::optional<ObjectKind> parseObjectKind(const std::string &text) {
  std::string normalized = normalizeEnumText(text);
  if (normalized == "port")
    return ObjectKind::Port;
  if (normalized == "register")
    return ObjectKind::Register;
  if (normalized == "memory-port")
    return ObjectKind::MemoryPort;
  if (normalized == "instance")
    return ObjectKind::Instance;
  return std::nullopt;
}

std::optional<IrStatus> parseIrStatus(const std::string &text) {
  std::string normalized = normalizeEnumText(text);
  if (normalized == "core")
    return IrStatus::Core;
  if (normalized == "synthesized" || normalized == "synth")
    return IrStatus::Synthesized;
  if (normalized == "post-emission" || normalized == "post-verilog-emission")
    return IrStatus::PostVerilogEmission;
  if (normalized == "unknown")
    return IrStatus::Unknown;
  return std::nullopt;
}

} // namespace circt_query
