#include "CIRCTQuery/Mcp.h"

#include "CIRCTQuery/Json.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#include <cctype>
#include <optional>
#include <sstream>

namespace circt_query {

using llvm::json::Array;
using llvm::json::Object;
using llvm::json::ObjectMapper;
using llvm::json::Path;
using llvm::json::Value;

bool fromJSON(const Value &value, LoadDesignSpec &spec, Path path) {
  ObjectMapper mapper(value, path);
  return mapper && mapper.map("path", spec.path) &&
         mapper.map("status", spec.status);
}

namespace {

struct LoadDesignsArgs {
  std::vector<LoadDesignSpec> paths;
};

struct DesignIdArgs {
  std::string designId;
};

struct SetTopArgs {
  std::string designId;
  std::string moduleName;
};

struct ModuleArgs {
  std::string designId;
  std::string moduleName;
};

struct ObjectListArgs {
  std::string designId;
  std::string query;
  std::optional<MatchMode> matchMode;
  std::optional<ObjectKind> kind;
};

struct NameSearchArgs {
  std::string designId;
  std::string query;
  std::optional<MatchMode> matchMode;
  std::optional<int64_t> limit;
};

bool fromJSON(const Value &value, LoadDesignsArgs &args, Path path) {
  ObjectMapper mapper(value, path);
  return mapper && mapper.map("paths", args.paths);
}

bool fromJSON(const Value &value, DesignIdArgs &args, Path path) {
  ObjectMapper mapper(value, path);
  return mapper && mapper.map("design_id", args.designId);
}

bool fromJSON(const Value &value, SetTopArgs &args, Path path) {
  ObjectMapper mapper(value, path);
  return mapper && mapper.map("design_id", args.designId) &&
         mapper.map("module_name", args.moduleName);
}

bool fromJSON(const Value &value, ModuleArgs &args, Path path) {
  ObjectMapper mapper(value, path);
  return mapper && mapper.map("design_id", args.designId) &&
         mapper.map("module_name", args.moduleName);
}

bool fromJSON(const Value &value, ObjectListArgs &args, Path path) {
  ObjectMapper mapper(value, path);
  return mapper && mapper.map("design_id", args.designId) &&
         mapper.mapOptional("query", args.query) &&
         mapper.map("match_mode", args.matchMode) &&
         mapper.map("kind", args.kind);
}

bool fromJSON(const Value &value, NameSearchArgs &args, Path path) {
  ObjectMapper mapper(value, path);
  return mapper && mapper.map("design_id", args.designId) &&
         mapper.map("query", args.query) &&
         mapper.map("match_mode", args.matchMode) &&
         mapper.map("limit", args.limit);
}

template <typename T>
std::optional<T> decodeArgs(const Value &arguments, std::string *errorMessage) {
  llvm::json::Path::Root root("arguments");
  T result;
  if (fromJSON(arguments, result, root))
    return result;
  if (errorMessage)
    *errorMessage = llvm::toString(root.getError());
  return std::nullopt;
}

ToolCallResult makeError(std::string message) {
  return ToolCallResult{true, std::move(message), Object{{"error", message}}};
}

ToolCallResult makeSuccess(std::string text, Value structuredContent) {
  return ToolCallResult{false, std::move(text), std::move(structuredContent)};
}

Array serializeDesignArray(const std::vector<DesignRecord> &designs) {
  Array array;
  for (const auto &design : designs)
    array.push_back(toJson(design));
  return array;
}

template <typename T> Array serializeArray(const std::vector<T> &values) {
  Array array;
  for (const auto &value : values)
    array.push_back(toJson(value));
  return array;
}

Object
schema(std::initializer_list<std::pair<llvm::StringLiteral, Value>> props,
       std::initializer_list<llvm::StringLiteral> required = {}) {
  Object properties;
  for (const auto &entry : props)
    properties[entry.first] = entry.second;
  Array requiredArray;
  for (llvm::StringLiteral key : required)
    requiredArray.push_back(key.str());
  Object result{{"type", "object"}, {"properties", std::move(properties)}};
  if (!requiredArray.empty())
    result["required"] = std::move(requiredArray);
  return result;
}

Value stringSchema() { return Object{{"type", "string"}}; }

Value matchModeSchema() {
  return Object{{"type", "string"},
                {"enum", Array{"wildcard", "fuzzy", "regex"}}};
}

Value objectKindSchema() {
  return Object{{"type", "string"},
                {"enum", Array{"port", "register", "memory_port", "instance"}}};
}

Value statusSchema() {
  return Object{{"type", "string"},
                {"enum", Array{"core", "synthesized", "post-emission",
                               "post-verilog-emission"}}};
}

Value loadPathsSchema() {
  return Object{
      {"type", "array"},
      {"items", schema({{"path", stringSchema()}, {"status", statusSchema()}},
                       {"path", "status"})}};
}

Object buildMcpTool(const ToolDefinition &tool) {
  Object result{{"name", tool.name}, {"description", tool.description}};
  result["inputSchema"] = Value(Object(tool.inputSchema));
  return result;
}

Object buildToolResult(const ToolCallResult &result) {
  Array content;
  std::string text = result.text;
  if (!result.structuredContent.getAsNull())
    text += "\n\nJSON:\n" + stringifyPretty(result.structuredContent);
  content.push_back(Object{{"type", "text"}, {"text", text}});

  Object response{{"content", std::move(content)}};
  if (result.isError)
    response["isError"] = true;
  if (!result.structuredContent.getAsNull())
    response["structuredContent"] = result.structuredContent;
  return response;
}

std::optional<std::string> getStringField(const Object &object,
                                          llvm::StringLiteral key) {
  const Value *value = object.get(key);
  if (!value)
    return std::nullopt;
  if (auto text = value->getAsString())
    return text->str();
  return std::nullopt;
}

std::optional<Value> getId(const Object &object) {
  const Value *value = object.get("id");
  if (!value)
    return std::nullopt;
  if (value->getAsInteger() || value->getAsString())
    return *value;
  return std::nullopt;
}

Object jsonRpcError(std::optional<Value> id, int code, std::string message) {
  Object response{{"jsonrpc", "2.0"},
                  {"error", Object{{"code", code}, {"message", message}}}};
  response["id"] = id ? *id : Value(nullptr);
  return response;
}

Object jsonRpcResult(const Value &id, Value result) {
  return Object{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

} // namespace

McpToolDispatcher::McpToolDispatcher(QueryService *service)
    : service(service) {}

std::vector<ToolDefinition> McpToolDispatcher::listTools() const {
  return {
      {"circt_design_load", "Load one or more CIRCT MLIR designs from disk",
       schema({{"paths", loadPathsSchema()}}, {"paths"})},
      {"circt_design_list", "List loaded designs", schema({})},
      {"circt_design_top_set",
       "Select the top module for hierarchical object queries",
       schema({{"design_id", stringSchema()}, {"module_name", stringSchema()}},
              {"design_id", "module_name"})},
      {"circt_module_list", "List modules in a loaded design",
       schema({{"design_id", stringSchema()}}, {"design_id"})},
      {"circt_module_get", "Return one module's structural summary",
       schema({{"design_id", stringSchema()}, {"module_name", stringSchema()}},
              {"design_id", "module_name"})},
      {"circt_module_ports_get", "Return ports for one module",
       schema({{"design_id", stringSchema()}, {"module_name", stringSchema()}},
              {"design_id", "module_name"})},
      {"circt_module_instances_get", "Return instances for one module",
       schema({{"design_id", stringSchema()}, {"module_name", stringSchema()}},
              {"design_id", "module_name"})},
      {"circt_object_list",
       "List top-relative ports, registers, memory ports, and instances",
       schema({{"design_id", stringSchema()},
               {"query", stringSchema()},
               {"match_mode", matchModeSchema()},
               {"kind", objectKindSchema()}},
              {"design_id"})},
      {"circt_name_search",
       "Search design, module, port, register, memory port, and instance names",
       schema({{"design_id", stringSchema()},
               {"query", stringSchema()},
               {"match_mode", matchModeSchema()},
               {"limit", Object{{"type", "integer"}, {"minimum", 0}}}},
              {"design_id", "query"})},
  };
}

ToolCallResult McpToolDispatcher::callTool(const std::string &name,
                                           const Value &arguments) {
  if (!service)
    return makeError("query service is not available");

  std::string error;
  if (name == "circt_design_load") {
    auto args = decodeArgs<LoadDesignsArgs>(arguments, &error);
    if (!args)
      return makeError(error);
    auto loaded = service->loadDesigns(args->paths);
    if (!loaded.ok)
      return makeError(loaded.message);
    return makeSuccess("designs loaded",
                       Object{{"designs", serializeDesignArray(loaded.value)}});
  }

  if (name == "circt_design_list") {
    return makeSuccess(
        "designs",
        Object{{"designs", serializeDesignArray(service->listDesigns())}});
  }

  if (name == "circt_design_top_set") {
    auto args = decodeArgs<SetTopArgs>(arguments, &error);
    if (!args)
      return makeError(error);
    Status status = service->setTop(args->designId, args->moduleName);
    if (!status.ok)
      return makeError(status.message);
    return makeSuccess("top selected",
                       Object{{"design_id", args->designId},
                              {"selected_top", args->moduleName}});
  }

  if (name == "circt_module_list") {
    auto args = decodeArgs<DesignIdArgs>(arguments, &error);
    if (!args)
      return makeError(error);
    auto modules = service->listModules(args->designId);
    if (!modules.ok)
      return makeError(modules.message);
    return makeSuccess("modules",
                       Object{{"modules", serializeArray(modules.value)}});
  }

  if (name == "circt_module_get") {
    auto args = decodeArgs<ModuleArgs>(arguments, &error);
    if (!args)
      return makeError(error);
    auto module = service->getModule(args->designId, args->moduleName);
    if (!module.ok)
      return makeError(module.message);
    return makeSuccess("module", Object{{"module", toJson(module.value)}});
  }

  if (name == "circt_module_ports_get") {
    auto args = decodeArgs<ModuleArgs>(arguments, &error);
    if (!args)
      return makeError(error);
    auto ports = service->getModulePorts(args->designId, args->moduleName);
    if (!ports.ok)
      return makeError(ports.message);
    return makeSuccess("ports", Object{{"ports", serializeArray(ports.value)}});
  }

  if (name == "circt_module_instances_get") {
    auto args = decodeArgs<ModuleArgs>(arguments, &error);
    if (!args)
      return makeError(error);
    auto instances =
        service->getModuleInstances(args->designId, args->moduleName);
    if (!instances.ok)
      return makeError(instances.message);
    return makeSuccess("instances",
                       Object{{"instances", serializeArray(instances.value)}});
  }

  if (name == "circt_object_list") {
    auto args = decodeArgs<ObjectListArgs>(arguments, &error);
    if (!args)
      return makeError(error);
    auto objects = service->listObjects(
        args->designId, args->query,
        args->matchMode.value_or(MatchMode::Wildcard), args->kind);
    if (!objects.ok)
      return makeError(objects.message);
    return makeSuccess("objects",
                       Object{{"objects", serializeArray(objects.value)}});
  }

  if (name == "circt_name_search") {
    auto args = decodeArgs<NameSearchArgs>(arguments, &error);
    if (!args)
      return makeError(error);
    auto result = service->searchNames(
        args->designId, args->query, args->matchMode.value_or(MatchMode::Fuzzy),
        args->limit && *args->limit >= 0 ? static_cast<size_t>(*args->limit)
                                         : 20);
    if (!result.ok)
      return makeError(result.message);
    return makeSuccess("matches", Object{{"result", toJson(result.value)}});
  }

  return makeError("unknown tool '" + name + "'");
}

StdioMcpServer::StdioMcpServer(McpToolDispatcher *dispatcher)
    : dispatcher(dispatcher) {}

int StdioMcpServer::run(std::istream &input, std::ostream &output) {
  while (true) {
    std::string body;
    Encoding encoding = Encoding::NewlineDelimited;
    if (!readMessage(input, &body, &encoding))
      return 0;
    if (body.empty())
      continue;

    auto parsed = llvm::json::parse(body);
    if (!parsed) {
      std::string message;
      llvm::raw_string_ostream os(message);
      llvm::logAllUnhandledErrors(parsed.takeError(), os, "");
      writeMessage(output, jsonRpcError(std::nullopt, -32700, os.str()),
                   encoding);
      continue;
    }

    const Object *request = parsed->getAsObject();
    if (!request) {
      writeMessage(output,
                   jsonRpcError(std::nullopt, -32600, "request must be object"),
                   encoding);
      continue;
    }
    auto id = getId(*request);
    auto method = getStringField(*request, "method");
    if (!method) {
      writeMessage(output,
                   jsonRpcError(id, -32600, "request.method must be string"),
                   encoding);
      continue;
    }

    if (*method == "initialize") {
      if (!id)
        continue;
      Object capabilities{{"tools", Object{{"listChanged", true}}}};
      Object result{
          {"protocolVersion", "2024-11-05"},
          {"capabilities", std::move(capabilities)},
          {"serverInfo", Object{{"name", "circt-query"}, {"version", "0.1"}}}};
      writeMessage(output, jsonRpcResult(*id, std::move(result)), encoding);
      continue;
    }

    if (*method == "notifications/initialized")
      continue;

    if (!id)
      continue;

    if (*method == "ping") {
      writeMessage(output, jsonRpcResult(*id, Object{}), encoding);
      continue;
    }

    if (*method == "tools/list") {
      Array tools;
      for (const auto &tool : dispatcher->listTools())
        tools.push_back(buildMcpTool(tool));
      writeMessage(output,
                   jsonRpcResult(*id, Object{{"tools", std::move(tools)}}),
                   encoding);
      continue;
    }

    if (*method == "tools/call") {
      const Value *paramsValue = request->get("params");
      const Object *params = paramsValue ? paramsValue->getAsObject() : nullptr;
      if (!params) {
        writeMessage(
            output,
            jsonRpcError(id, -32602, "tools/call params must be object"),
            encoding);
        continue;
      }
      auto toolName = getStringField(*params, "name");
      const Value *arguments = params->get("arguments");
      Value emptyArguments(Object{});
      if (!arguments)
        arguments = &emptyArguments;
      if (!toolName) {
        writeMessage(output,
                     jsonRpcError(id, -32602, "tool name must be string"),
                     encoding);
        continue;
      }
      ToolCallResult result = dispatcher->callTool(*toolName, *arguments);
      writeMessage(output, jsonRpcResult(*id, buildToolResult(result)),
                   encoding);
      continue;
    }

    writeMessage(output,
                 jsonRpcError(id, -32601, "unknown method '" + *method + "'"),
                 encoding);
  }
}

bool StdioMcpServer::readMessage(std::istream &input, std::string *body,
                                 Encoding *encoding) const {
  body->clear();
  *encoding = Encoding::NewlineDelimited;

  int first = input.peek();
  if (first == EOF)
    return false;
  if (first == 'C') {
    std::string line;
    size_t contentLength = 0;
    while (std::getline(input, line)) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      if (line.empty())
        break;
      llvm::StringRef ref(line);
      if (ref.consume_front("Content-Length:")) {
        ref = ref.trim();
        unsigned long long parsedLength = 0;
        if (!ref.getAsInteger(10, parsedLength))
          contentLength = static_cast<size_t>(parsedLength);
      }
    }
    if (contentLength == 0)
      return false;
    body->resize(contentLength);
    input.read(body->data(), static_cast<std::streamsize>(contentLength));
    *encoding = Encoding::ContentLength;
    return static_cast<size_t>(input.gcount()) == contentLength;
  }

  std::string line;
  if (!std::getline(input, line))
    return false;
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  *body = std::move(line);
  return true;
}

void StdioMcpServer::writeMessage(std::ostream &output, const Value &message,
                                  Encoding encoding) const {
  std::string body = stringify(message);
  if (encoding == Encoding::ContentLength)
    output << "Content-Length: " << body.size() << "\r\n\r\n" << body;
  else
    output << body << '\n';
  output.flush();
}

} // namespace circt_query
