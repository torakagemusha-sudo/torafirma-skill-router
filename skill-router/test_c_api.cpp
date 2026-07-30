#include "skilllib_c.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static std::string json_string(const std::string& json, const std::string& key) {
  const std::string marker = "\"" + key + "\":\"";
  const auto start = json.find(marker);
  assert(start != std::string::npos);
  const auto value_start = start + marker.size();
  const auto end = json.find('"', value_start);
  assert(end != std::string::npos);
  return json.substr(value_start, end - value_start);
}

int main() {
  const fs::path root = fs::temp_directory_path() / "skillrouter_c_api_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);

  const fs::path skill = root / "SKILL.md";
  {
    std::ofstream out(skill, std::ios::binary);
    out << "---\n"
        << "name: c-api-test\n"
        << "version: 1.0.0\n"
        << "description: \"deterministic c api routing\"\n"
        << "---\n\n"
        << "# C API Test\n\nBODY\n";
  }

  const fs::path catalog = root / "catalog.db";
  const fs::path telemetry = root / "telemetry.db";
  skilllib_t* lib = nullptr;
  assert(skilllib_open(catalog.string().c_str(), telemetry.string().c_str(), 0, &lib) ==
         SKILLLIB_OK);
  assert(lib != nullptr);
  assert(std::string(skilllib_version()) == "1.1.0");

  skilllib_buffer_t registered{};
  assert(skilllib_register(lib, skill.string().c_str(), "deterministic,c,api",
                           &registered) == SKILLLIB_OK);
  const std::string registered_json(registered.data, registered.len);
  assert(registered_json.find("\"ok\":true") != std::string::npos);
  skilllib_buffer_free(&registered);

  skilllib_buffer_t search{};
  assert(skilllib_search(lib, "deterministic api", 8, "hybrid", 0, &search) ==
         SKILLLIB_OK);
  const std::string search_json(search.data, search.len);
  const std::string revision = json_string(search_json, "revision_id");
  const std::string generation = json_string(search_json, "catalog_generation");
  assert(revision.rfind("sha256:", 0) == 0);
  assert(generation.rfind("sha256:", 0) == 0);
  skilllib_buffer_free(&search);

  skilllib_buffer_t fetched{};
  assert(skilllib_fetch(lib, "c-api-test", "c-api-test", "",
                        generation.c_str(), &fetched) ==
         SKILLLIB_INVALID_ARGUMENT);
  assert(std::string(skilllib_last_error(lib)).find("expected_revision") !=
         std::string::npos);
  assert(fetched.data == nullptr && fetched.len == 0);

  assert(skilllib_fetch(lib, "c-api-test", "c-api-test", revision.c_str(),
                        "", &fetched) == SKILLLIB_INVALID_ARGUMENT);
  assert(std::string(skilllib_last_error(lib)).find("expected_catalog_generation") !=
         std::string::npos);
  assert(fetched.data == nullptr && fetched.len == 0);

  assert(skilllib_fetch(lib, "c-api-test", "c-api-test",
                        revision.c_str(), generation.c_str(), &fetched) ==
         SKILLLIB_OK);
  const std::string fetched_json(fetched.data, fetched.len);
  assert(fetched_json.find("BODY") != std::string::npos);
  assert(fetched_json.find("\"pinned\":true") != std::string::npos);
  skilllib_buffer_free(&fetched);

  {
    std::ofstream out(skill, std::ios::binary | std::ios::app);
    out << "MUTATION\n";
  }

  const auto status = skilllib_fetch(
      lib, "c-api-test", "c-api-test", revision.c_str(), generation.c_str(),
      &fetched);
  assert(status == SKILLLIB_REVISION_MISMATCH);
  assert(std::string(skilllib_last_error(lib)).find("REVISION_MISMATCH") !=
         std::string::npos);
  assert(fetched.data == nullptr && fetched.len == 0);

  skilllib_close(lib);
  fs::remove_all(root, ec);
  return 0;
}
