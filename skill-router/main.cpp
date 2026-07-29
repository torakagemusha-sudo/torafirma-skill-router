// main.cpp - consolidated CLI, shell, loopback HTTP and MCP interfaces.
#include "skill_library.hpp"
#include "mcp_server.hpp"

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <fcntl.h>
  #include <io.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  #define CLOSESOCK closesocket
  static void platform_net_init() { WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa); }
  static void platform_net_cleanup() { WSACleanup(); }
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using socket_t = int;
  #define CLOSESOCK close
  static void platform_net_init() {}
  static void platform_net_cleanup() {}
#endif

#include <algorithm>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace skilllib;

namespace {

constexpr std::size_t kMaxRequestBytes = 2u * 1024u * 1024u;

struct Args {
  std::string db = "skill_index.db";
  std::string telemetry_db;
  std::string role;
  std::string target, query, context_query, format = "text", mode = "hybrid";
  std::string expected_revision, expected_catalog_generation;
  int top = 8, port = 8090;
  long long min_searches = 5;
  bool include_archived = false;
};

Args parse(int argc, char** argv, int start) {
  Args a;
  for (int i = start; i < argc; ++i) {
    const std::string s = argv[i];
    auto next = [&](const char* opt) -> std::string {
      if (i + 1 >= argc) throw std::runtime_error(std::string("missing value for ") + opt);
      return argv[++i];
    };
    if (s == "--db") a.db = next("--db");
    else if (s == "--telemetry-db") a.telemetry_db = next("--telemetry-db");
    else if (s == "--role") a.role = next("--role");
    else if (s == "--top") a.top = std::stoi(next("--top"));
    else if (s == "--json") a.format = "json";
    else if (s == "--port") a.port = std::stoi(next("--port"));
    else if (s == "--context") a.context_query = next("--context");
    else if (s == "--mode") a.mode = next("--mode");
    else if (s == "--revision") a.expected_revision = next("--revision");
    else if (s == "--catalog-generation") a.expected_catalog_generation = next("--catalog-generation");
    else if (s == "--min-searches") a.min_searches = std::stoll(next("--min-searches"));
    else if (s == "--include-archived") a.include_archived = true;
    else if (a.target.empty()) a.target = s;
    else { if (!a.query.empty()) a.query += ' '; a.query += s; }
  }
  if (a.query.empty() && !a.target.empty()) a.query = a.target;
  if (!a.role.empty() && a.role != "consumer" && a.role != "operator")
    throw std::runtime_error("--role must be consumer or operator");
  return a;
}

CatalogAccess access_for(const Args& a, bool default_consumer) {
  if (a.role == "consumer") return CatalogAccess::ReadOnly;
  if (a.role == "operator") return CatalogAccess::ReadWrite;
  return default_consumer ? CatalogAccess::ReadOnly : CatalogAccess::ReadWrite;
}

std::unique_ptr<SkillLibrary> make_library(const Args& a, bool default_consumer) {
  return std::make_unique<SkillLibrary>(a.db, a.telemetry_db, access_for(a, default_consumer));
}

void require_operator(const Args& a, const char* command) {
  if (a.role == "consumer")
    throw std::runtime_error(std::string(command) + " is unavailable in consumer role");
}

void walk_for_skill_md(const std::string& root, std::vector<std::string>& out) {
  std::error_code ec;
  if (!fs::exists(root, ec) || ec) return;
  for (const auto& entry : fs::recursive_directory_iterator(
           root, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec) || ec) continue;
    const std::string name = entry.path().filename().string();
    if (name == "SKILL.md" || (name.size() > 9 && name.compare(name.size()-9,9,"_SKILL.md") == 0))
      out.push_back(entry.path().string());
  }
}

int cmd_register(const Args& a) {
  require_operator(a, "register");
  if (a.query.empty()) throw std::runtime_error("register requires a SKILL.md path");
  SkillLibrary lib(a.db, a.telemetry_db, CatalogAccess::ReadWrite);
  const auto r = lib.register_skill(a.query);
  if (!r.ok) {
    std::cout << "{\"ok\":false,\"error\":\"" << json_escape(r.error) << "\"}\n";
    return 1;
  }
  std::cout << "{\"ok\":true,\"skill_id\":\"" << json_escape(r.skill_id)
            << "\",\"skill_version\":\"" << json_escape(r.skill_version)
            << "\",\"revision_id\":\"" << json_escape(r.revision_id)
            << "\",\"catalog_generation\":\"" << lib.catalog_generation()
            << "\",\"created\":" << (r.created?"true":"false")
            << ",\"updated\":" << (r.updated?"true":"false") << "}\n";
  return 0;
}

