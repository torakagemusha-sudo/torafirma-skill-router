#include "skill_library.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <string>

namespace py = pybind11;
using skilllib::CatalogAccess;
using skilllib::SearchHit;
using skilllib::SearchMode;
using skilllib::SkillLibrary;

namespace {

py::dict search_hit_dict(const SearchHit& h) {
  py::dict components;
  components["exact_keyword"] = h.exact_keyword_score;
  components["exact_name"] = h.exact_name_score;
  components["exact_description"] = h.exact_description_score;
  components["fts_raw"] = h.fts_raw_score;
  components["fts_normalized"] = h.fts_normalized_score;
  components["fts_min"] = h.fts_min;
  components["fts_max"] = h.fts_max;
  components["fuzzy"] = h.fuzzy_score;
  components["base"] = h.base_score;
  components["search_count"] = h.search_count;
  components["fetch_count"] = h.fetch_count;
  components["telemetry_multiplier"] = h.telemetry_multiplier;
  components["state_multiplier"] = h.state_multiplier;

  py::dict out;
  out["skill_id"] = h.skill_id;
  out["description"] = h.description;
  out["skill_version"] = h.skill_version;
  out["revision_id"] = h.revision_id;
  out["catalog_generation"] = h.catalog_generation;
  out["ranking_policy"] = h.ranking_policy;
  out["query_digest"] = h.query_digest;
  out["normalized_tokens"] = h.normalized_tokens;
  out["search_mode"] = h.search_mode;
  out["state"] = skilllib::to_string(h.state);
  out["score"] = h.score;
  out["score_components"] = components;
  out["tie_break_key"] = h.tie_break_key;
  return out;
}

}  // namespace

PYBIND11_MODULE(skillrouter_native, m) {
  m.doc() = "Direct C++ convenience binding for Torafirma Skill Router";
  m.attr("__version__") = skilllib::kEngineVersion;
  m.attr("ranking_policy") = skilllib::kRankingPolicy;

  py::enum_<SearchMode>(m, "SearchMode")
      .value("HYBRID", SearchMode::Hybrid)
      .value("EXACT", SearchMode::Exact)
      .value("FTS", SearchMode::Fts)
      .value("FUZZY", SearchMode::Fuzzy);

  py::class_<SkillLibrary>(m, "SkillLibrary")
      .def(py::init([](const std::string& catalog_path,
                       const std::string& telemetry_path,
                       bool read_only) {
        return std::make_unique<SkillLibrary>(
            catalog_path, telemetry_path,
            read_only ? CatalogAccess::ReadOnly : CatalogAccess::ReadWrite);
      }),
      py::arg("catalog_path"),
      py::arg("telemetry_path") = "",
      py::arg("read_only") = true)
      .def("register",
           [](SkillLibrary& lib, const std::string& path,
              const std::string& keywords) {
             const auto r = lib.register_skill(path, keywords);
             if (!r.ok) throw std::runtime_error(r.error);
             py::dict out;
             out["skill_id"] = r.skill_id;
             out["skill_version"] = r.skill_version;
             out["revision_id"] = r.revision_id;
             out["catalog_generation"] = lib.catalog_generation();
             out["created"] = r.created;
             out["updated"] = r.updated;
             return out;
           },
           py::arg("skill_md_path"), py::arg("keywords") = "")
      .def("search",
           [](SkillLibrary& lib, const std::string& query, int top_n,
              SearchMode mode, bool include_archived) {
             py::list out;
             for (const auto& h :
                  lib.search(query, top_n, include_archived, mode)) {
               out.append(search_hit_dict(h));
             }
             return out;
           },
           py::arg("query"), py::arg("top_n") = 8,
           py::arg("mode") = SearchMode::Hybrid,
           py::arg("include_archived") = false)
      .def("fetch",
           [](SkillLibrary& lib, const std::string& skill_id,
              const std::string& expected_revision,
              const std::string& catalog_generation,
              const std::string& query_context) {
             const auto r = lib.fetch(skill_id, query_context,
                                      expected_revision, catalog_generation);
             py::dict out;
             out["skill_id"] = r.skill_id;
             out["skill_version"] = r.skill_version;
             out["revision_id"] = r.revision_id;
             out["catalog_generation"] = r.catalog_generation;
             out["pinned"] = r.pinned;
             out["body"] = r.body;
             return out;
           },
           py::arg("skill_id"), py::arg("expected_revision"),
           py::arg("catalog_generation"), py::arg("query_context") = "")
      .def("catalog_generation", &SkillLibrary::catalog_generation)
      .def_property_readonly("catalog_read_only",
                             &SkillLibrary::catalog_read_only);
}
