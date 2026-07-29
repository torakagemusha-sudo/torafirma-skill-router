// test_library.cpp - regression and contract tests for Skill Router 1.1.0.
#include "skill_library.hpp"
#include "mcp_server.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace skilllib;
namespace fs = std::filesystem;

static int g_pass = 0;
static std::vector<std::pair<const char*, void (*)()>>& tests() {
  static std::vector<std::pair<const char*, void (*)()>> t;
  return t;
}
#define TEST(name) void name(); struct name##_reg { name##_reg(){ tests().push_back({#name,name}); } } name##_inst; void name()

static fs::path root() {
  static fs::path p = []{
    fs::path x = fs::temp_directory_path() / "skillrouter_1_1_tests";
    std::error_code ec; fs::remove_all(x, ec); fs::create_directories(x, ec); return x;
  }();
  return p;
}
static fs::path dir(const std::string& name) {
  fs::path p = root() / name; std::error_code ec; fs::create_directories(p, ec); return p;
}
static std::string write_skill(const fs::path& d, const std::string& name,
                               const std::string& desc, const std::string& body="body",
                               const std::string& version="1.0.0") {
  fs::path p = d / (name + "_SKILL.md");
  std::ofstream f(p, std::ios::binary);
  f << "---\nname: " << name << "\nversion: " << version
    << "\ndescription: \"" << desc << "\"\n---\n\n# " << name << "\n\n" << body << "\n";
  return p.string();
}
static void append(const std::string& p, const std::string& s) {
  std::ofstream f(p, std::ios::binary | std::ios::app); f << s;
}

