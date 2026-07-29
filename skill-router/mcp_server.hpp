// mcp_server.hpp - Model Context Protocol interface for skillrouter.
#pragma once

#include "skill_library.hpp"

#include <cmath>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace skilllib::mcp {

inline constexpr const char* kProtocolVersion = "2024-11-05";
inline constexpr const char* kServerName = "skillrouter";

struct JsonValue {
  enum class Type { Null, Bool, Num, Str, Arr, Obj };
  Type type = Type::Null;
  bool b = false;
  double num = 0.0;
  std::string str;
  std::vector<JsonValue> arr;
  std::map<std::string, JsonValue> obj;

  bool is_null() const { return type == Type::Null; }
  bool is_obj() const { return type == Type::Obj; }
  bool is_str() const { return type == Type::Str; }
  bool is_num() const { return type == Type::Num; }
  bool is_bool() const { return type == Type::Bool; }
  const JsonValue* find(const std::string& key) const {
    if (type != Type::Obj) return nullptr;
    const auto it = obj.find(key);
    return it == obj.end() ? nullptr : &it->second;
  }
  std::string as_str(const std::string& def = "") const { return is_str() ? str : def; }
  double as_num(double def = 0.0) const { return is_num() ? num : def; }
  bool as_bool(bool def = false) const { return is_bool() ? b : def; }
};

class JsonParser {
 public:
  explicit JsonParser(const std::string& s) : s_(s) {}

  JsonValue parse() {
    skip_ws();
    JsonValue v = parse_value();
    skip_ws();
    if (i_ != s_.size()) fail("trailing characters after JSON value");
    return v;
  }

 private:
  [[noreturn]] void fail(const char* what) const {
    throw std::runtime_error(std::string("JSON parse error: ") + what);
  }
  void skip_ws() {
    while (i_ < s_.size()) {
      const char c = s_[i_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i_;
      else break;
    }
  }
  char peek() const {
    if (i_ >= s_.size()) fail("unexpected end of input");
    return s_[i_];
  }
  JsonValue parse_value() {
    skip_ws();
    const char c = peek();
    switch (c) {
      case '{': return parse_object();
      case '[': return parse_array();
      case '"': { JsonValue v; v.type = JsonValue::Type::Str; v.str = parse_string(); return v; }
      case 't': case 'f': return parse_bool();
      case 'n': return parse_null();
      default:
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        fail("unexpected character");
    }
  }
  JsonValue parse_object() {
    JsonValue v; v.type = JsonValue::Type::Obj;
    ++i_;
    skip_ws();
    if (i_ < s_.size() && s_[i_] == '}') { ++i_; return v; }
    while (true) {
      skip_ws();
      if (peek() != '"') fail("expected string key in object");
      const std::string key = parse_string();
      skip_ws();
      if (peek() != ':') fail("expected ':' after object key");
      ++i_;
      v.obj[key] = parse_value();
      skip_ws();
      const char c = peek();
      if (c == ',') { ++i_; continue; }
      if (c == '}') { ++i_; break; }
      fail("expected ',' or '}' in object");
    }
    return v;
  }
  JsonValue parse_array() {
    JsonValue v; v.type = JsonValue::Type::Arr;
    ++i_;
    skip_ws();
    if (i_ < s_.size() && s_[i_] == ']') { ++i_; return v; }
    while (true) {
      v.arr.push_back(parse_value());
      skip_ws();
      const char c = peek();
      if (c == ',') { ++i_; continue; }
      if (c == ']') { ++i_; break; }
      fail("expected ',' or ']' in array");
    }
    return v;
  }
  static void append_utf8(std::string& out, unsigned int cp) {
    if (cp <= 0x7F) out += static_cast<char>(cp);
    else if (cp <= 0x7FF) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  unsigned int parse_hex4() {
    if (i_ + 4 > s_.size()) fail("truncated \\u escape");
    unsigned int cp = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = s_[i_++];
      cp <<= 4;
      if (c >= '0' && c <= '9') cp |= static_cast<unsigned int>(c - '0');
      else if (c >= 'a' && c <= 'f') cp |= static_cast<unsigned int>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') cp |= static_cast<unsigned int>(c - 'A' + 10);
      else fail("invalid hex digit in \\u escape");
    }
    return cp;
  }
  std::string parse_string() {
    ++i_;
    std::string out;
    while (true) {
      if (i_ >= s_.size()) fail("unterminated string");
      const char c = s_[i_++];
      if (c == '"') break;
      if (c == '\\') {
        if (i_ >= s_.size()) fail("unterminated escape");
        const char e = s_[i_++];
        switch (e) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 't': out += '\t'; break;
          case 'u': {
            unsigned int cp = parse_hex4();
            if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 1 < s_.size() &&
                s_[i_] == '\\' && s_[i_ + 1] == 'u') {
              i_ += 2;
              const unsigned int lo = parse_hex4();
              if (lo >= 0xDC00 && lo <= 0xDFFF)
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              else { append_utf8(out, cp); cp = lo; }
            }
            append_utf8(out, cp);
            break;
          }
          default: fail("invalid escape character");
        }
      } else {
        out += c;
      }
    }
    return out;
  }
  JsonValue parse_number() {
    const std::size_t start = i_;
    if (s_[i_] == '-') ++i_;
    while (i_ < s_.size()) {
      const char c = s_[i_];
      if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') ++i_;
      else break;
    }
    JsonValue v; v.type = JsonValue::Type::Num;
    try { v.num = std::stod(s_.substr(start, i_ - start)); }
    catch (...) { fail("invalid number"); }
    return v;
  }
  JsonValue parse_bool() {
    JsonValue v; v.type = JsonValue::Type::Bool;
    if (s_.compare(i_, 4, "true") == 0) { v.b = true; i_ += 4; }
    else if (s_.compare(i_, 5, "false") == 0) { v.b = false; i_ += 5; }
    else fail("invalid literal");
    return v;
  }
  JsonValue parse_null() {
    if (s_.compare(i_, 4, "null") != 0) fail("invalid literal");
    i_ += 4;
    return JsonValue{};
  }

  const std::string& s_;
  std::size_t i_ = 0;
};