int cmd_index(const Args& a) {
  require_operator(a, "index");
  if (a.query.empty()) throw std::runtime_error("index requires a root directory");
  SkillLibrary lib(a.db, a.telemetry_db, CatalogAccess::ReadWrite);
  std::vector<std::string> files; walk_for_skill_md(a.query, files);
  int created=0, updated=0, unchanged=0, errors=0;
  for (const auto& f : files) {
    const auto r=lib.register_skill(f);
    if (!r.ok) { ++errors; std::cerr << "  ! " << f << ": " << r.error << '\n'; }
    else if (r.created) ++created;
    else if (r.updated) ++updated;
    else ++unchanged;
  }
  std::cout << "{\"ok\":true,\"scanned\":" << files.size()
            << ",\"created\":" << created << ",\"updated\":" << updated
            << ",\"unchanged\":" << unchanged << ",\"errors\":" << errors
            << ",\"catalog_generation\":\"" << lib.catalog_generation() << "\"}\n";
  return errors ? 1 : 0;
}

int cmd_search(const Args& a) {
  if (a.query.empty()) throw std::runtime_error("search requires a query string");
  auto lib=make_library(a,true);
  const auto hits=lib->search(a.query,a.top,a.include_archived,search_mode_from_string(a.mode));
  if (a.format == "json") {
    std::cout << mcp::search_hits_json(hits) << '\n';
  } else {
    if (hits.empty()) { std::cout << "(no matching skills)\n"; return 0; }
    for (const auto& h : hits) {
      std::cout << h.skill_id << "  (score " << h.score << ", " << to_string(h.state) << ")\n"
                << "    " << h.description << "\n"
                << "    version=" << h.skill_version << " revision=" << h.revision_id << "\n"
                << "    catalog_generation=" << h.catalog_generation << "\n";
    }
  }
  return 0;
}

int cmd_fetch(const Args& a) {
  if (a.query.empty()) throw std::runtime_error("fetch requires a skill_id");
  auto lib=make_library(a,true);
  const bool consumer=lib->catalog_read_only();
  if (consumer && (a.expected_revision.empty() || a.expected_catalog_generation.empty()))
    throw std::runtime_error("consumer fetch requires --revision and --catalog-generation from search output");
  const auto result=lib->fetch(a.query,a.context_query,a.expected_revision,a.expected_catalog_generation);
  std::cout << result.body;
  return 0;
}

int cmd_use(const Args& a) {
  if (a.query.empty()) throw std::runtime_error("use requires a skill_id");
  auto lib=make_library(a,true); lib->log_used(a.query,a.context_query);
  std::cout << "{\"ok\":true,\"skill_id\":\"" << json_escape(a.query) << "\",\"event\":\"USED\"}\n";
  return 0;
}

int cmd_stats(const Args& a) {
  auto lib=make_library(a,true); std::cout << mcp::stats_json(*lib) << '\n'; return 0;
}

int cmd_graveyard(const Args& a) {
  auto lib=make_library(a,true); std::cout << mcp::graveyard_json(*lib,a.min_searches) << '\n'; return 0;
}

int cmd_deprecate(const Args& a) {
  require_operator(a,"deprecate");
  if (a.query.empty()) throw std::runtime_error("deprecate requires a skill_id");
  SkillLibrary lib(a.db,a.telemetry_db,CatalogAccess::ReadWrite); lib.set_state(a.query,State::Deprecated);
  std::cout << "{\"ok\":true,\"skill_id\":\"" << json_escape(a.query)
            << "\",\"state\":\"DEPRECATED\",\"catalog_generation\":\""
            << lib.catalog_generation() << "\"}\n"; return 0;
}

int cmd_archive(const Args& a) {
  require_operator(a,"archive");
  if (a.query.empty()) throw std::runtime_error("archive requires a skill_id");
  SkillLibrary lib(a.db,a.telemetry_db,CatalogAccess::ReadWrite); lib.set_state(a.query,State::Archived);
  std::cout << "{\"ok\":true,\"skill_id\":\"" << json_escape(a.query)
            << "\",\"state\":\"ARCHIVED\",\"catalog_generation\":\""
            << lib.catalog_generation() << "\"}\n"; return 0;
}

