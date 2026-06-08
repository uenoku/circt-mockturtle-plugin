#ifndef CIRCT_QUERY_MCP_H
#define CIRCT_QUERY_MCP_H

#include "CIRCTQuery/Service.h"

#include "llvm/Support/JSON.h"

#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace circt_query {

struct ToolDefinition {
  std::string name;
  std::string description;
  llvm::json::Object inputSchema;
};

struct ToolCallResult {
  bool isError = false;
  std::string text;
  llvm::json::Value structuredContent = nullptr;
};

class McpToolDispatcher {
public:
  explicit McpToolDispatcher(QueryService *service);

  std::vector<ToolDefinition> listTools() const;
  ToolCallResult callTool(const std::string &name,
                          const llvm::json::Value &arguments);

private:
  QueryService *service = nullptr;
};

class StdioMcpServer {
public:
  explicit StdioMcpServer(McpToolDispatcher *dispatcher);

  int run(std::istream &input, std::ostream &output);

private:
  enum class Encoding { NewlineDelimited, ContentLength };

  bool readMessage(std::istream &input, std::string *body,
                   Encoding *encoding) const;
  void writeMessage(std::ostream &output, const llvm::json::Value &message,
                    Encoding encoding) const;

  McpToolDispatcher *dispatcher = nullptr;
};

} // namespace circt_query

#endif // CIRCT_QUERY_MCP_H
