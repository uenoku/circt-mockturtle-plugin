#include "CIRCTQuery/Service.h"

#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/HW/HWInstanceGraph.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/SV/SVDialect.h"
#include "circt/Dialect/SV/SVOps.h"
#include "circt/Dialect/Seq/SeqDialect.h"
#include "circt/Dialect/Seq/SeqOps.h"
#include "circt/Dialect/Synth/SynthDialect.h"
#include "mlir/Bytecode/BytecodeReader.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/Operation.h"
#include "mlir/Parser/Parser.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <memory>
#include <set>

namespace circt_query {
namespace {

struct ParsedIr {
  std::unique_ptr<mlir::MLIRContext> context;
  std::shared_ptr<llvm::SourceMgr> sourceMgr;
  mlir::OwningOpRef<mlir::ModuleOp> module;
};

std::string printType(mlir::Type type) {
  std::string text;
  llvm::raw_string_ostream os(text);
  type.print(os);
  return os.str();
}

std::string printAttribute(mlir::Attribute attr) {
  std::string text;
  llvm::raw_string_ostream os(text);
  attr.print(os);
  return os.str();
}

std::string printLocation(mlir::Location loc) {
  std::string text;
  llvm::raw_string_ostream os(text);
  loc.print(os);
  return os.str();
}

std::map<std::string, std::string>
serializeDictionaryAttr(mlir::DictionaryAttr attrs) {
  std::map<std::string, std::string> result;
  if (!attrs)
    return result;
  for (mlir::NamedAttribute attr : attrs) {
    if (auto stringAttr = mlir::dyn_cast<mlir::StringAttr>(attr.getValue())) {
      result[attr.getName().str()] = stringAttr.getValue().str();
      continue;
    }
    result[attr.getName().str()] = printAttribute(attr.getValue());
  }
  return result;
}

void addUnique(std::vector<std::string> *values, const std::string &value) {
  if (value.empty())
    return;
  if (std::find(values->begin(), values->end(), value) == values->end())
    values->push_back(value);
}

std::string diagnosticsOrFallback(const std::string &diagnostics,
                                  const std::string &fallback) {
  if (diagnostics.empty())
    return fallback;
  return diagnostics;
}

void configureContext(mlir::MLIRContext *context) {
  mlir::DialectRegistry registry;
  registry.insert<circt::comb::CombDialect>();
  registry.insert<circt::hw::HWDialect>();
  registry.insert<circt::seq::SeqDialect>();
  registry.insert<circt::sv::SVDialect>();
  registry.insert<circt::synth::SynthDialect>();
  context->appendDialectRegistry(registry);
  context->loadAllAvailableDialects();
  context->allowUnregisteredDialects();
}

Result<ParsedIr>
parseIrFromSourceMgr(std::shared_ptr<llvm::SourceMgr> sourceMgr) {
  ParsedIr result;
  result.context = std::make_unique<mlir::MLIRContext>();
  configureContext(result.context.get());
  mlir::ParserConfig config(result.context.get());

  std::string diagnostics;
  llvm::raw_string_ostream diagnosticStream(diagnostics);
  mlir::SourceMgrDiagnosticHandler handler(*sourceMgr, result.context.get(),
                                           diagnosticStream);

  const llvm::MemoryBuffer *mainBuffer =
      sourceMgr ? sourceMgr->getMemoryBuffer(sourceMgr->getMainFileID())
                : nullptr;
  if (mainBuffer && mlir::isBytecode(mainBuffer->getMemBufferRef())) {
    mlir::Block block;
    mlir::BytecodeReader reader(mainBuffer->getMemBufferRef(), config,
                                /*lazyLoad=*/false, sourceMgr);
    if (mlir::failed(reader.readTopLevel(&block))) {
      return Result<ParsedIr>::failure(diagnosticsOrFallback(
          diagnosticStream.str(), "failed to parse MLIR bytecode"));
    }
    result.module =
        mlir::detail::constructContainerOpForParserIfNecessary<mlir::ModuleOp>(
            &block, result.context.get(),
            mlir::UnknownLoc::get(result.context.get()));
  } else {
    result.module = mlir::parseSourceFile<mlir::ModuleOp>(sourceMgr, config);
  }

  if (!result.module) {
    return Result<ParsedIr>::failure(
        diagnosticsOrFallback(diagnosticStream.str(), "failed to parse MLIR"));
  }
  result.sourceMgr = std::move(sourceMgr);
  return Result<ParsedIr>::success(std::move(result));
}

Result<ParsedIr> parseIrFromFile(const std::string &path) {
  auto bufferOrError = llvm::MemoryBuffer::getFileOrSTDIN(path);
  if (!bufferOrError)
    return Result<ParsedIr>::failure("unable to open '" + path + "'");
  auto sourceMgr = std::make_shared<llvm::SourceMgr>();
  sourceMgr->AddNewSourceBuffer(std::move(*bufferOrError), llvm::SMLoc());
  return parseIrFromSourceMgr(std::move(sourceMgr));
}

Result<ParsedIr> parseIrFromString(const std::string &text,
                                   const std::string &sourceName) {
  auto sourceMgr = std::make_shared<llvm::SourceMgr>();
  sourceMgr->AddNewSourceBuffer(
      llvm::MemoryBuffer::getMemBufferCopy(text, sourceName), llvm::SMLoc());
  return parseIrFromSourceMgr(std::move(sourceMgr));
}

std::optional<std::string> getRegisterNameAttr(mlir::Operation *operation) {
  if (!operation)
    return std::nullopt;
  if (auto stringAttr = operation->getAttrOfType<mlir::StringAttr>("name")) {
    if (!stringAttr.getValue().empty())
      return stringAttr.getValue().str();
  }
  if (auto stringAttr =
          operation->getAttrOfType<mlir::StringAttr>("sv.namehint")) {
    if (!stringAttr.getValue().empty())
      return stringAttr.getValue().str();
  }
  return std::nullopt;
}

std::optional<std::string> getInstanceResultName(mlir::OpResult result) {
  auto instance = mlir::dyn_cast<circt::hw::InstanceOp>(result.getOwner());
  if (!instance)
    return std::nullopt;
  unsigned resultIndex = result.getResultNumber();
  auto resultNames = instance.getResultNames();
  if (resultIndex >= resultNames.size())
    return std::nullopt;
  auto portNameAttr =
      mlir::dyn_cast<mlir::StringAttr>(resultNames[resultIndex]);
  if (!portNameAttr)
    return std::nullopt;
  std::string instanceName = instance.getInstanceName().str();
  return instanceName.empty()
             ? portNameAttr.getValue().str()
             : instanceName + "." + portNameAttr.getValue().str();
}

std::optional<std::string> getWireNameAttr(mlir::OpResult result) {
  auto wire = mlir::dyn_cast<circt::hw::WireOp>(result.getOwner());
  if (!wire)
    return std::nullopt;
  if (auto nameAttr = wire.getNameAttr())
    return nameAttr.getValue().str();
  return std::nullopt;
}

std::string getFirMemBaseName(mlir::Value memory) {
  auto memOp = memory.getDefiningOp<circt::seq::FirMemOp>();
  if (!memOp)
    return "firmem";
  if (auto nameAttr = memOp.getNameAttr();
      nameAttr && !nameAttr.getValue().empty())
    return nameAttr.getValue().str();
  return "firmem";
}

template <typename PortOp> unsigned getFirMemPortOrdinal(PortOp portOp) {
  auto memOp =
      portOp.getMemory().template getDefiningOp<circt::seq::FirMemOp>();
  if (!memOp)
    return 0;
  unsigned ordinal = 0;
  for (mlir::Operation *user : memOp->getUsers()) {
    if (!mlir::isa<PortOp>(user))
      continue;
    if (user == portOp.getOperation())
      return ordinal;
    ++ordinal;
  }
  return ordinal;
}

template <typename PortOp>
std::string makeFirMemPortName(PortOp portOp, llvm::StringRef kind) {
  return getFirMemBaseName(portOp.getMemory()) + "_" + kind.str() +
         std::to_string(getFirMemPortOrdinal(portOp));
}

class ModuleValueNamer {
public:
  explicit ModuleValueNamer(circt::hw::HWModuleLike moduleLike)
      : moduleLike(moduleLike) {}