TEST(test_sha256_empty_vector) {
  assert(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}
TEST(test_sha256_abc_vector) {
  assert(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
TEST(test_revision_id_is_sha256_namespaced) {
  assert(revision_id("abc") == "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
TEST(test_frontmatter_version_parsed) {
  auto fm = parse_frontmatter("---\nname: a\nversion: 2.3.4\ndescription: \"desc\"\n---\nbody");
  assert(fm.valid && fm.version == "2.3.4");
}
TEST(test_frontmatter_version_defaults) {
  auto fm = parse_frontmatter("---\nname: a\ndescription: \"desc\"\n---\nbody");
  assert(fm.valid && fm.version == "1.0.0");
}
TEST(test_frontmatter_folded_description) {
  auto fm = parse_frontmatter("---\nname: a\ndescription: >\n  first line\n  second line\n---\nbody");
  assert(fm.valid && fm.description == "first line second line");
}
TEST(test_frontmatter_missing_invalid) {
  assert(!parse_frontmatter("---\nname: a\n---\nbody").valid);
}
TEST(test_xml_tag_detection) {
  assert(looks_like_xml_tag("fetch <skill_id> now"));
  assert(!looks_like_xml_tag("score < 3 and count > 1"));
}
TEST(test_register_creates_content_addressed_row) {
  auto d=dir("register"); auto p=write_skill(d,"alpha","alpha widgets","BODY","1.2.3");
  SkillLibrary lib(":memory:"); auto r=lib.register_skill(p); assert(r.ok&&r.created);
  SkillRow row; assert(lib.get_row("alpha",row));
  assert(row.version=="1.2.3" && row.content_hash.rfind("sha256:",0)==0 && row.state==State::Indexed);
}
TEST(test_register_rejects_xml_description) {
  auto d=dir("xml"); fs::path p=d/"SKILL.md"; std::ofstream f(p); f<<"---\nname: x\ndescription: \"fetch <skill_id>\"\n---\n"; f.close();
  SkillLibrary lib(":memory:"); auto r=lib.register_skill(p.string()); assert(!r.ok);
}
TEST(test_reregister_unchanged_noop) {
  auto d=dir("noop"); auto p=write_skill(d,"n","noop desc"); SkillLibrary lib(":memory:");
  assert(lib.register_skill(p).created); auto r=lib.register_skill(p); assert(r.ok&&!r.created&&!r.updated);
}
TEST(test_reindex_changes_revision_and_version) {
  auto d=dir("reindex"); auto p=write_skill(d,"r","old desc","A","1.0.0"); SkillLibrary lib(":memory:");
  auto a=lib.register_skill(p); write_skill(d,"r","new desc","B","2.0.0"); auto b=lib.register_skill(p);
  assert(b.updated && a.revision_id!=b.revision_id && b.skill_version=="2.0.0");
}
TEST(test_drift_detection_marks_stale_operator) {
  auto d=dir("drift"); auto p=write_skill(d,"d","drift desc"); SkillLibrary lib(":memory:"); lib.register_skill(p);
  append(p,"mutation"); assert(lib.mark_stale_if_drifted("d")); SkillRow row; lib.get_row("d",row); assert(row.state==State::Stale);
}
TEST(test_deprecated_state_survives_reindex) {
  auto d=dir("preserve"); auto p=write_skill(d,"p","desc"); SkillLibrary lib(":memory:"); lib.register_skill(p); lib.set_state("p",State::Deprecated);
  write_skill(d,"p","changed"); lib.register_skill(p); SkillRow row; lib.get_row("p",row); assert(row.state==State::Deprecated);
}
TEST(test_search_keyword_weight_exposed) {
  auto d=dir("rank_kw"); auto p=write_skill(d,"alpha","alpha widget processing"); SkillLibrary lib(":memory:"); lib.register_skill(p,"widget");
  auto h=lib.search("widget",8,false,SearchMode::Exact); assert(h.size()==1&&h[0].exact_keyword_score==3.0);
}
TEST(test_search_name_weight_exposed) {
  auto d=dir("rank_name"); auto p=write_skill(d,"specialname","unrelated prose"); SkillLibrary lib(":memory:"); lib.register_skill(p,"other");
  auto h=lib.search("specialname",8,false,SearchMode::Exact); assert(h.size()==1&&h[0].exact_name_score==2.0);
}
TEST(test_search_description_weight_exposed) {
  auto d=dir("rank_desc"); auto p=write_skill(d,"xskill","contains rareword here"); SkillLibrary lib(":memory:"); lib.register_skill(p,"other");
  auto h=lib.search("rareword",8,false,SearchMode::Exact); assert(h.size()==1&&h[0].exact_description_score==1.0);
}
TEST(test_search_returns_policy_revision_generation) {
  auto d=dir("contract"); auto p=write_skill(d,"contract","contract work"); SkillLibrary lib(":memory:"); lib.register_skill(p);
  auto h=lib.search("contract"); assert(h.size()==1); assert(h[0].ranking_policy==kRankingPolicy);
  assert(h[0].revision_id.rfind("sha256:",0)==0 && h[0].catalog_generation.rfind("sha256:",0)==0);
}
TEST(test_search_normalization_and_query_digest) {
  auto d=dir("normalize"); auto p=write_skill(d,"norm","alpha beta"); SkillLibrary lib(":memory:"); lib.register_skill(p);
  auto h=lib.search("The ALPHA alpha and beta"); assert(h[0].normalized_tokens=="alpha beta");
  assert(h[0].query_digest=="sha256:"+sha256_hex("alpha beta"));
}
TEST(test_search_logs_full_decision) {
  auto d=dir("decision"); auto p=write_skill(d,"decision","decision audit"); SkillLibrary lib(":memory:"); lib.register_skill(p);
  lib.search("decision audit"); assert(lib.count_routing_decisions("decision")==1);
}
TEST(test_exact_tie_break_is_skill_id) {
  auto d=dir("tie"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"b_skill","shared token")); lib.register_skill(write_skill(d,"a_skill","shared token"));
  auto h=lib.search("shared token",8,false,SearchMode::Exact); assert(h.size()==2&&h[0].skill_id=="a_skill");
}
TEST(test_fts_stemming) {
  auto d=dir("fts"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"trainer","training optimizers"));
  if (lib.fts_enabled()) { assert(lib.search("train optimizer",8,false,SearchMode::Exact).empty()); assert(!lib.search("train optimizer",8,false,SearchMode::Fts).empty()); }
}
TEST(test_fuzzy_typo) {
  auto d=dir("fuzzy"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"kube","kubernetes orchestration"));
  auto h=lib.search("kubernets",8,false,SearchMode::Fuzzy); assert(!h.empty()&&h[0].fuzzy_score>0);
}
TEST(test_exact_dominates_hybrid) {
  auto d=dir("dominates"); SkillLibrary lib(":memory:");
  lib.register_skill(write_skill(d,"exact","kubernetes deployment")); lib.register_skill(write_skill(d,"stem","deploying kubernetes clusters"));
  auto h=lib.search("kubernetes deployment",8,false,SearchMode::Hybrid); assert(h.size()==2&&h[0].skill_id=="exact");
}
TEST(test_deprecated_multiplier) {
  auto d=dir("deprecated"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"old","shared task")); lib.register_skill(write_skill(d,"new","shared task"));
  lib.set_state("old",State::Deprecated); auto h=lib.search("shared task"); assert(h[0].skill_id=="new");
  for(auto& x:h) if(x.skill_id=="old") assert(x.state_multiplier==0.3);
}
TEST(test_archived_excluded_unless_requested) {
  auto d=dir("archived"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"gone","retired task")); lib.set_state("gone",State::Archived);
  assert(lib.search("retired task").empty()); assert(!lib.search("retired task",8,true).empty());
}
TEST(test_telemetry_boost_is_exposed) {
  auto d=dir("boost"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"boosted","boost task"));
  auto first=lib.search("boost task"); assert(first[0].telemetry_multiplier==1.0); lib.fetch_body("boosted");
  auto second=lib.search("boost task"); assert(second[0].telemetry_multiplier>1.0);
}
TEST(test_search_count_only_bumps_returned_hits) {
  auto d=dir("topn"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"a","shared task")); lib.register_skill(write_skill(d,"b","shared task"));
  lib.search("shared task",1); SkillRow a,b; lib.get_row("a",a); lib.get_row("b",b); assert(a.search_count+b.search_count==1);
}
TEST(test_fetch_exact_success_and_receipt) {
  auto d=dir("fetch_ok"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"f","fetch task","MARK")); auto h=lib.search("fetch task");
  auto r=lib.fetch("f","ctx",h[0].revision_id,h[0].catalog_generation); assert(r.pinned&&r.body.find("MARK")!=std::string::npos);
  assert(lib.count_fetch_receipts("OK")==1);
}
TEST(test_fetch_wrong_expected_revision_fails_closed) {
  auto d=dir("fetch_wrong"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"f","fetch task")); auto h=lib.search("fetch task");
  bool threw=false; try{lib.fetch("f","ctx","sha256:deadbeef",h[0].catalog_generation);}catch(const DbError& e){threw=std::string(e.what()).find("REVISION_MISMATCH")!=std::string::npos;} assert(threw);
  assert(lib.count_fetch_receipts("EXPECTED_REVISION_MISMATCH")==1); SkillRow row; lib.get_row("f",row); assert(row.fetch_count==0);
}
TEST(test_fetch_on_disk_drift_fails_closed) {
  auto d=dir("fetch_drift"); auto p=write_skill(d,"f","fetch task"); SkillLibrary lib(":memory:"); lib.register_skill(p); auto h=lib.search("fetch task"); append(p,"MUTATE");
  bool threw=false; try{lib.fetch("f","ctx",h[0].revision_id,h[0].catalog_generation);}catch(const DbError& e){threw=std::string(e.what()).find("REVISION_MISMATCH")!=std::string::npos;} assert(threw);
  assert(lib.count_fetch_receipts("INDEX_DRIFT")==1); SkillRow row; lib.get_row("f",row); assert(row.fetch_count==0&&row.state==State::Stale);
}
TEST(test_fetch_catalog_generation_mismatch_fails_closed) {
  auto d=dir("fetch_gen"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"f","fetch task")); auto h=lib.search("fetch task");
  lib.register_skill(write_skill(d,"other","other task")); bool threw=false;
  try{lib.fetch("f","ctx",h[0].revision_id,h[0].catalog_generation);}catch(const DbError& e){threw=std::string(e.what()).find("CATALOG_GENERATION_MISMATCH")!=std::string::npos;} assert(threw);
  assert(lib.count_fetch_receipts("CATALOG_GENERATION_MISMATCH")==1);
}
TEST(test_catalog_generation_stable_across_first_fetch) {
  auto d=dir("gen_stable"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"f","fetch task")); auto before=lib.catalog_generation(); lib.fetch_body("f"); auto after=lib.catalog_generation(); assert(before==after);
}
TEST(test_catalog_generation_changes_on_reindex) {
  auto d=dir("gen_change"); auto p=write_skill(d,"f","fetch task"); SkillLibrary lib(":memory:"); lib.register_skill(p); auto before=lib.catalog_generation(); write_skill(d,"f","changed task"); lib.register_skill(p); assert(before!=lib.catalog_generation());
}
TEST(test_unpinned_fetch_remains_operator_compatible) {
  auto d=dir("unpinned"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"f","fetch task","BODY")); auto r=lib.fetch("f"); assert(!r.pinned&&r.body.find("BODY")!=std::string::npos);
}
TEST(test_consumer_catalog_is_read_only) {
  auto d=dir("consumer"); auto db=(d/"catalog.db").string(); auto tele=(d/"telemetry.db").string(); auto p=write_skill(d,"c","consumer task"); {SkillLibrary op(db); op.register_skill(p);} SkillLibrary c(db,tele,CatalogAccess::ReadOnly);
  bool denied=false; try{c.set_state("c",State::Archived);}catch(const DbError& e){denied=std::string(e.what()).find("read-only")!=std::string::npos;} assert(denied);
}
TEST(test_consumer_cannot_register) {
  auto d=dir("consumer_reg"); auto db=(d/"catalog.db").string(); auto tele=(d/"telemetry.db").string(); auto p=write_skill(d,"c","consumer task"); {SkillLibrary op(db); op.register_skill(p);} SkillLibrary c(db,tele,CatalogAccess::ReadOnly);
  bool denied=false; try{c.register_skill(p);}catch(const DbError&){denied=true;} assert(denied);
}
TEST(test_consumer_search_fetch_with_separate_telemetry) {
  auto d=dir("consumer_io"); auto db=(d/"catalog.db").string(); auto tele=(d/"telemetry.db").string(); auto p=write_skill(d,"c","consumer task","BODY"); {SkillLibrary op(db); op.register_skill(p);} SkillLibrary c(db,tele,CatalogAccess::ReadOnly);
  auto h=c.search("consumer task"); auto r=c.fetch("c","ctx",h[0].revision_id,h[0].catalog_generation); assert(r.body.find("BODY")!=std::string::npos); auto t=c.telemetry(); assert(t.total_searches==1&&t.total_fetches==1);
}
TEST(test_shared_telemetry_visible_across_consumer_processes) {
  auto d=dir("consumer_shared"); auto db=(d/"catalog.db").string(); auto tele=(d/"telemetry.db").string(); auto p=write_skill(d,"c","consumer task"); {SkillLibrary op(db); op.register_skill(p);} {SkillLibrary a(db,tele,CatalogAccess::ReadOnly); auto h=a.search("consumer task"); a.fetch("c","",h[0].revision_id,h[0].catalog_generation);} {SkillLibrary b(db,tele,CatalogAccess::ReadOnly); SkillRow row; b.get_row("c",row); assert(row.search_count==1&&row.fetch_count==1);}
}
TEST(test_consumer_drift_detection_does_not_mutate_catalog) {
  auto d=dir("consumer_drift"); auto db=(d/"catalog.db").string(); auto tele=(d/"telemetry.db").string(); auto p=write_skill(d,"c","consumer task"); {SkillLibrary op(db); op.register_skill(p);} append(p,"MUTATE"); {SkillLibrary c(db,tele,CatalogAccess::ReadOnly); assert(c.mark_stale_if_drifted("c"));} {SkillLibrary op(db); SkillRow row; op.get_row("c",row); assert(row.state==State::Indexed);}
}
TEST(test_graveyard_uses_separate_telemetry) {
  auto d=dir("grave"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"g","generic broad task")); for(int i=0;i<6;++i) lib.search("generic broad task"); auto c=lib.graveyard_candidates(5); assert(c.size()==1&&c[0].skill_id=="g");
}
TEST(test_recent_events_chronological) {
  auto d=dir("events"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"e","event task")); lib.search("event task"); lib.fetch_body("e"); auto e=lib.recent_events(); assert(e.size()==2&&e[0].event=="SUGGESTED"&&e[1].event=="FETCHED");
}
TEST(test_used_event_logged) {
  SkillLibrary lib(":memory:"); lib.log_used("x","context"); assert(lib.count_events("x","USED")==1);
}
TEST(test_oversized_query_rejected) {
  SkillLibrary lib(":memory:"); bool threw=false; try{lib.search(std::string(kMaxQueryBytes+1,'x'));}catch(const DbError&){threw=true;} assert(threw);
}
TEST(test_persistence_across_reopen) {
  auto d=dir("persist"); auto db=(d/"catalog.db").string(); auto p=write_skill(d,"p","persist task"); {SkillLibrary a(db); a.register_skill(p); a.search("persist task");} {SkillLibrary b(db); SkillRow row; assert(b.get_row("p",row)&&row.search_count==1);}
}