void render_dashboard(SkillLibrary& lib, int event_tail=8) {
  const auto counts=lib.state_counts(); const auto t=lib.telemetry();
  long long total=0; for (const auto& [k,v]:counts) { (void)k; total+=v; }
  std::cout << "\n=== skillrouter live status =============================================\n"
            << " engine v" << kEngineVersion << "  policy " << kRankingPolicy
            << "  access " << (lib.catalog_read_only()?"consumer/read-only":"operator/read-write") << "\n"
            << " catalog_generation " << lib.catalog_generation() << "\n"
            << " skills " << total << "  events " << lib.total_events() << "\n"
            << " suggestions " << t.total_searches << "  fetches " << t.total_fetches
            << "  conversion " << t.overall_conversion << "\n";
  const auto ev=lib.recent_events(event_tail);
  for (const auto& e:ev) {
    std::cout << "   " << (e.ts.empty()?"--":e.ts) << "  " << e.event << "  " << e.skill_id;
    if (!e.query.empty()) std::cout << "  q=\"" << e.query << "\"";
    std::cout << '\n';
  }
  std::cout << "========================================================================\n";
}

void shell_help() {
  std::cout << "commands:\n"
            << "  search <query>\n  fetch <skill_id>\n  events [N]\n  stats\n  graveyard\n"
            << "  deprecate <skill_id>\n  archive <skill_id>\n  status\n  help\n  quit\n";
}

int cmd_shell(const Args& a) {
  require_operator(a,"shell");
  SkillLibrary lib(a.db,a.telemetry_db,CatalogAccess::ReadWrite);
  std::cout << "skillrouter operator shell v" << kEngineVersion << "\n"; shell_help();
  std::string line;
  while (true) {
    render_dashboard(lib); std::cout << "skillrouter> " << std::flush;
    if (!std::getline(std::cin,line)) break;
    if (line.empty()) continue;
    std::istringstream iss(line); std::string cmd; iss>>cmd; std::string rest; std::getline(iss,rest);
    if (!rest.empty()&&rest[0]==' ') rest.erase(0,1);
    try {
      if (cmd=="quit"||cmd=="exit") break;
      if (cmd=="help") shell_help();
      else if (cmd=="status") {}
      else if (cmd=="search") {
        const auto hits=lib.search(rest);
        for (const auto& h:hits) std::cout << "  " << h.skill_id << " " << h.score << " " << h.revision_id << "\n";
      } else if (cmd=="fetch") std::cout << lib.fetch_body(rest) << '\n';
      else if (cmd=="events") {
        int n=20; if (!rest.empty()) try { n=std::max(1,std::stoi(rest)); } catch (...) {}
        for (const auto& e:lib.recent_events(n)) std::cout << e.ts << " " << e.event << " " << e.skill_id << "\n";
      } else if (cmd=="stats") std::cout << mcp::stats_json(lib) << '\n';
      else if (cmd=="graveyard") std::cout << mcp::graveyard_json(lib,5) << '\n';
      else if (cmd=="deprecate") { lib.set_state(rest,State::Deprecated); std::cout << "ok\n"; }
      else if (cmd=="archive") { lib.set_state(rest,State::Archived); std::cout << "ok\n"; }
      else std::cout << "unknown command\n";
    } catch (const std::exception& e) { std::cout << "error: " << e.what() << '\n'; }
  }
  return 0;
}

int cmd_mcp(const Args& a) {
#ifdef _WIN32
  _setmode(_fileno(stdout),_O_BINARY); _setmode(_fileno(stdin),_O_BINARY);
#endif
  auto lib=make_library(a,true);
  std::cerr << "skillrouter MCP stdio v" << kEngineVersion << " catalog=" << a.db
            << " telemetry=" << lib->telemetry_path() << " access="
            << (lib->catalog_read_only()?"read-only":"read-write") << "\n";
  std::string line;
  while (std::getline(std::cin,line)) {
    if (!line.empty()&&line.back()=='\r') line.pop_back();
    if (line.empty()) continue;
    const auto response=mcp::handle_request(*lib,line);
    if (response) std::cout << *response << '\n' << std::flush;
  }
  return 0;
}

volatile std::sig_atomic_t g_stop=0;
void on_sig(int) { g_stop=1; }
void install_shutdown_handler() {
#ifdef _WIN32
  std::signal(SIGINT,on_sig); std::signal(SIGTERM,on_sig);
#else
  struct sigaction sa{}; sa.sa_handler=on_sig; sigaction(SIGINT,&sa,nullptr); sigaction(SIGTERM,&sa,nullptr);
#endif
}