  std::optional<std::string> get(mlir::Value value) {
    if (!value)
      return std::nullopt;
    auto it = valueNames.find(value);
    if (it != valueNames.end())
      return it->second;

    std::string name;
    if (auto preferred = getPreferredName(value)) {
      auto [nameIt, inserted] = usedNames.try_emplace(*preferred, value);
      if (inserted || nameIt->second == value)
        name = *preferred;
    }
    if (name.empty()) {
      if (auto opResult = mlir::dyn_cast<mlir::OpResult>(value)) {
        if (mlir::isa<circt::seq::CompRegOp, circt::seq::FirRegOp>(
                opResult.getOwner())) {
          name = makeUniqueName(getFallbackRegisterName(opResult.getOwner()),
                                value);
        }
      }
    }
    if (name.empty())
      name = "__value" + std::to_string(nextSyntheticId++);
    valueNames.try_emplace(value, name);
    return name;
  }

private:
  std::string makeUniqueName(llvm::StringRef baseName, mlir::Value value) {
    std::string candidate = baseName.str();
    for (unsigned suffix = 0;; ++suffix) {
      auto [nameIt, inserted] = usedNames.try_emplace(candidate, value);
      if (inserted || nameIt->second == value)
        return candidate;
      candidate = baseName.str() + "_" + std::to_string(suffix + 1);
    }
  }