TEST(test_mcp_json_nested) {
  auto v=mcp::json_parse(R"({"a":1,"b":[true,false,null,"x"],"c":{"d":"e"}})"); assert(v.is_obj()&&v.find("b")->arr.size()==4&&v.find("c")->find("d")->as_str()=="e");
}
TEST(test_mcp_json_unicode) {
  auto v=mcp::json_parse(R"({"s":"é \uD83D\uDE00"})"); assert(v.find("s")->str.find("\xf0\x9f\x98\x80")!=std::string::npos);
}
TEST(test_mcp_json_malformed) {
  bool threw=false; try{mcp::json_parse("{bad");}catch(...){threw=true;} assert(threw);
}
TEST(test_mcp_initialize) {
  SkillLibrary lib(":memory:"); auto r=mcp::handle_request(lib,R"({"jsonrpc":"2.0","id":1,"method":"initialize"})"); assert(r&&r->find("\"version\":\"1.1.0\"")!=std::string::npos);
}
TEST(test_mcp_tools_list_identity_contract) {
  SkillLibrary lib(":memory:"); auto r=mcp::handle_request(lib,R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"); assert(r&&r->find("expected_revision")!=std::string::npos&&r->find("catalog_generation")!=std::string::npos);
}
TEST(test_mcp_search_returns_explanation) {
  auto d=dir("mcp_search"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"m","mcp task")); auto r=mcp::handle_request(lib,R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"skill_search","arguments":{"query":"mcp task"}}})"); assert(r&&r->find("score_components")!=std::string::npos&&r->find("revision_id")!=std::string::npos);
}
TEST(test_mcp_fetch_requires_revision) {
  auto d=dir("mcp_req"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"m","mcp task")); auto r=mcp::handle_request(lib,R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"skill_fetch","arguments":{"skill_id":"m"}}})"); assert(r&&r->find("isError")!=std::string::npos&&r->find("expected_revision")!=std::string::npos);
}
TEST(test_mcp_exact_fetch_success) {
  auto d=dir("mcp_fetch"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"m","mcp task","MCPBODY")); auto h=lib.search("mcp task");
  std::string q="{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"skill_fetch\",\"arguments\":{\"skill_id\":\"m\",\"expected_revision\":\""+h[0].revision_id+"\",\"catalog_generation\":\""+h[0].catalog_generation+"\"}}}"; auto r=mcp::handle_request(lib,q); assert(r&&r->find("MCPBODY")!=std::string::npos&&r->find("isError")==std::string::npos);
}
TEST(test_mcp_exact_fetch_mismatch_is_tool_error) {
  auto d=dir("mcp_mismatch"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"m","mcp task")); auto h=lib.search("mcp task");
  std::string q="{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"skill_fetch\",\"arguments\":{\"skill_id\":\"m\",\"expected_revision\":\"sha256:bad\",\"catalog_generation\":\""+h[0].catalog_generation+"\"}}}"; auto r=mcp::handle_request(lib,q); assert(r&&r->find("isError")!=std::string::npos&&r->find("REVISION_MISMATCH")!=std::string::npos);
}
TEST(test_mcp_resources_are_revision_pinned) {
  auto d=dir("mcp_res"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"m","resource task")); auto r=mcp::handle_request(lib,R"({"jsonrpc":"2.0","id":5,"method":"resources/list"})"); assert(r&&r->find("skill://m@sha256:")!=std::string::npos);
}
TEST(test_mcp_revision_pinned_resource_read) {
  auto d=dir("mcp_read"); SkillLibrary lib(":memory:"); lib.register_skill(write_skill(d,"m","resource task","RESOURCEBODY")); SkillRow row; lib.get_row("m",row); std::string q="{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"resources/read\",\"params\":{\"uri\":\"skill://m@"+row.content_hash+"\"}}"; auto r=mcp::handle_request(lib,q); assert(r&&r->find("RESOURCEBODY")!=std::string::npos);
}
TEST(test_mcp_unpinned_resource_rejected) {
  SkillLibrary lib(":memory:"); auto r=mcp::handle_request(lib,R"({"jsonrpc":"2.0","id":7,"method":"resources/read","params":{"uri":"skill://m"}})"); assert(r&&r->find("\"code\":-32602")!=std::string::npos);
}
TEST(test_mcp_stats_exposes_policy_generation_access) {
  SkillLibrary lib(":memory:"); auto r=mcp::handle_request(lib,R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"skill_stats","arguments":{}}})"); assert(r&&r->find(kRankingPolicy)!=std::string::npos&&r->find("catalog_generation")!=std::string::npos&&r->find("catalog_access")!=std::string::npos);
}
TEST(test_mcp_notification_no_reply) {
  SkillLibrary lib(":memory:"); assert(!mcp::handle_request(lib,R"({"jsonrpc":"2.0","method":"notifications/initialized"})").has_value());
}
TEST(test_mcp_unknown_method) {
  SkillLibrary lib(":memory:"); auto r=mcp::handle_request(lib,R"({"jsonrpc":"2.0","id":9,"method":"nope"})"); assert(r&&r->find("\"code\":-32601")!=std::string::npos);
}
TEST(test_mcp_parse_error_null_id) {
  SkillLibrary lib(":memory:"); auto r=mcp::handle_request(lib,"bad"); assert(r&&r->find("\"code\":-32700")!=std::string::npos&&r->find("\"id\":null")!=std::string::npos);
}

int main() {
  for (auto& [name, fn] : tests()) {
    fn(); std::printf("PASS  %s\n", name); ++g_pass;
  }
  std::printf("\n%d/%zu passed\n", g_pass, tests().size());
  return g_pass == static_cast<int>(tests().size()) ? 0 : 1;
}
