#include "skilllib_c.h"

#include "skill_library.hpp"

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <memory>
#include <new>
#include <sstream>
#include <string>

struct skilllib_t {
  std::unique_ptr<skilllib::SkillLibrary> impl;
  std::string last_error;
};

namespace {

const char* nz(const char* value) {
  return value ? value : "";
}

void clear_error(skilllib_t* lib) {
  if (lib) lib->last_error.clear();
}

skilllib_status_t classify_error(const std::string& message) {
  if (message.find("CATALOG_GENERATION_MISMATCH") != std::string::npos)
    return SKILLLIB_CATALOG_GENERATION_MISMATCH;
  if (message.find("REVISION_MISMATCH") != std::string::npos ||
      message.find("INDEX_DRIFT") != std::string::npos)
    return SKILLLIB_REVISION_MISMATCH;
  if (message.find("unknown skill_id") != std::string::npos)
    return SKILLLIB_NOT_FOUND;
  if (message.find("read-only") != std::string::npos ||
      message.find("denied") != std::string::npos)
    return SKILLLIB_READ_ONLY;
  if (message.find("cannot open") != std::string::npos ||
      message.find("missing on disk") != std::string::npos)
    return SKILLLIB_IO_ERROR;
  if (message.find("open failed") != std::string::npos ||
      message.find("prepare:") != std::string::npos ||
      message.find("step rc=") != std::string::npos)
    return SKILLLIB_DATABASE_ERROR;
  return SKILLLIB_INTERNAL_ERROR;
}

skilllib_status_t fail(skilllib_t* lib, const std::string& message,
                       skilllib_status_t status = SKILLLIB_INTERNAL_ERROR) {
  if (lib) lib->last_error = message;
  return status == SKILLLIB_INTERNAL_ERROR ? classify_error(message) : status;
}

skilllib_status_t write_buffer(skilllib_t* lib, const std::string& value,
                               skilllib_buffer_t* out) {
  if (!out) return fail(lib, "output buffer is null", SKILLLIB_INVALID_ARGUMENT);
  out->data = nullptr;
  out->len = 0;
  char* data = static_cast<char*>(std::malloc(value.size() + 1));
  if (!data) return fail(lib, "output allocation failed", SKILLLIB_INTERNAL_ERROR);
  if (!value.empty()) std::memcpy(data, value.data(), value.size());
  data[value.size()] = '\0';
  out->data = data;
  out->len = value.size();
  return SKILLLIB_OK;
}

std::string bool_json(bool value) {
  return value ? "true" : "false";
}

std::string register_json(const skilllib::RegisterResult& r,
                          const std::string& generation) {
  std::ostringstream out;
  out << "{\"ok\":true"
      << ",\"skill_id\":\"" << skilllib::json_escape(r.skill_id) << "\""
      << ",\"skill_version\":\"" << skilllib::json_escape(r.skill_version) << "\""
      << ",\"revision_id\":\"" << skilllib::json_escape(r.revision_id) << "\""
      << ",\"catalog_generation\":\"" << skilllib::json_escape(generation) << "\""
      << ",\"created\":" << bool_json(r.created)
      << ",\"updated\":" << bool_json(r.updated)
      << "}";
  return out.str();
}

std::string search_json(const std::vector<skilllib::SearchHit>& hits) {
  std::ostringstream out;
  out << std::setprecision(17) << "[";
  for (std::size_t i = 0; i < hits.size(); ++i) {
    if (i) out << ",";
    const auto& h = hits[i];
    out << "{"
        << "\"rank\":" << i
        << ",\"skill_id\":\"" << skilllib::json_escape(h.skill_id) << "\""
        << ",\"description\":\"" << skilllib::json_escape(h.description) << "\""
        << ",\"skill_version\":\"" << skilllib::json_escape(h.skill_version) << "\""
        << ",\"revision_id\":\"" << skilllib::json_escape(h.revision_id) << "\""
        << ",\"catalog_generation\":\"" << skilllib::json_escape(h.catalog_generation) << "\""
        << ",\"ranking_policy\":\"" << skilllib::json_escape(h.ranking_policy) << "\""
        << ",\"query_digest\":\"" << skilllib::json_escape(h.query_digest) << "\""
        << ",\"normalized_tokens\":\"" << skilllib::json_escape(h.normalized_tokens) << "\""
        << ",\"search_mode\":\"" << skilllib::json_escape(h.search_mode) << "\""
        << ",\"state\":\"" << skilllib::to_string(h.state) << "\""
        << ",\"score\":" << h.score
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
        << "}"
        << ",\"tie_break_key\":\"" << skilllib::json_escape(h.tie_break_key) << "\""
        << "}";
  }
  out << "]";
  return out.str();
}

std::string fetch_json(const skilllib::FetchResult& r) {
  std::ostringstream out;
  out << "{"
      << "\"skill_id\":\"" << skilllib::json_escape(r.skill_id) << "\""
      << ",\"skill_version\":\"" << skilllib::json_escape(r.skill_version) << "\""
      << ",\"revision_id\":\"" << skilllib::json_escape(r.revision_id) << "\""
      << ",\"catalog_generation\":\"" << skilllib::json_escape(r.catalog_generation) << "\""
      << ",\"pinned\":" << bool_json(r.pinned)
      << ",\"body\":\"" << skilllib::json_escape(r.body) << "\""
      << "}";
  return out.str();
}

}  // namespace