  std::string getFallbackRegisterName(mlir::Operation *operation) {
    auto it = fallbackRegisterNames.find(operation);
    if (it != fallbackRegisterNames.end())
      return it->second;
    std::string name =
        "unnamed_register_" + std::to_string(nextFallbackRegisterId++);
    fallbackRegisterNames.try_emplace(operation, name);
    return name;
  }

  std::optional<std::string> getTopLevelPortName(mlir::BlockArgument value) {
    mlir::Block *body = moduleLike.getBodyBlock();
    if (!body || value.getOwner() != body)
      return std::nullopt;
    unsigned index = value.getArgNumber();
    if (index >= moduleLike.getNumInputPorts())
      return std::nullopt;
    return moduleLike.getInputName(index).str();
  }

  std::optional<std::string> getPreferredName(mlir::Value value) {
    if (auto blockArgument = mlir::dyn_cast<mlir::BlockArgument>(value))
      return getTopLevelPortName(blockArgument);
    auto opResult = mlir::dyn_cast<mlir::OpResult>(value);
    if (!opResult)
      return std::nullopt;
    if (auto instanceResultName = getInstanceResultName(opResult))
      return instanceResultName;
    if (auto wireName = getWireNameAttr(opResult))
      return wireName;
    if (mlir::isa<circt::seq::CompRegOp, circt::seq::FirRegOp>(
            opResult.getOwner()))
      return getRegisterNameAttr(opResult.getOwner());
    if (auto readOp =
            mlir::dyn_cast<circt::seq::FirMemReadOp>(opResult.getOwner()))
      return makeFirMemPortName(readOp, "R");
    if (auto readWriteOp =
            mlir::dyn_cast<circt::seq::FirMemReadWriteOp>(opResult.getOwner()))
      return makeFirMemPortName(readWriteOp, "RW");
    if (auto nameAttr = opResult.getOwner()->getAttrOfType<mlir::StringAttr>(
            "sv.namehint")) {
      if (!nameAttr.getValue().empty())
        return nameAttr.getValue().str();
    }
    return std::nullopt;
  }

