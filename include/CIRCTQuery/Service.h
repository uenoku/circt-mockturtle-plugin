#ifndef CIRCT_QUERY_SERVICE_H
#define CIRCT_QUERY_SERVICE_H

#include "CIRCTQuery/Types.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace circt_query {

struct LoadDesignSpec {
  std::string path;
  std::optional<IrStatus> status;
};

class QueryService {
public:
  Result<DesignRecord> loadDesign(const LoadDesignSpec &spec);
  Result<std::vector<DesignRecord>>
  loadDesigns(const std::vector<LoadDesignSpec> &specs);

  std::vector<DesignRecord> listDesigns() const;
  Status setTop(const std::string &designId, const std::string &moduleName);

  Result<std::vector<ModuleSummary>>
  listModules(const std::string &designId) const;
  Result<ModuleInfo> getModule(const std::string &designId,
                               const std::string &moduleName) const;
  Result<std::vector<PortInfo>>
  getModulePorts(const std::string &designId,
                 const std::string &moduleName) const;
  Result<std::vector<InstanceInfo>>
  getModuleInstances(const std::string &designId,
                     const std::string &moduleName) const;

  Result<std::vector<DesignObjectInfo>>
  listObjects(const std::string &designId, const std::string &query = "",
              MatchMode matchMode = MatchMode::Wildcard,
              std::optional<ObjectKind> kind = std::nullopt) const;

  Result<NameSearchResult> searchNames(const std::string &designId,
                                       const std::string &query,
                                       MatchMode matchMode = MatchMode::Fuzzy,
                                       size_t limit = 20) const;

private:
  struct LoadedDesign {
    DesignRecord record;
    DesignModel model;
  };

  const LoadedDesign *findDesign(const std::string &designId) const;
  LoadedDesign *findDesign(const std::string &designId);

  std::map<std::string, LoadedDesign> designs;
  unsigned nextDesignId = 1;
};

Result<DesignModel> parseDesignFile(const std::string &path,
                                    bool includeValueIndex = true);
Result<DesignModel> parseDesignText(const std::string &text,
                                    const std::string &sourceName,
                                    bool includeValueIndex = true);
Result<IrStatus> inferDesignFileStatus(const std::string &path);

} // namespace circt_query

#endif // CIRCT_QUERY_SERVICE_H