inline JsonValue json_parse(const std::string& s) { return JsonParser(s).parse(); }

inline std::string serialize_id(const JsonValue& id) {
  switch (id.type) {
    case JsonValue::Type::Str: return "\"" + json_escape(id.str) + "\"";
    case JsonValue::Type::Num: {
      const long long as_int = static_cast<long long>(id.num);
      if (static_cast<double>(as_int) == id.num) return std::to_string(as_int);
      std::ostringstream o; o << id.num; return o.str();
    }
    default: return "null";
  }
}

inline std::string rpc_result(const std::string& id_json, const std::string& result_json) {
  return "{\"jsonrpc\":\"2.0\",\"id\":" + id_json + ",\"result\":" + result_json + "}";
}

inline std::string rpc_error(const std::string& id_json, int code, const std::string& message) {
  std::ostringstream o;
  o << "{\"jsonrpc\":\"2.0\",\"id\":" << id_json << ",\"error\":{\"code\":" << code
    << ",\"message\":\"" << json_escape(message) << "\"}}";
  return o.str();
}

inline std::string tool_text_result(const std::string& text, bool is_error = false) {
  std::ostringstream o;
  o << "{\"content\":[{\"type\":\"text\",\"text\":\"" << json_escape(text) << "\"}]";
  if (is_error) o << ",\"isError\":true";
  o << "}";
  return o.str();
}

inline std::string tools_list_json() {
  return R"json({"tools":[
{"name":"skill_search","title":"Search the skill library","description":"Search indexed skill metadata using the deterministic tf.skillrouter.hybrid-lexical.v1 ranking policy. Results include the score decomposition, immutable skill revision and catalog generation required for an exact fetch.","inputSchema":{"type":"object","properties":{"query":{"type":"string"},"top":{"type":"integer","minimum":1},"mode":{"type":"string","enum":["hybrid","exact","fts","fuzzy"]},"include_archived":{"type":"boolean"}},"required":["query"]}},
{"name":"skill_fetch","title":"Fetch an exact skill revision","description":"Load the exact SKILL.md revision selected by skill_search. The router fails closed if the catalog generation or body revision changed between search and fetch. Loaded revisions are pinned in the caller context; there is no implicit hot reload.","inputSchema":{"type":"object","properties":{"skill_id":{"type":"string"},"expected_revision":{"type":"string"},"catalog_generation":{"type":"string"},"context":{"type":"string"}},"required":["skill_id","expected_revision","catalog_generation"]}},
{"name":"skill_stats","title":"Skill library telemetry","description":"Return aggregate telemetry, catalog generation, ranking policy and lifecycle counts.","inputSchema":{"type":"object","properties":{}}},
{"name":"skill_graveyard","title":"Low-value skill report","description":"Return skills frequently suggested but rarely fetched.","inputSchema":{"type":"object","properties":{"min_searches":{"type":"integer","minimum":0}}}}
]})json";
}