  circt::hw::HWModuleLike moduleLike;
  llvm::DenseMap<mlir::Value, std::string> valueNames;
  llvm::DenseMap<mlir::Operation *, std::string> fallbackRegisterNames;
  llvm::StringMap<mlir::Value> usedNames;
  unsigned nextSyntheticId = 0;
  unsigned nextFallbackRegisterId = 0;
};

llvm::SmallVector<mlir::Value> getValueGraphOperands(mlir::Operation *op) {
  llvm::SmallVector<mlir::Value> operands;
  if (!op)
    return operands;
  if (mlir::isa<circt::hw::InstanceOp>(op))
    return operands;
  if (mlir::isa<circt::seq::CompRegOp, circt::seq::FirRegOp>(op)) {
    if (op->getNumOperands() != 0)
      operands.push_back(op->getOperand(0));
    return operands;
  }
  operands.append(op->operand_begin(), op->operand_end());
  return operands;
}

ValueKind classifyValueKind(mlir::Value value) {
  if (mlir::isa<mlir::BlockArgument>(value))
    return ValueKind::InputPort;
  auto result = mlir::dyn_cast<mlir::OpResult>(value);
  if (!result)
    return ValueKind::OperationResult;
  if (mlir::isa<circt::seq::CompRegOp, circt::seq::FirRegOp>(result.getOwner()))
    return ValueKind::Register;
  if (mlir::isa<circt::hw::InstanceOp>(result.getOwner()))
    return ValueKind::InstanceResult;
  if (mlir::isa<circt::hw::WireOp>(result.getOwner()))
    return ValueKind::Wire;
  return ValueKind::OperationResult;
}

std::optional<RegisterInfo> buildRegisterInfo(mlir::Operation *operation,
                                              ModuleValueNamer &valueNamer) {
  auto valueName = [&](mlir::Value value) {
    auto name = valueNamer.get(value);
    return name ? *name : std::string();
  };
  auto assignOptionalDriver = [&](std::optional<std::string> &field,
                                  mlir::Value value) {
    if (auto driver = valueNamer.get(value))
      field = *driver;
  };
  auto appendDataDriver = [&](RegisterInfo &info) {
    if (!operation->getOperands().empty()) {
      if (auto driver = valueNamer.get(operation->getOperand(0)))
        info.dataDrivers.push_back(*driver);
    }
  };
  auto displayName = [&](mlir::StringAttr nameAttr, mlir::Value result) {
    if (nameAttr && !nameAttr.getValue().empty())
      return nameAttr.getValue().str();
    if (auto attrName = getRegisterNameAttr(operation))
      return *attrName;
    return valueName(result);
  };

  if (auto compReg = mlir::dyn_cast<circt::seq::CompRegOp>(operation)) {
    RegisterInfo info;
    info.name = displayName(compReg.getNameAttr(), compReg.getResult());
    info.type = printType(compReg.getResult().getType());
    info.outputValueName = valueName(compReg.getResult());
    appendDataDriver(info);
    assignOptionalDriver(info.clockDriver, compReg.getClk());
    assignOptionalDriver(info.resetDriver, compReg.getReset());
    assignOptionalDriver(info.resetValueDriver, compReg.getResetValue());
    return info;
  }
  if (auto firReg = mlir::dyn_cast<circt::seq::FirRegOp>(operation)) {
    RegisterInfo info;
    info.name = displayName(firReg.getNameAttr(), firReg.getResult());
    info.type = printType(firReg.getResult().getType());
    info.outputValueName = valueName(firReg.getResult());
    appendDataDriver(info);
    assignOptionalDriver(info.clockDriver, firReg.getClk());
    assignOptionalDriver(info.resetDriver, firReg.getReset());
    assignOptionalDriver(info.resetValueDriver, firReg.getResetValue());
    return info;
  }
  return std::nullopt;
}

std::vector<MemoryPortInfo> buildMemoryPortInfos(mlir::Operation *operation,
                                                 ModuleValueNamer &valueNamer) {
  std::vector<MemoryPortInfo> infos;
  auto valueName = [&](mlir::Value value) {
    auto name = valueNamer.get(value);
    return name ? *name : std::string();
  };
  auto appendDriver = [&](MemoryPortInfo &info, mlir::Value value) {
    if (auto driver = valueNamer.get(value))
      info.dataDrivers.push_back(*driver);
  };

  if (auto readOp = mlir::dyn_cast<circt::seq::FirMemReadOp>(operation)) {
    MemoryPortInfo source;
    source.name = makeFirMemPortName(readOp, "R");
    source.type = printType(readOp.getData().getType());
    source.outputValueName = valueName(readOp.getData());
    infos.push_back(std::move(source));

    MemoryPortInfo addr;
    addr.name = makeFirMemPortName(readOp, "R") + "_ADDR";
    addr.type = printType(readOp.getAddress().getType());
    appendDriver(addr, readOp.getAddress());
    infos.push_back(std::move(addr));
    return infos;
  }

  if (auto writeOp = mlir::dyn_cast<circt::seq::FirMemWriteOp>(operation)) {
    std::string base = makeFirMemPortName(writeOp, "W");
    MemoryPortInfo addr;
    addr.name = base + "_ADDR";
    addr.type = printType(writeOp.getAddress().getType());
    appendDriver(addr, writeOp.getAddress());
    infos.push_back(std::move(addr));

    MemoryPortInfo data;
    data.name = base + "_DATA";
    data.type = printType(writeOp.getData().getType());
    appendDriver(data, writeOp.getData());
    infos.push_back(std::move(data));
    return infos;
  }

  if (auto readWriteOp =
          mlir::dyn_cast<circt::seq::FirMemReadWriteOp>(operation)) {
    std::string base = makeFirMemPortName(readWriteOp, "RW");
    MemoryPortInfo source;
    source.name = base;
    source.type = printType(readWriteOp.getReadData().getType());
    source.outputValueName = valueName(readWriteOp.getReadData());
    infos.push_back(std::move(source));

    MemoryPortInfo addr;
    addr.name = base + "_ADDR";
    addr.type = printType(readWriteOp.getAddress().getType());
    appendDriver(addr, readWriteOp.getAddress());
    infos.push_back(std::move(addr));

    MemoryPortInfo data;
    data.name = base + "_DATA";
    data.type = printType(readWriteOp.getWriteData().getType());
    appendDriver(data, readWriteOp.getWriteData());
    infos.push_back(std::move(data));
  }
  return infos;
}

IrStatus inferIrStatus(mlir::ModuleOp module) {
  bool sawSynth = false;
  bool sawSvReg = false;
  auto classifyAttrs = [&](mlir::ArrayRef<mlir::NamedAttribute> attrs) {
    for (const auto &attr : attrs) {
      if (attr.getName().strref().starts_with("synth.")) {
        sawSynth = true;
        return;
      }
    }
  };
  classifyAttrs(module->getAttrs());
  module.walk([&](mlir::Operation *op) {
    if (mlir::isa_and_nonnull<circt::synth::SynthDialect>(op->getDialect()))
      sawSynth = true;
    if (mlir::isa<circt::sv::RegOp>(op))
      sawSvReg = true;
    classifyAttrs(op->getAttrs());
  });
  if (sawSvReg)
    return IrStatus::PostVerilogEmission;
  if (sawSynth)
    return IrStatus::Synthesized;
  return IrStatus::Core;
}

ModuleInfo buildModuleInfo(circt::hw::HWModuleLike moduleLike,
                           bool includeValueIndex) {
  ModuleInfo info;
  info.name = moduleLike.getModuleNameAttr().getValue().str();
  info.isExternal =
      mlir::isa<circt::hw::HWModuleExternOp>(moduleLike.getOperation());

  auto moduleType = moduleLike.getHWModuleType();
  for (size_t index = 0; index < moduleLike.getNumInputPorts(); ++index) {
    auto circtPort = moduleLike.getPort(moduleLike.getPortIdForInputId(index));
    PortInfo port;
    port.name = moduleLike.getInputName(index).str();
    port.type = printType(moduleType.getInputTypes()[index]);
    port.direction = PortDirection::Input;
    port.attributes = serializeDictionaryAttr(circtPort.attrs);
    info.ports.push_back(std::move(port));
  }
  for (size_t index = 0; index < moduleLike.getNumOutputPorts(); ++index) {
    auto circtPort = moduleLike.getPort(moduleLike.getPortIdForOutputId(index));
    PortInfo port;
    port.name = moduleLike.getOutputName(index).str();
    port.type = printType(moduleType.getOutputTypes()[index]);
    port.direction = PortDirection::Output;
    port.attributes = serializeDictionaryAttr(circtPort.attrs);
    info.ports.push_back(std::move(port));
  }

  mlir::Block *body = moduleLike.getBodyBlock();
  if (!body)
    return info;

  ModuleValueNamer valueNamer(moduleLike);
  if (includeValueIndex) {
    for (size_t index = 0; index < moduleLike.getNumInputPorts(); ++index) {
      std::string name = moduleLike.getInputName(index).str();
      ValueInfo &value = info.values[name];
      value.name = name;
      value.kind = ValueKind::InputPort;
      value.type = printType(moduleType.getInputTypes()[index]);
      value.definingOp = "module input";
      auto circtPort =
          moduleLike.getPort(moduleLike.getPortIdForInputId(index));
      value.attributes = serializeDictionaryAttr(circtPort.attrs);
    }
  }

  body->walk([&](mlir::Operation *operation) {
    ++info.operationCount;

    if (auto instance = mlir::dyn_cast<circt::hw::InstanceOp>(operation)) {
      InstanceInfo entry;
      entry.name = instance.getInstanceName().str();
      entry.targetModule = instance.getReferencedModuleName().str();
      if (includeValueIndex) {
        auto argNames = instance.getArgNames();
        for (auto [index, operand] :
             llvm::enumerate(operation->getOperands())) {
          if (index >= argNames.size())
            break;
          auto nameAttr = mlir::dyn_cast<mlir::StringAttr>(argNames[index]);
          auto operandName = valueNamer.get(operand);
          if (nameAttr && operandName)
            entry.inputBindings.push_back(
                {nameAttr.getValue().str(), *operandName});
        }
        auto resultNames = instance.getResultNames();
        for (auto [index, result] : llvm::enumerate(operation->getResults())) {
          if (index >= resultNames.size())
            break;
          auto portNameAttr =
              mlir::dyn_cast<mlir::StringAttr>(resultNames[index]);
          auto localName = valueNamer.get(result);
          if (portNameAttr && localName)
            entry.outputBindings.push_back(
                {portNameAttr.getValue().str(), *localName});
        }
      }
      info.instances.push_back(std::move(entry));
    }

    if (mlir::isa<circt::seq::CompRegOp, circt::seq::FirRegOp>(operation))
      ++info.registerCount;

    if (includeValueIndex) {
      if (auto regInfo = buildRegisterInfo(operation, valueNamer)) {
        auto existing =
            std::find_if(info.registers.begin(), info.registers.end(),
                         [&](const RegisterInfo &value) {
                           return value.name == regInfo->name;
                         });
        if (existing == info.registers.end())
          info.registers.push_back(*regInfo);
      }
      for (const auto &memoryPort :
           buildMemoryPortInfos(operation, valueNamer)) {
        auto existing =
            std::find_if(info.memoryPorts.begin(), info.memoryPorts.end(),
                         [&](const MemoryPortInfo &value) {
                           return value.name == memoryPort.name;
                         });
        if (existing == info.memoryPorts.end())
          info.memoryPorts.push_back(memoryPort);
      }

      std::vector<std::string> resultNames;
      llvm::SmallVector<mlir::Value> valueGraphOperands =
          getValueGraphOperands(operation);
      for (mlir::Value result : operation->getResults()) {
        auto name = valueNamer.get(result);
        if (!name)
          continue;
        resultNames.push_back(*name);
        ValueInfo &value = info.values[*name];
        value.name = *name;
        value.kind = classifyValueKind(result);
        value.type = printType(result.getType());
        value.location = printLocation(result.getLoc());
        value.definingOp = operation->getName().getStringRef().str();
        value.attributes =
            serializeDictionaryAttr(operation->getAttrDictionary());
        for (mlir::Value operand : valueGraphOperands) {
          if (auto operandName = valueNamer.get(operand))
            addUnique(&value.drivers, *operandName);
        }
      }

      if (resultNames.empty()) {
        if (auto output = mlir::dyn_cast<circt::hw::OutputOp>(operation)) {
          for (auto [index, operand] : llvm::enumerate(output.getOperands())) {
            if (index >= moduleLike.getNumOutputPorts())
              break;
            std::string outputName = moduleLike.getOutputName(index).str();
            ValueInfo &value = info.values[outputName];
            value.name = outputName;
            value.kind = ValueKind::OutputPort;
            value.type = printType(moduleType.getOutputTypes()[index]);
            value.location = printLocation(output.getLoc());
            value.definingOp = "module output";
            auto circtPort =
                moduleLike.getPort(moduleLike.getPortIdForOutputId(index));
            value.attributes = serializeDictionaryAttr(circtPort.attrs);
            if (auto operandName = valueNamer.get(operand)) {
              addUnique(&value.drivers, *operandName);
              info.outputDrivers[outputName] = *operandName;
            }
          }
        }
        for (mlir::Value operand : operation->getOperands()) {
          if (auto operandName = valueNamer.get(operand)) {
            ValueInfo &value = info.values[*operandName];
            value.name = *operandName;
            addUnique(&value.users, operation->getName().getStringRef().str());
          }
        }
        return;
      }

      for (mlir::Value operand : valueGraphOperands) {
        if (auto operandName = valueNamer.get(operand)) {
          ValueInfo &value = info.values[*operandName];
          value.name = *operandName;
          for (const auto &resultName : resultNames)
            addUnique(&value.users, resultName);
        }
      }
    }
  });
  return info;
}

Result<DesignModel> buildDesignModel(mlir::ModuleOp module,
                                     bool includeValueIndex) {
  DesignModel design;
  design.hasValueIndex = includeValueIndex;

  std::vector<circt::hw::HWModuleLike> moduleLikes;
  for (mlir::Operation &operation : module.getBody()->getOperations()) {
    auto moduleLike = mlir::dyn_cast<circt::hw::HWModuleLike>(&operation);
    if (!moduleLike || operation.getNumRegions() == 0)
      continue;
    moduleLikes.push_back(moduleLike);
    design.moduleOrder.push_back(
        moduleLike.getModuleNameAttr().getValue().str());
  }
  if (moduleLikes.empty()) {
    return Result<DesignModel>::failure(
        "input does not contain any hw.module operations");
  }

  for (auto moduleLike : moduleLikes) {
    ModuleInfo info = buildModuleInfo(moduleLike, includeValueIndex);
    design.modules[info.name] = std::move(info);
  }
  return Result<DesignModel>::success(std::move(design));
}

} // namespace

Result<DesignModel> parseDesignFile(const std::string &path,
                                    bool includeValueIndex) {
  auto parsed = parseIrFromFile(path);
  if (!parsed.ok)
    return Result<DesignModel>::failure(parsed.message);
  return buildDesignModel(*parsed.value.module, includeValueIndex);
}

Result<DesignModel> parseDesignText(const std::string &text,
                                    const std::string &sourceName,
                                    bool includeValueIndex) {
  auto parsed = parseIrFromString(text, sourceName);
  if (!parsed.ok)
    return Result<DesignModel>::failure(parsed.message);
  return buildDesignModel(*parsed.value.module, includeValueIndex);
}

Result<IrStatus> inferDesignFileStatus(const std::string &path) {
  auto parsed = parseIrFromFile(path);
  if (!parsed.ok)
    return Result<IrStatus>::failure(parsed.message);
  return Result<IrStatus>::success(inferIrStatus(*parsed.value.module));
}

} // namespace circt_query