std::string http_response(int code,const std::string& ctype,const std::string& body) {
  const char* status=code==200?"200 OK":code==404?"404 Not Found":code==409?"409 Conflict":code==413?"413 Payload Too Large":"400 Bad Request";
  std::ostringstream o; o << "HTTP/1.1 " << status << "\r\nContent-Type: " << ctype
    << "\r\nContent-Length: " << body.size() << "\r\nConnection: close\r\n\r\n" << body; return o.str();
}
std::string url_decode(const std::string& s) {
  std::string out; out.reserve(s.size());
  for (std::size_t i=0;i<s.size();++i) {
    if (s[i]=='%'&&i+2<s.size()&&std::isxdigit(static_cast<unsigned char>(s[i+1]))&&std::isxdigit(static_cast<unsigned char>(s[i+2]))) {
      auto hex=[](char c){return std::isdigit(static_cast<unsigned char>(c))?c-'0':std::tolower(static_cast<unsigned char>(c))-'a'+10;};
      out+=static_cast<char>((hex(s[i+1])<<4)|hex(s[i+2])); i+=2;
    } else if (s[i]=='+') out+=' '; else out+=s[i];
  }
  return out;
}
struct ParsedPath { std::string path; std::map<std::string,std::string> query; };
ParsedPath parse_path(const std::string& raw) {
  ParsedPath p; const auto q=raw.find('?'); p.path=q==std::string::npos?raw:raw.substr(0,q);
  if (q==std::string::npos) return p;
  const std::string qs=raw.substr(q+1); std::size_t i=0;
  while (i<qs.size()) {
    const auto amp=qs.find('&',i); const std::string pair=qs.substr(i,amp==std::string::npos?std::string::npos:amp-i);
    const auto eq=pair.find('='); if (eq!=std::string::npos) p.query[url_decode(pair.substr(0,eq))]=url_decode(pair.substr(eq+1));
    if (amp==std::string::npos) break;
    i=amp+1;
  }
  return p;
}
std::string recv_request(socket_t c,bool& too_big) {
  std::string req; char buf[8192]; std::size_t hdr_end=std::string::npos, content_len=0; too_big=false;
#ifdef _WIN32
  int r;
#else
  ssize_t r;
#endif
  while ((r=::recv(c,buf,sizeof(buf),0))>0) {
    req.append(buf,static_cast<std::size_t>(r)); if (req.size()>kMaxRequestBytes) {too_big=true;return "";}
    if (hdr_end==std::string::npos) {
      hdr_end=req.find("\r\n\r\n");
      if (hdr_end!=std::string::npos) { const auto p=req.find("Content-Length:"); if (p!=std::string::npos) try{content_len=std::stoul(req.substr(p+15));}catch(...){content_len=0;} }
    }
    if (hdr_end!=std::string::npos&&req.size()>=hdr_end+4+content_len) break;
  }
  return req;
}