inline std::string search_hit_json(const SearchHit& h) {
  std::ostringstream o;
  o << "{\"skill_id\":\"" << json_escape(h.skill_id)
    << "\",\"description\":\"" << json_escape(h.description)
    << "\",\"skill_version\":\"" << json_escape(h.skill_version)
    << "\",\"revision_id\":\"" << json_escape(h.revision_id)
    << "\",\"catalog_generation\":\"" << json_escape(h.catalog_generation)
    << "\",\"ranking_policy\":\"" << json_escape(h.ranking_policy)
    << "\",\"query_digest\":\"" << json_escape(h.query_digest)
    << "\",\"normalized_tokens\":\"" << json_escape(h.normalized_tokens)
    << "\",\"search_mode\":\"" << json_escape(h.search_mode)
    << "\",\"state\":\"" << to_string(h.state)
    << "\",\"score\":" << h.score
    << ",\"score_components\":{"
    << "\"exact_keyword\":" << h.exact_keyword_score
    << ",\"exact_name\":" << h.exact_name_score
    << ",\"exact_description\":" << h.exact_description_score
    << ",\"fts_raw\":" << h.fts_raw_score
    << ",\"fts_normalized\":" << h.fts_normalized_score
    << ",\"fts_min\":" << h.fts_min
    << ",\"fts_max\":" << h.fts_max
    << ",\"fuzzy\":" << h.fuzzy_score
    << ",\"base\":" << h.base_score
    << ",\"search_count\":" << h.search_count
    << ",\"fetch_count\":" << h.fetch_count
    << ",\"telemetry_multiplier\":" << h.telemetry_multiplier
    << ",\"state_multiplier\":" << h.state_multiplier
    << ",\"tie_break_key\":\"" << json_escape(h.tie_break_key) << "\"}}";
  return o.str();
}

inline std::string search_hits_json(const std::vector<SearchHit>& hits) {
  std::ostringstream o; o << '[';
  for (std::size_t i = 0; i < hits.size(); ++i) {
    if (i) o << ',';
    o << search_hit_json(hits[i]);
  }
  o << ']';
  return o.str();
}

inline std::string stats_json(SkillLibrary& lib) {
  const auto t = lib.telemetry();
  const auto counts = lib.state_counts();
  std::ostringstream o;
  o << "{\"engine_version\":\"" << kEngineVersion
    << "\",\"ranking_policy\":\"" << kRankingPolicy
    << "\",\"catalog_generation\":\"" << lib.catalog_generation()
    << "\",\"catalog_access\":\"" << (lib.catalog_read_only() ? "read-only" : "read-write")
    << "\",\"total_searches\":" << t.total_searches
    << ",\"total_fetches\":" << t.total_fetches
    << ",\"overall_conversion\":" << t.overall_conversion << ",\"by_state\":{";
  bool first = true;
  for (const auto& [k, v] : counts) {
    if (!first) o << ',';
    o << '\"' << json_escape(k) << "\":" << v;
    first = false;
  }
  o << "}}";
  return o.str();
}

inline std::string graveyard_json(SkillLibrary& lib, long long min_searches) {
  const auto cands = lib.graveyard_candidates(min_searches);
  std::ostringstream o; o << '[';
  for (std::size_t i = 0; i < cands.size(); ++i) {
    if (i) o << ',';
    const double conv = cands[i].search_count > 0
        ? static_cast<double>(cands[i].fetch_count) / cands[i].search_count : 0.0;
    o << "{\"skill_id\":\"" << json_escape(cands[i].skill_id)
      << "\",\"search_count\":" << cands[i].search_count
      << ",\"fetch_count\":" << cands[i].fetch_count
      << ",\"conversion\":" << conv << '}';
  }
  o << ']';
  return o.str();
}

inline std::string resources_list_json(SkillLibrary& lib) {
  const auto rows = lib.list_all();
  std::ostringstream o; o << "{\"resources\":[";
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (i) o << ',';
    const std::string uri = "skill://" + rows[i].skill_id + "@" + rows[i].content_hash;
    o << "{\"uri\":\"" << json_escape(uri)
      << "\",\"name\":\"" << json_escape(rows[i].skill_id + "@" + rows[i].version)
      << "\",\"description\":\"" << json_escape(rows[i].description)
      << "\",\"mimeType\":\"text/markdown\"}";
  }
  o << "]}";
  return o.str();
}

