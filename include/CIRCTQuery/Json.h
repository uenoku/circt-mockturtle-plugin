#ifndef CIRCT_QUERY_JSON_H
#define CIRCT_QUERY_JSON_H

#include "CIRCTQuery/Types.h"

#include "llvm/Support/JSON.h"

namespace circt_query {

llvm::json::Value toJson(const PortInfo &port);
llvm::json::Value toJson(const RegisterInfo &reg);
llvm::json::Value toJson(const MemoryPortInfo &port);
llvm::json::Value toJson(const InstanceInfo &instance);
llvm::json::Value toJson(const ValueInfo &value);
llvm::json::Value toJson(const ModuleInfo &module);
llvm::json::Value toJson(const ModuleSummary &module);
llvm::json::Value toJson(const DesignRecord &design);
llvm::json::Value toJson(const DesignObjectInfo &object);
llvm::json::Value toJson(const NameMatch &match);
llvm::json::Value toJson(const NameSearchResult &result);

bool fromJSON(const llvm::json::Value &value, MatchMode &mode,
              llvm::json::Path path);
bool fromJSON(const llvm::json::Value &value, ObjectKind &kind,
              llvm::json::Path path);
bool fromJSON(const llvm::json::Value &value, IrStatus &status,
              llvm::json::Path path);

std::string stringify(const llvm::json::Value &value);
std::string stringifyPretty(const llvm::json::Value &value);

} // namespace circt_query

#endif // CIRCT_QUERY_JSON_H
