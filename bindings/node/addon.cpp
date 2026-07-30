#include <node_api.h>

#include "skilllib_c.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

struct NodeHandle {
  skilllib_t* lib = nullptr;
};

void check(napi_env env, napi_status status, const char* message) {
  if (status != napi_ok) napi_throw_error(env, nullptr, message);
}

std::string string_arg(napi_env env, napi_value value) {
  size_t len = 0;
  check(env, napi_get_value_string_utf8(env, value, nullptr, 0, &len),
        "expected string");
  std::string out(len + 1, '\0');
  size_t written = 0;
  check(env, napi_get_value_string_utf8(env, value, out.data(), out.size(), &written),
        "failed to read string");
  out.resize(written);
  return out;
}

NodeHandle* handle_arg(napi_env env, napi_value value) {
  void* ptr = nullptr;
  check(env, napi_get_value_external(env, value, &ptr),
        "expected Skill Router handle");
  auto* handle = static_cast<NodeHandle*>(ptr);
  if (!handle || !handle->lib) {
    napi_throw_error(env, nullptr, "Skill Router handle is closed");
    return nullptr;
  }
  return handle;
}

void finalize_handle(napi_env, void* data, void*) {
  auto* handle = static_cast<NodeHandle*>(data);
  if (!handle) return;
  if (handle->lib) skilllib_close(handle->lib);
  delete handle;
}

napi_value parse_json(napi_env env, const char* data, size_t len) {
  napi_value global, json, parse, text, result;
  check(env, napi_get_global(env, &global), "global lookup failed");
  check(env, napi_get_named_property(env, global, "JSON", &json),
        "JSON lookup failed");
  check(env, napi_get_named_property(env, json, "parse", &parse),
        "JSON.parse lookup failed");
  check(env, napi_create_string_utf8(env, data, len, &text),
        "JSON text allocation failed");
  check(env, napi_call_function(env, json, parse, 1, &text, &result),
        "JSON.parse failed");
  return result;
}

void throw_status(napi_env env, NodeHandle* handle, skilllib_status_t status) {
  std::string message = "skilllib status " + std::to_string(static_cast<int>(status));
  if (handle && handle->lib) {
    const char* detail = skilllib_last_error(handle->lib);
    if (detail && *detail) message += ": " + std::string(detail);
  }
  napi_throw_error(env, nullptr, message.c_str());
}

napi_value open_router(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  check(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr),
        "argument lookup failed");
  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "open(catalogPath, telemetryPath?, readOnly?)");
    return nullptr;
  }

  const std::string catalog = string_arg(env, argv[0]);
  std::string telemetry;
  if (argc >= 2) telemetry = string_arg(env, argv[1]);
  bool read_only = true;
  if (argc >= 3) {
    check(env, napi_get_value_bool(env, argv[2], &read_only),
          "readOnly must be boolean");
  }

  auto* handle = new NodeHandle();
  const auto status = skilllib_open(
      catalog.c_str(), telemetry.c_str(), read_only ? 1 : 0, &handle->lib);
  if (status != SKILLLIB_OK) {
    delete handle;
    napi_throw_error(env, nullptr, "skilllib_open failed");
    return nullptr;
  }

  napi_value external;
  check(env, napi_create_external(env, handle, finalize_handle, nullptr, &external),
        "handle creation failed");
  return external;
}

napi_value close_router(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  check(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr),
        "argument lookup failed");
  if (argc < 1) return nullptr;
  void* ptr = nullptr;
  check(env, napi_get_value_external(env, argv[0], &ptr),
        "expected Skill Router handle");
  auto* handle = static_cast<NodeHandle*>(ptr);
  if (handle && handle->lib) {
    skilllib_close(handle->lib);
    handle->lib = nullptr;
  }
  napi_value undefined;
  napi_get_undefined(env, &undefined);
  return undefined;
}