extern "C" {

const char* skilllib_version(void) {
  return skilllib::kEngineVersion;
}

const char* skilllib_ranking_policy(void) {
  return skilllib::kRankingPolicy;
}

skilllib_status_t skilllib_open(const char* catalog_path,
                                const char* telemetry_path,
                                int read_only,
                                skilllib_t** out_lib) {
  if (!out_lib || !catalog_path || !*catalog_path)
    return SKILLLIB_INVALID_ARGUMENT;
  *out_lib = nullptr;
  try {
    std::unique_ptr<skilllib_t> handle(new skilllib_t());
    handle->impl = std::make_unique<skilllib::SkillLibrary>(
        catalog_path, nz(telemetry_path),
        read_only ? skilllib::CatalogAccess::ReadOnly
                  : skilllib::CatalogAccess::ReadWrite);
    *out_lib = handle.release();
    return SKILLLIB_OK;
  } catch (const std::exception&) {
    return SKILLLIB_DATABASE_ERROR;
  } catch (...) {
    return SKILLLIB_INTERNAL_ERROR;
  }
}

void skilllib_close(skilllib_t* lib) {
  delete lib;
}

const char* skilllib_last_error(const skilllib_t* lib) {
  return lib ? lib->last_error.c_str() : "skillib handle is null";
}

void skilllib_buffer_free(skilllib_buffer_t* buffer) {
  if (!buffer) return;
  std::free(buffer->data);
  buffer->data = nullptr;
  buffer->len = 0;
}

skilllib_status_t skilllib_register(skilllib_t* lib,
                                   const char* skill_md_path,
                                    const char* keywords,
                                   skilllib_buffer_t* out_json) {
  if (out_json) {
    out_json->data = nullptr;
    out_json->len = 0;
  }
  if (!lib || !lib->impl || !skill_md_path || !*skill_md_path || !out_json)
    return fail(lib, "invalid register arguments", SKILLLIB_INVALID_ARGUMENT);
  try {
    clear_error(lib);
    const auto result = lib->impl->register_skill(skill_md_path, nz(keywords));
    if (!result.ok) return fail(lib, result.error, SKILLLIB_INVALID_ARGUMENT);
    return write_buffer(lib,
                        register_json(result, lib->impl->catalog_generation()),
                        out_json);
  } catch (const std::exception& e) {
    return fail(lib, e.what());
  } catch (...) {
    return fail(lib, "unknown register failure");
  }
}

skilllib_status_t skilllib_search(skilllib_t* lib,
                                  const char* query,
                                  int top_n,
                                  const char* mode,
                                  int include_archived,
                                  skilllib_buffer_t* out_json) {
  if (out_json) {
    out_json->data = nullptr;
    out_json->len = 0;
  }
  if (!lib || !lib->impl || !query || !out_json)
    return fail(lib, "invalid search arguments", SKILLLIB_INVALID_ARGUMENT);
  try {
    clear_error(lib);
    const auto hits = lib->impl->search(
        query, top_n, include_archived != 0,
        skilllib::search_mode_from_string(nz(mode)));
    return write_buffer(lib, search_json(hits), out_json);
  } catch (const std::exception& e) {
    return fail(lib, e.what());
  } catch (...) {
    return fail(lib, "unknown search failure");
  }
}

skilllib_status_t skilllib_fetch(skilllib_t* lib,
                                 const char* skill_id,
                                 const char* query_context,
                                 const char* expected_revision,
                                 const char* expected_catalog_generation,
                                 skilllib_buffer_t* out_json) {
  if (out_json) {
    out_json->data = nullptr;
    out_json->len = 0;
  }
  if (!lib || !lib->impl || !skill_id || !*skill_id ||
      !expected_revision || !*expected_revision ||
      !expected_catalog_generation || !*expected_catalog_generation ||
      !out_json)
    return fail(lib,
                "fetch requires skill_id, expected_revision, and "
                "expected_catalog_generation",
                SKILLLIB_INVALID_ARGUMENT);
  try {
    clear_error(lib);
    const auto result = lib->impl->fetch(
        skill_id, nz(query_context), nz(expected_revision),
        nz(expected_catalog_generation));
    return write_buffer(lib, fetch_json(result), out_json);
  } catch (const std::exception& e) {
    return fail(lib, e.what());
  } catch (...) {
    return fail(lib, "unknown fetch failure");
  }
}

skilllib_status_t skilllib_catalog_generation(
    skilllib_t* lib,
    skilllib_buffer_t* out_generation) {
  if (out_generation) {
    out_generation->data = nullptr;
    out_generation->len = 0;
  }
  if (!lib || !lib->impl || !out_generation)
    return fail(lib, "invalid catalog-generation arguments",
                SKILLLIB_INVALID_ARGUMENT);
  try {
    clear_error(lib);
    return write_buffer(lib, lib->impl->catalog_generation(), out_generation);
  } catch (const std::exception& e) {
    return fail(lib, e.what());
  } catch (...) {
    return fail(lib, "unknown catalog-generation failure");
  }
}

}  // extern "C"