int cmd_serve(const Args& a) {
  platform_net_init(); auto lib=make_library(a,true);
  socket_t srv=::socket(AF_INET,SOCK_STREAM,0);
#ifdef _WIN32
  if (srv==INVALID_SOCKET) throw std::runtime_error("socket failed");
#else
  if (srv<0) throw std::runtime_error("socket failed");
#endif
  int one=1; setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&one),sizeof(one));
  sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK); addr.sin_port=htons(static_cast<std::uint16_t>(a.port));
  if (::bind(srv,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))!=0) throw std::runtime_error("bind failed");
  if (::listen(srv,16)!=0) throw std::runtime_error("listen failed");
  install_shutdown_handler();
  std::cerr << "skillrouter HTTP on 127.0.0.1:" << a.port << " access=" << (lib->catalog_read_only()?"read-only":"read-write") << "\n";
  while (!g_stop) {
    socket_t c=::accept(srv,nullptr,nullptr);
#ifdef _WIN32
    if (c==INVALID_SOCKET) {if(g_stop)break;continue;}
#else
    if (c<0) {if(g_stop)break;continue;}
#endif
    std::string resp;
    try {
      bool too_big=false; const std::string req=recv_request(c,too_big);
      if (too_big) resp=http_response(413,"application/json","{\"ok\":false,\"error\":\"request too large\"}");
      else {
        std::istringstream head(req.substr(0,req.find("\r\n"))); std::string method,raw; head>>method>>raw; const auto pp=parse_path(raw);
        if (method=="GET"&&pp.path=="/health") resp=http_response(200,"application/json",mcp::stats_json(*lib));
        else if (method=="GET"&&pp.path=="/stats") resp=http_response(200,"application/json",mcp::stats_json(*lib));
        else if (method=="GET"&&pp.path=="/graveyard") resp=http_response(200,"application/json",mcp::graveyard_json(*lib,5));
        else if (method=="GET"&&pp.path=="/search") {
          const auto it=pp.query.find("q"); if(it==pp.query.end()) resp=http_response(400,"application/json","{\"ok\":false,\"error\":\"missing q\"}");
          else { const auto mt=pp.query.find("mode"); const auto sm=mt==pp.query.end()?SearchMode::Hybrid:search_mode_from_string(mt->second); resp=http_response(200,"application/json",mcp::search_hits_json(lib->search(it->second,8,false,sm))); }
        } else if (method=="GET"&&pp.path=="/fetch") {
          const auto id=pp.query.find("id"), rev=pp.query.find("revision"), gen=pp.query.find("catalog_generation");
          if(id==pp.query.end()||rev==pp.query.end()||gen==pp.query.end()) resp=http_response(400,"application/json","{\"ok\":false,\"error\":\"id, revision and catalog_generation are required\"}");
          else try {resp=http_response(200,"text/plain; charset=utf-8",lib->fetch_body(id->second,"http",rev->second,gen->second));}
          catch(const DbError& e){const std::string m=e.what(); resp=http_response(m.find("MISMATCH")!=std::string::npos?409:404,"application/json","{\"ok\":false,\"error\":\""+json_escape(m)+"\"}");}
        } else resp=http_response(404,"application/json","{\"ok\":false,\"error\":\"not found\"}");
      }
    } catch(const std::exception& e) {resp=http_response(400,"application/json","{\"ok\":false,\"error\":\""+json_escape(e.what())+"\"}");}
    std::size_t sent=0; while(sent<resp.size()) {const int chunk=static_cast<int>(std::min(resp.size()-sent,static_cast<std::size_t>(std::numeric_limits<int>::max()))); const auto n=::send(c,resp.data()+sent,chunk,0); if(n<=0)break; sent+=static_cast<std::size_t>(n);} CLOSESOCK(c);
  }
  CLOSESOCK(srv); platform_net_cleanup(); return 0;
}

void usage() {
  std::cerr << "skillrouter v" << kEngineVersion << " - deterministic content-addressed skill routing\n"
    << "  skillrouter                           operator shell\n"
    << "  skillrouter register <SKILL.md>       [--db PATH] [--telemetry-db PATH]\n"
    << "  skillrouter index <root>              [--db PATH] [--telemetry-db PATH]\n"
    << "  skillrouter search \"query\"           [--db PATH] [--telemetry-db PATH] [--json] [--mode ...]\n"
    << "  skillrouter fetch <skill_id>          --revision SHA256 --catalog-generation SHA256 [--db PATH]\n"
    << "  skillrouter stats|graveyard|use       [--db PATH] [--telemetry-db PATH]\n"
    << "  skillrouter deprecate|archive <id>    [--role operator]\n"
    << "  skillrouter serve|mcp                 [--db PATH] [--telemetry-db PATH] [--role consumer|operator]\n"
    << "Consumer role is the default for search/fetch/stats/serve/mcp; operator role is required for publication and lifecycle writes.\n";
}

} // namespace

int main(int argc,char** argv) {
  if(argc<2) {try{Args a; return cmd_shell(a);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
  const std::string cmd=argv[1]; if(cmd=="--help"||cmd=="-h"||cmd=="help"){usage();return 0;}
  try {
    const Args a=parse(argc,argv,2);
    if(cmd=="register") return cmd_register(a);
    if(cmd=="index") return cmd_index(a);
    if(cmd=="search") return cmd_search(a);
    if(cmd=="fetch") return cmd_fetch(a);
    if(cmd=="use") return cmd_use(a);
    if(cmd=="stats") return cmd_stats(a);
    if(cmd=="graveyard") return cmd_graveyard(a);
    if(cmd=="deprecate") return cmd_deprecate(a);
    if(cmd=="archive") return cmd_archive(a);
    if(cmd=="shell") return cmd_shell(a);
    if(cmd=="serve") return cmd_serve(a);
    if(cmd=="mcp") return cmd_mcp(a);
    usage(); return 2;
  } catch(const std::exception& e) {std::cerr << "{\"ok\":false,\"error\":\"" << json_escape(e.what()) << "\"}\n"; return 1;}
}