napi_value search_router(napi_env env, napi_callback_info info) {
  size_t argc = 5;
  napi_value argv[5];
  check(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr),
        "argument lookup failed");
  if (argc < 2) {
    napi_throw_type_error(env, nullptr,
                          "search(handle, query, topN?, mode?, includeArchived?)");
    return nullptr;
  }
  NodeHandle* handle = handle_arg(env, argv[0]);
  if (!handle) return nullptr;
  const std::string query = string_arg(env, argv[1]);

  int32_t top_n = 8;
  if (argc >= 3) napi_get_value_int32(env, argv[2], &top_n);
  std::string mode = "hybrid";
  if (argc >= 4) mode = string_arg(env, argv[3]);
  bool include_archived = false;
  if (argc >= 5) napi_get_value_bool(env, argv[4], &include_archived);

  skilllib_buffer_t out{};
  const auto status = skilllib_search(
      handle->lib, query.c_str(), top_n, mode.c_str(),
      include_archived ? 1 : 0, &out);
  if (status != SKILLLIB_OK) {
    throw_status(env, handle, status);
    return nullptr;
  }
  napi_value result = parse_json(env, out.data, out.len);
  skilllib_buffer_free(&out);
  return result;
}

napi_value register_router(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  check(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr),
        "argument lookup failed");
  if (argc < 2) {
    napi_throw_type_error(env, nullptr,
                          "register(handle, skillPath, keywords?)");
    return nullptr;
  }
  NodeHandle* handle = handle_arg(env, argv[0]);
  if (!handle) return nullptr;
  const std::string path = string_arg(env, argv[1]);
  std::string keywords;
  if (argc >= 3) keywords = string_arg(env, argv[2]);

  skilllib_buffer_t out{};
  const auto status = skilllib_register(
      handle->lib, path.c_str(), keywords.c_str(), &out);
  if (status != SKILLLIB_OK) {
    throw_status(env, handle, status);
    return nullptr;
  }
  napi_value result = parse_json(env, out.data, out.len);
  skilllib_buffer_free(&out);
  return result;
}

napi_value fetch_router(napi_env env, napi_callback_info info) {
  size_t argc = 5;
  napi_value argv[5];
  check(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr),
        "argument lookup failed");
  if (argc < 4) {
    napi_throw_type_error(
        env, nullptr,
        "fetch(handle, skillId, expectedRevision, catalogGeneration, context?)");
    return nullptr;
  }
  NodeHandle* handle = handle_arg(env, argv[0]);
  if (!handle) return nullptr;
  const std::string skill_id = string_arg(env, argv[1]);
  const std::string revision = string_arg(env, argv[2]);
  const std::string generation = string_arg(env, argv[3]);
  std::string context;
  if (argc >= 5) context = string_arg(env, argv[4]);

  skilllib_buffer_t out{};
  const auto status = skilllib_fetch(
      handle->lib, skill_id.c_str(), context.c_str(),
      revision.c_str(), generation.c_str(), &out);
  if (status != SKILLLIB_OK) {
    throw_status(env, handle, status);
    return nullptr;
  }
  napi_value result = parse_json(env, out.data, out.len);
  skilllib_buffer_free(&out);
  return result;
}

napi_value generation_router(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  check(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr),
        "argument lookup failed");
  NodeHandle* handle = argc ? handle_arg(env, argv[0]) : nullptr;
  if (!handle) return nullptr;

  skilllib_buffer_t out{};
  const auto status = skilllib_catalog_generation(handle->lib, &out);
  if (status != SKILLLIB_OK) {
    throw_status(env, handle, status);
    return nullptr;
  }
  napi_value result;
  check(env, napi_create_string_utf8(env, out.data, out.len, &result),
        "generation allocation failed");
  skilllib_buffer_free(&out);
  return result;
}

napi_value init(napi_env env, napi_value exports) {
  const napi_property_descriptor properties[] = {
      {"open", nullptr, open_router, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"close", nullptr, close_router, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"register", nullptr, register_router, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"search", nullptr, search_router, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"fetch", nullptr, fetch_router, nullptr, nullptr, nullptr, napi_default, nullptr},
      {"catalogGeneration", nullptr, generation_router, nullptr, nullptr, nullptr,
       napi_default, nullptr},
  };
  check(env, napi_define_properties(
                 env, exports,
                 sizeof(properties) / sizeof(properties[0]), properties),
        "export definition failed");
  return exports;
}

}  // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