inline std::string call_tool(SkillLibrary& lib, const std::string& name, const JsonValue& args) {
  try {
    if (name == "skill_search") {
      const JsonValue* q = args.find("query");
      if (!q || !q->is_str() || q->str.empty())
        return tool_text_result("skill_search requires a non-empty string 'query'", true);
      int top = 8;
      if (const JsonValue* t = args.find("top"); t && t->is_num() && t->num >= 1)
        top = static_cast<int>(t->num);
      bool include_archived = false;
      if (const JsonValue* ia = args.find("include_archived"); ia && ia->is_bool())
        include_archived = ia->b;
      SearchMode mode = SearchMode::Hybrid;
      if (const JsonValue* m = args.find("mode"); m && m->is_str())
        mode = search_mode_from_string(m->str);
      return tool_text_result(search_hits_json(lib.search(q->str, top, include_archived, mode)));
    }
    if (name == "skill_fetch") {
      const JsonValue* id = args.find("skill_id");
      const JsonValue* rev = args.find("expected_revision");
      const JsonValue* gen = args.find("catalog_generation");
      if (!id || !id->is_str() || id->str.empty())
        return tool_text_result("skill_fetch requires a non-empty string 'skill_id'", true);
      if (!rev || !rev->is_str() || rev->str.empty())
        return tool_text_result("skill_fetch requires 'expected_revision' from skill_search", true);
      if (!gen || !gen->is_str() || gen->str.empty())
        return tool_text_result("skill_fetch requires 'catalog_generation' from skill_search", true);
      const std::string context = args.find("context") ? args.find("context")->as_str() : "";
      try {
        return tool_text_result(lib.fetch_body(id->str, context, rev->str, gen->str));
      } catch (const DbError& e) {
        return tool_text_result(std::string("cannot fetch exact skill revision: ") + e.what(), true);
      }
    }
    if (name == "skill_stats") return tool_text_result(stats_json(lib));
    if (name == "skill_graveyard") {
      long long minimum = 5;
      if (const JsonValue* m = args.find("min_searches"); m && m->is_num() && m->num >= 0)
        minimum = static_cast<long long>(m->num);
      return tool_text_result(graveyard_json(lib, minimum));
    }
    return tool_text_result("unknown tool: " + name, true);
  } catch (const std::exception& e) {
    return tool_text_result(std::string("tool error: ") + e.what(), true);
  }
}

inline std::optional<std::string> handle_request(SkillLibrary& lib, const std::string& line) {
  JsonValue req;
  try { req = json_parse(line); }
  catch (const std::exception& e) { return rpc_error("null", -32700, std::string("parse error: ") + e.what()); }

  const JsonValue* id = req.find("id");
  const bool notification = id == nullptr || id->is_null();
  const std::string id_json = id ? serialize_id(*id) : "null";
  const JsonValue* method_v = req.find("method");
  if (!method_v || !method_v->is_str()) {
    if (notification) return std::nullopt;
    return rpc_error(id_json, -32600, "invalid request: missing 'method'");
  }
  if (notification) return std::nullopt;
  const std::string method = method_v->str;
  const JsonValue* params = req.find("params");
  JsonValue empty; empty.type = JsonValue::Type::Obj;
  const JsonValue& p = params ? *params : empty;

  if (method == "initialize") {
    std::ostringstream o;
    o << "{\"protocolVersion\":\"" << kProtocolVersion
      << "\",\"capabilities\":{\"tools\":{},\"resources\":{}},\"serverInfo\":{\"name\":\""
      << kServerName << "\",\"version\":\"" << kEngineVersion << "\"}}";
    return rpc_result(id_json, o.str());
  }
  if (method == "ping") return rpc_result(id_json, "{}");
  if (method == "tools/list") return rpc_result(id_json, tools_list_json());
  if (method == "tools/call") {
    const JsonValue* name = p.find("name");
    if (!name || !name->is_str()) return rpc_error(id_json, -32602, "invalid params: 'name' is required");
    const JsonValue* args = p.find("arguments");
    JsonValue empty_args; empty_args.type = JsonValue::Type::Obj;
    return rpc_result(id_json, call_tool(lib, name->str, args ? *args : empty_args));
  }
  if (method == "resources/list") return rpc_result(id_json, resources_list_json(lib));
  if (method == "resources/templates/list") return rpc_result(id_json, "{\"resourceTemplates\":[]}");
  if (method == "resources/read") {
    const JsonValue* uri_v = p.find("uri");
    if (!uri_v || !uri_v->is_str()) return rpc_error(id_json, -32602, "invalid params: 'uri' is required");
    const std::string uri = uri_v->str;
    const std::string prefix = "skill://";
    if (uri.compare(0, prefix.size(), prefix) != 0)
      return rpc_error(id_json, -32602, "unsupported resource uri");
    const std::string ref = uri.substr(prefix.size());
    const std::size_t at = ref.rfind('@');
    if (at == std::string::npos || at == 0 || at + 1 >= ref.size())
      return rpc_error(id_json, -32602, "resource uri must pin a revision: skill://skill_id@sha256:...");
    const std::string skill_id = ref.substr(0, at);
    const std::string expected_revision = ref.substr(at + 1);
    try {
      const auto fetched = lib.fetch(skill_id, "resource:" + uri, expected_revision, "");
      std::ostringstream o;
      o << "{\"contents\":[{\"uri\":\"" << json_escape(uri)
        << "\",\"mimeType\":\"text/markdown\",\"text\":\"" << json_escape(fetched.body) << "\"}]}";
      return rpc_result(id_json, o.str());
    } catch (const DbError& e) {
      return rpc_error(id_json, -32002, std::string("resource not found or revision changed: ") + e.what());
    }
  }
  return rpc_error(id_json, -32601, "method not found: " + method);
}

}  // namespace skilllib::mcp
