// skill_library.hpp - deterministic, content-addressed skill routing engine.
#pragma once

#include "third_party/sqlite3.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace skilllib {

inline constexpr const char* kEngineVersion = "1.1.0";
inline constexpr const char* kRankingPolicy = "tf.skillrouter.hybrid-lexical.v1";
inline constexpr std::size_t kMaxDescBytes = 4096;
inline constexpr std::size_t kMaxQueryBytes = 8192;
inline constexpr std::size_t kMaxSkillBytes = 8u * 1024u * 1024u;

// ---------- utilities -------------------------------------------------------

inline std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

inline std::string json_escape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          char b[8];
          std::snprintf(b, sizeof(b), "\\u%04x", c);
          o += b;
        } else {
          o += static_cast<char>(c);
        }
    }
  }
  return o;
}

inline const std::set<std::string>& stopwords() {
  static const std::set<std::string> kStop = {
      "a","an","the","and","or","of","to","in","on","for","with","is","are",
      "be","it","this","that","use","uses","using","when","whenever","also",
      "trigger","triggers","user","users","should","would","can","will",
      "any","all","not","from","into","by","at","as"};
  return kStop;
}

inline std::vector<std::string> tokenize(const std::string& text) {
  std::vector<std::string> out;
  std::set<std::string> seen;
  std::string cur;
  auto flush = [&] {
    if (cur.size() > 1 && !stopwords().count(cur) && seen.insert(cur).second)
      out.push_back(cur);
    cur.clear();
  };
  for (unsigned char c : text) {
    if (std::isalnum(c) || c == '_') cur += static_cast<char>(std::tolower(c));
    else flush();
  }
  flush();
  return out;
}

inline std::string join_tokens(const std::vector<std::string>& tokens) {
  std::string out;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (i) out += ' ';
    out += tokens[i];
  }
  return out;
}

inline int bounded_edit_distance(const std::string& a, const std::string& b, int maxd) {
  const int n = static_cast<int>(a.size()), m = static_cast<int>(b.size());
  if (std::abs(n - m) > maxd) return maxd + 1;
  std::vector<int> prev(m + 1), cur(m + 1);
  for (int j = 0; j <= m; ++j) prev[j] = j;
  for (int i = 1; i <= n; ++i) {
    cur[0] = i;
    int row_min = cur[0];
    for (int j = 1; j <= m; ++j) {
      const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
      row_min = std::min(row_min, cur[j]);
    }
    if (row_min > maxd) return maxd + 1;
    std::swap(prev, cur);
  }
  return prev[m];
}

// Small stdlib-only SHA-256 implementation. Revision IDs are protocol-facing,
// cross-language identities; FNV-style hashes are not strong enough for that role.
class Sha256 {
 public:
  Sha256() { reset(); }

  void update(const unsigned char* data, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
      data_[datalen_++] = data[i];
      if (datalen_ == 64) {
        transform();
        bitlen_ += 512;
        datalen_ = 0;
      }
    }
  }

  std::array<unsigned char, 32> final() {
    std::array<unsigned char, 32> hash{};
    std::size_t i = datalen_;
    data_[i++] = 0x80;
    if (i > 56) {
      while (i < 64) data_[i++] = 0;
      transform();
      i = 0;
    }
    while (i < 56) data_[i++] = 0;

    bitlen_ += static_cast<std::uint64_t>(datalen_) * 8;
    for (int j = 0; j < 8; ++j)
      data_[63 - j] = static_cast<unsigned char>((bitlen_ >> (j * 8)) & 0xff);
    transform();

    for (i = 0; i < 4; ++i) {
      hash[i]      = static_cast<unsigned char>((state_[0] >> (24 - i * 8)) & 0xff);
      hash[i + 4]  = static_cast<unsigned char>((state_[1] >> (24 - i * 8)) & 0xff);
      hash[i + 8]  = static_cast<unsigned char>((state_[2] >> (24 - i * 8)) & 0xff);
      hash[i + 12] = static_cast<unsigned char>((state_[3] >> (24 - i * 8)) & 0xff);
      hash[i + 16] = static_cast<unsigned char>((state_[4] >> (24 - i * 8)) & 0xff);
      hash[i + 20] = static_cast<unsigned char>((state_[5] >> (24 - i * 8)) & 0xff);
      hash[i + 24] = static_cast<unsigned char>((state_[6] >> (24 - i * 8)) & 0xff);
      hash[i + 28] = static_cast<unsigned char>((state_[7] >> (24 - i * 8)) & 0xff);
    }
    return hash;
  }

 private:
  static constexpr std::array<std::uint32_t, 64> k_ = {
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

  static std::uint32_t rotr(std::uint32_t x, std::uint32_t n) { return (x >> n) | (x << (32 - n)); }
  static std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return (x & y) ^ (~x & z); }
  static std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
  static std::uint32_t ep0(std::uint32_t x) { return rotr(x,2) ^ rotr(x,13) ^ rotr(x,22); }
  static std::uint32_t ep1(std::uint32_t x) { return rotr(x,6) ^ rotr(x,11) ^ rotr(x,25); }
  static std::uint32_t sig0(std::uint32_t x) { return rotr(x,7) ^ rotr(x,18) ^ (x >> 3); }
  static std::uint32_t sig1(std::uint32_t x) { return rotr(x,17) ^ rotr(x,19) ^ (x >> 10); }

  void reset() {
    datalen_ = 0;
    bitlen_ = 0;
    state_ = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
              0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    data_.fill(0);
  }

  void transform() {
    std::uint32_t m[64];
    for (std::size_t i = 0, j = 0; i < 16; ++i, j += 4)
      m[i] = (static_cast<std::uint32_t>(data_[j]) << 24) |
             (static_cast<std::uint32_t>(data_[j + 1]) << 16) |
             (static_cast<std::uint32_t>(data_[j + 2]) << 8) |
             static_cast<std::uint32_t>(data_[j + 3]);
    for (std::size_t i = 16; i < 64; ++i)
      m[i] = sig1(m[i - 2]) + m[i - 7] + sig0(m[i - 15]) + m[i - 16];

    std::uint32_t a=state_[0], b=state_[1], c=state_[2], d=state_[3];
    std::uint32_t e=state_[4], f=state_[5], g=state_[6], h=state_[7];
    for (std::size_t i = 0; i < 64; ++i) {
      const std::uint32_t t1 = h + ep1(e) + ch(e,f,g) + k_[i] + m[i];
      const std::uint32_t t2 = ep0(a) + maj(a,b,c);
      h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
    state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
  }

  std::array<unsigned char, 64> data_{};
  std::size_t datalen_ = 0;
  std::uint64_t bitlen_ = 0;
  std::array<std::uint32_t, 8> state_{};
};

inline std::string sha256_hex(const std::string& input) {
  Sha256 h;
  h.update(reinterpret_cast<const unsigned char*>(input.data()), input.size());
  const auto bytes = h.final();
  std::ostringstream o;
  o << std::hex << std::setfill('0');
  for (unsigned char b : bytes) o << std::setw(2) << static_cast<unsigned int>(b);
  return o.str();
}

inline std::string revision_id(const std::string& body) {
  return "sha256:" + sha256_hex(body);
}

inline std::string length_field(const std::string& s) {
  return std::to_string(s.size()) + ":" + s + ";";
}

// ---------- minimal frontmatter parser ------------------------------------

struct Frontmatter {
  std::string name;
  std::string description;
  std::string version = "1.0.0";
  bool valid = false;
};

inline bool looks_like_xml_tag(const std::string& s) {
  std::size_t i = 0;
  while (i < s.size()) {
    const std::size_t lt = s.find('<', i);
    if (lt == std::string::npos) return false;
    const std::size_t gt = s.find('>', lt);
    if (gt == std::string::npos) return false;
    std::string inner = s.substr(lt + 1, gt - lt - 1);
    if (!inner.empty() && inner[0] == '/') inner = inner.substr(1);
    bool tag_shaped = !inner.empty() &&
        (std::isalpha(static_cast<unsigned char>(inner[0])) || inner[0] == '_');
    if (tag_shaped) {
      for (unsigned char c : inner) {
        if (!(std::isalnum(c) || c == '_' || c == ' ' || c == '/' || c == '-')) {
          tag_shaped = false;
          break;
        }
      }
    }
    if (tag_shaped) return true;
    i = gt + 1;
  }
  return false;
}

inline Frontmatter parse_frontmatter(const std::string& text) {
  Frontmatter fm;
  std::string normalized = text;
  normalized.erase(std::remove(normalized.begin(), normalized.end(), '\r'), normalized.end());
  if (normalized.compare(0, 4, "---\n") != 0) return fm;
  const auto end = normalized.find("\n---\n", 4);
  if (end == std::string::npos) return fm;
  const std::string block = normalized.substr(4, end - 4);
  std::vector<std::string> lines;
  { std::istringstream iss(block); std::string l; while (std::getline(iss, l)) lines.push_back(l); }

  std::map<std::string, std::string> kv;
  std::size_t i = 0;
  while (i < lines.size()) {
    const std::string& line = lines[i];
    const auto colon = line.find(':');
    if (colon == std::string::npos || colon == 0 || std::isspace(static_cast<unsigned char>(line[0]))) {
      ++i; continue;
    }
    const std::string key = line.substr(0, colon);
    std::string rest = line.substr(colon + 1);
    const std::size_t s = rest.find_first_not_of(' ');
    rest = (s == std::string::npos) ? "" : rest.substr(s);
    if (rest == ">" || rest == "|") {
      std::string block_val;
      ++i;
      while (i < lines.size() && (lines[i].empty() ||
             std::isspace(static_cast<unsigned char>(lines[i][0])))) {
        std::string t = lines[i];
        const std::size_t ts = t.find_first_not_of(' ');
        if (ts != std::string::npos) {
          if (!block_val.empty()) block_val += ' ';
          block_val += t.substr(ts);
        }
        ++i;
      }
      kv[key] = block_val;
      continue;
    }
    if (rest.size() >= 2 && (rest.front() == '"' || rest.front() == '\'') && rest.back() == rest.front())
      rest = rest.substr(1, rest.size() - 2);
    kv[key] = rest;
    ++i;
  }
  fm.name = kv.count("name") ? kv["name"] : "";
  fm.description = kv.count("description") ? kv["description"] : "";
  if (kv.count("version") && !kv["version"].empty()) fm.version = kv["version"];
  fm.valid = !fm.name.empty() && !fm.description.empty();
  return fm;
}

// ---------- SQLite wrappers ------------------------------------------------

class DbError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class Stmt {
 public:
  Stmt(sqlite3* db, const std::string& sql) {
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &s_, nullptr) != SQLITE_OK)
      throw DbError(std::string("prepare: ") + sqlite3_errmsg(db) + " in " + sql);
  }
  ~Stmt() { sqlite3_finalize(s_); }
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;
  Stmt& bind(int i, const std::string& v) { sqlite3_bind_text(s_, i, v.c_str(), -1, SQLITE_TRANSIENT); return *this; }
  Stmt& bind(int i, double v) { sqlite3_bind_double(s_, i, v); return *this; }
  Stmt& bind(int i, long long v) { sqlite3_bind_int64(s_, i, v); return *this; }
  bool step() {
    const int rc = sqlite3_step(s_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw DbError(std::string("step rc=") + std::to_string(rc) + ": " + sqlite3_errmsg(sqlite3_db_handle(s_)));
  }
  long long col_i(int i) const { return sqlite3_column_int64(s_, i); }
  double col_d(int i) const { return sqlite3_column_double(s_, i); }
  std::string col_s(int i) const {
    const unsigned char* t = sqlite3_column_text(s_, i);
    return t ? reinterpret_cast<const char*>(t) : "";
  }
 private:
  sqlite3_stmt* s_ = nullptr;
};

class Db {
 public:
  explicit Db(const std::string& path, bool read_only = false) : read_only_(read_only) {
    const int flags = read_only
        ? (SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX)
        : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX);
    if (sqlite3_open_v2(path.c_str(), &h_, flags, nullptr) != SQLITE_OK) {
      const std::string err = h_ ? sqlite3_errmsg(h_) : "unknown";
      if (h_) sqlite3_close(h_);
      h_ = nullptr;
      throw DbError("open failed: " + path + ": " + err);
    }
    sqlite3_busy_timeout(h_, 5000);
    if (read_only_) {
      exec("PRAGMA query_only=ON;");
    } else {
      exec("PRAGMA journal_mode=WAL;");
      exec("PRAGMA foreign_keys=ON;");
    }
  }
  ~Db() { if (h_) sqlite3_close(h_); }
  Db(const Db&) = delete;
  Db& operator=(const Db&) = delete;
  void exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(h_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
      const std::string m = err ? err : "exec failed";
      sqlite3_free(err);
      throw DbError(m + " in " + sql);
    }
  }
  sqlite3* raw() const { return h_; }
  bool read_only() const { return read_only_; }
 private:
  sqlite3* h_ = nullptr;
  bool read_only_ = false;
};

// ---------- domain types ---------------------------------------------------

enum class State { Registered, Indexed, Active, Stale, Deprecated, Archived };

inline std::string to_string(State s) {
  switch (s) {
    case State::Registered: return "REGISTERED";
    case State::Indexed: return "INDEXED";
    case State::Active: return "ACTIVE";
    case State::Stale: return "STALE";
    case State::Deprecated: return "DEPRECATED";
    case State::Archived: return "ARCHIVED";
  }
  return "REGISTERED";
}

inline State state_from_string(const std::string& s) {
  if (s == "INDEXED") return State::Indexed;
  if (s == "ACTIVE") return State::Active;
  if (s == "STALE") return State::Stale;
  if (s == "DEPRECATED") return State::Deprecated;
  if (s == "ARCHIVED") return State::Archived;
  return State::Registered;
}

inline std::string publication_state(State s) {
  if (s == State::Indexed || s == State::Active) return "AVAILABLE";
  return to_string(s);
}

enum class SearchMode { Hybrid, Exact, Fts, Fuzzy };
inline SearchMode search_mode_from_string(const std::string& s) {
  if (s == "exact") return SearchMode::Exact;
  if (s == "fts") return SearchMode::Fts;
  if (s == "fuzzy") return SearchMode::Fuzzy;
  return SearchMode::Hybrid;
}
inline std::string to_string(SearchMode m) {
  switch (m) {
    case SearchMode::Exact: return "exact";
    case SearchMode::Fts: return "fts";
    case SearchMode::Fuzzy: return "fuzzy";
    case SearchMode::Hybrid: return "hybrid";
  }
  return "hybrid";
}

enum class CatalogAccess { ReadWrite, ReadOnly };

struct SkillRow {
  long long id = 0;
  std::string skill_id, description, keywords, path, content_hash, version;
  State state = State::Registered;
  long long size_bytes = 0, search_count = 0, fetch_count = 0;
  std::string registered_at, updated_at, last_searched_at, last_fetched_at;
};

struct SearchHit {
  std::string skill_id, description, path;
  std::string skill_version, revision_id, catalog_generation;
  std::string ranking_policy = kRankingPolicy;
  std::string query_digest, normalized_tokens, search_mode, tie_break_key;
  double exact_keyword_score = 0.0;
  double exact_name_score = 0.0;
  double exact_description_score = 0.0;
  double fts_raw_score = 0.0;
  double fts_normalized_score = 0.0;
  double fts_min = 0.0;
  double fts_max = 0.0;
  double fuzzy_score = 0.0;
  double base_score = 0.0;
  double telemetry_multiplier = 1.0;
  double state_multiplier = 1.0;
  double score = 0.0;
  long long search_count = 0;
  long long fetch_count = 0;
  State state = State::Registered;
};

struct RegisterResult {
  bool ok = false;
  bool created = false;
  bool updated = false;
  std::string skill_id;
  std::string skill_version;
  std::string revision_id;
  std::string error;
};

struct FetchResult {
  std::string body;
  std::string skill_id;
  std::string skill_version;
  std::string revision_id;
  std::string catalog_generation;
  bool pinned = false;
};

struct GraveyardCandidate {
  std::string skill_id;
  long long search_count = 0;
  long long fetch_count = 0;
};

struct LogEvent {
  std::string ts, query, skill_id, event;
  long long rank = 0;
  double score = 0.0;
};

// ---------- engine ---------------------------------------------------------

class SkillLibrary {
 public:
  explicit SkillLibrary(const std::string& catalog_path,
                        const std::string& telemetry_path = "",
                        CatalogAccess access = CatalogAccess::ReadWrite)
      : catalog_path_(catalog_path),
        telemetry_path_(resolve_telemetry_path(catalog_path, telemetry_path, access)),
        access_(access),
        db_(catalog_path, access == CatalogAccess::ReadOnly),
        telemetry_db_(telemetry_path_, false) {
    if (same_database(catalog_path_, telemetry_path_))
      throw DbError("telemetry database must be separate from the catalog database");
    init_catalog_schema();
    init_telemetry_schema();
    init_fts();
  }

  bool catalog_read_only() const { return access_ == CatalogAccess::ReadOnly; }
  const std::string& catalog_path() const { return catalog_path_; }
  const std::string& telemetry_path() const { return telemetry_path_; }

  void init_catalog_schema() {
    if (catalog_read_only()) {
      Stmt verify(db_.raw(), "SELECT 1 FROM sqlite_master WHERE type='table' AND name='skills'");
      if (!verify.step()) throw DbError("read-only catalog does not contain a skills table");
      return;
    }
    db_.exec(
        "CREATE TABLE IF NOT EXISTS skills ("
        " id INTEGER PRIMARY KEY,"
        " skill_id TEXT NOT NULL UNIQUE,"
        " description TEXT NOT NULL,"
        " keywords TEXT NOT NULL DEFAULT '',"
        " path TEXT NOT NULL,"
        " content_hash TEXT NOT NULL DEFAULT '',"
        " size_bytes INTEGER NOT NULL DEFAULT 0,"
        " version TEXT NOT NULL DEFAULT '',"
        " state TEXT NOT NULL DEFAULT 'REGISTERED',"
        " search_count INTEGER NOT NULL DEFAULT 0,"
        " fetch_count INTEGER NOT NULL DEFAULT 0,"
        " registered_at TEXT NOT NULL DEFAULT (datetime('now')),"
        " updated_at TEXT NOT NULL DEFAULT (datetime('now')),"
        " last_searched_at TEXT,"
        " last_fetched_at TEXT"
        ") STRICT;");
  }

  void init_telemetry_schema() {
    telemetry_db_.exec(
        "CREATE TABLE IF NOT EXISTS skill_telemetry ("
        " skill_id TEXT PRIMARY KEY,"
        " search_count INTEGER NOT NULL DEFAULT 0,"
        " fetch_count INTEGER NOT NULL DEFAULT 0,"
        " last_searched_at TEXT,"
        " last_fetched_at TEXT"
        ") STRICT;");
    telemetry_db_.exec(
        "CREATE TABLE IF NOT EXISTS search_log ("
        " id INTEGER PRIMARY KEY,"
        " ts TEXT NOT NULL DEFAULT (datetime('now')),"
        " query TEXT NOT NULL,"
        " skill_id TEXT NOT NULL,"
        " rank INTEGER NOT NULL,"
        " score REAL NOT NULL,"
        " event TEXT NOT NULL"
        ") STRICT;");
    telemetry_db_.exec(
        "CREATE TABLE IF NOT EXISTS routing_decisions ("
        " id INTEGER PRIMARY KEY,"
        " ts TEXT NOT NULL DEFAULT (datetime('now')),"
        " query TEXT NOT NULL,"
        " query_digest TEXT NOT NULL,"
        " normalized_tokens TEXT NOT NULL,"
        " skill_id TEXT NOT NULL,"
        " skill_version TEXT NOT NULL,"
        " skill_revision TEXT NOT NULL,"
        " catalog_generation TEXT NOT NULL,"
        " ranking_policy TEXT NOT NULL,"
        " search_mode TEXT NOT NULL,"
        " rank INTEGER NOT NULL,"
        " exact_keyword REAL NOT NULL,"
        " exact_name REAL NOT NULL,"
        " exact_description REAL NOT NULL,"
        " fts_raw REAL NOT NULL,"
        " fts_normalized REAL NOT NULL,"
        " fts_min REAL NOT NULL,"
        " fts_max REAL NOT NULL,"
        " fuzzy REAL NOT NULL,"
        " base_score REAL NOT NULL,"
        " search_count INTEGER NOT NULL,"
        " fetch_count INTEGER NOT NULL,"
        " telemetry_multiplier REAL NOT NULL,"
        " state_multiplier REAL NOT NULL,"
        " final_score REAL NOT NULL,"
        " tie_break_key TEXT NOT NULL"
        ") STRICT;");
    telemetry_db_.exec(
        "CREATE TABLE IF NOT EXISTS fetch_receipts ("
        " id INTEGER PRIMARY KEY,"
        " ts TEXT NOT NULL DEFAULT (datetime('now')),"
        " query TEXT NOT NULL,"
        " skill_id TEXT NOT NULL,"
        " skill_version TEXT NOT NULL,"
        " expected_revision TEXT NOT NULL,"
        " actual_revision TEXT NOT NULL,"
        " expected_catalog_generation TEXT NOT NULL,"
        " actual_catalog_generation TEXT NOT NULL,"
        " status TEXT NOT NULL"
        ") STRICT;");
  }

  void init_fts() {
    if (catalog_read_only()) {
      try {
        Stmt s(db_.raw(), "SELECT 1 FROM sqlite_master WHERE type='table' AND name='skills_fts'");
        fts_enabled_ = s.step();
      } catch (...) {
        fts_enabled_ = false;
      }
      return;
    }
    try {
      db_.exec(
          "CREATE VIRTUAL TABLE IF NOT EXISTS skills_fts USING fts5("
          " skill_id, description, keywords,"
          " content='skills', content_rowid='id',"
          " tokenize='porter unicode61');");
      db_.exec(
          "CREATE TRIGGER IF NOT EXISTS skills_fts_ai AFTER INSERT ON skills BEGIN"
          " INSERT INTO skills_fts(rowid, skill_id, description, keywords)"
          " VALUES (new.id, new.skill_id, new.description, new.keywords);"
          " END;");
      db_.exec(
          "CREATE TRIGGER IF NOT EXISTS skills_fts_ad AFTER DELETE ON skills BEGIN"
          " INSERT INTO skills_fts(skills_fts, rowid, skill_id, description, keywords)"
          " VALUES('delete', old.id, old.skill_id, old.description, old.keywords);"
          " END;");
      db_.exec(
          "CREATE TRIGGER IF NOT EXISTS skills_fts_au"
          " AFTER UPDATE OF skill_id, description, keywords ON skills BEGIN"
          " INSERT INTO skills_fts(skills_fts, rowid, skill_id, description, keywords)"
          " VALUES('delete', old.id, old.skill_id, old.description, old.keywords);"
          " INSERT INTO skills_fts(rowid, skill_id, description, keywords)"
          " VALUES (new.id, new.skill_id, new.description, new.keywords);"
          " END;");
      fts_enabled_ = true;
      Stmt sc(db_.raw(), "SELECT (SELECT COUNT(*) FROM skills),(SELECT COUNT(*) FROM skills_fts)");
      if (sc.step() && sc.col_i(0) != sc.col_i(1))
        db_.exec("INSERT INTO skills_fts(skills_fts) VALUES('rebuild');");
    } catch (const DbError&) {
      fts_enabled_ = false;
    }
  }

  bool fts_enabled() const { return fts_enabled_; }

  RegisterResult register_skill(const std::string& skill_md_path,
                                const std::string& explicit_keywords = "") {
    require_catalog_write("register_skill");
    RegisterResult r;
    std::ifstream f(skill_md_path, std::ios::binary);
    if (!f) { r.error = "cannot open " + skill_md_path; return r; }
    std::ostringstream ss; ss << f.rdbuf();
    const std::string text = ss.str();
    if (text.size() > kMaxSkillBytes) { r.error = "file too large (>8MiB)"; return r; }

    Frontmatter fm = parse_frontmatter(text);
    if (!fm.valid) { r.error = "invalid or missing frontmatter (need name: and description:)"; return r; }
    if (looks_like_xml_tag(fm.description)) {
      r.error = "description contains XML/HTML-tag-shaped text; use word-based placeholders";
      return r;
    }
    if (fm.description.size() > kMaxDescBytes) fm.description.resize(kMaxDescBytes);

    const std::string rev = revision_id(text);
    const std::string keywords = explicit_keywords.empty()
        ? derive_keywords(fm.name + " " + fm.description) : lower(explicit_keywords);

    Stmt sel(db_.raw(),
        "SELECT description,keywords,path,content_hash,version,state FROM skills WHERE skill_id=?");
    sel.bind(1, fm.name);
    const bool exists = sel.step();
    const std::string old_desc = exists ? sel.col_s(0) : "";
    const std::string old_kw = exists ? sel.col_s(1) : "";
    const std::string old_path = exists ? sel.col_s(2) : "";
    const std::string old_rev = exists ? sel.col_s(3) : "";
    const std::string old_version = exists ? sel.col_s(4) : "";
    const State old_state = exists ? state_from_string(sel.col_s(5)) : State::Registered;

    if (!exists) {
      Stmt ins(db_.raw(),
          "INSERT INTO skills (skill_id,description,keywords,path,content_hash,size_bytes,version,state)"
          " VALUES (?,?,?,?,?,?,?,?)");
      ins.bind(1, fm.name).bind(2, fm.description).bind(3, keywords).bind(4, skill_md_path)
         .bind(5, rev).bind(6, static_cast<long long>(text.size())).bind(7, fm.version)
         .bind(8, to_string(State::Indexed));
      ins.step();
      r.created = true;
    } else if (old_desc != fm.description || old_kw != keywords || old_path != skill_md_path ||
               old_rev != rev || old_version != fm.version) {
      State next = State::Indexed;
      if (old_state == State::Active) next = State::Active;
      else if (old_state == State::Deprecated) next = State::Deprecated;
      else if (old_state == State::Archived) next = State::Archived;
      Stmt upd(db_.raw(),
          "UPDATE skills SET description=?,keywords=?,path=?,content_hash=?,size_bytes=?,version=?,"
          "state=?,updated_at=datetime('now') WHERE skill_id=?");
      upd.bind(1, fm.description).bind(2, keywords).bind(3, skill_md_path).bind(4, rev)
         .bind(5, static_cast<long long>(text.size())).bind(6, fm.version)
         .bind(7, to_string(next)).bind(8, fm.name);
      upd.step();
      r.updated = true;
    }

    ensure_telemetry_row(fm.name, 0, 0);
    r.ok = true;
    r.skill_id = fm.name;
    r.skill_version = fm.version;
    r.revision_id = rev;
    return r;
  }

  static std::string derive_keywords(const std::string& text) {
    const auto toks = tokenize(text);
    std::string out;
    for (std::size_t i = 0; i < toks.size() && i < 20; ++i) {
      if (i) out += ',';
      out += toks[i];
    }
    return out;
  }

  bool mark_stale_if_drifted(const std::string& skill_id) {
    Stmt sel(db_.raw(), "SELECT path,content_hash,state FROM skills WHERE skill_id=?");
    sel.bind(1, skill_id);
    if (!sel.step()) return false;
    const std::string path = sel.col_s(0), stored = sel.col_s(1);
    const State st = state_from_string(sel.col_s(2));
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    const std::string observed = revision_id(ss.str());
    if (observed != stored && st != State::Stale) {
      if (!catalog_read_only()) set_state(skill_id, State::Stale);
      log_event("", skill_id, 0, 0.0, "STALE_DETECTED");
      return true;
    }
    return false;
  }

  void set_state(const std::string& skill_id, State s) {
    require_catalog_write("set_state");
    Stmt upd(db_.raw(), "UPDATE skills SET state=?,updated_at=datetime('now') WHERE skill_id=?");
    upd.bind(1, to_string(s)).bind(2, skill_id);
    upd.step();
  }

  std::string catalog_generation() {
    Stmt sel(db_.raw(),
        "SELECT skill_id,description,keywords,content_hash,version,state FROM skills ORDER BY skill_id");
    std::string canonical;
    while (sel.step()) {
      canonical += length_field(sel.col_s(0));
      canonical += length_field(sel.col_s(1));
      canonical += length_field(sel.col_s(2));
      canonical += length_field(sel.col_s(3));
      canonical += length_field(sel.col_s(4));
      canonical += length_field(publication_state(state_from_string(sel.col_s(5))));
    }
    return "sha256:" + sha256_hex(canonical);
  }

  static constexpr double kFtsWeight = 0.6;
  static constexpr double kFuzzyWeight = 0.35;

  std::vector<SearchHit> search(const std::string& query, int top_n = 8,
                                bool include_archived = false,
                                SearchMode mode = SearchMode::Hybrid) {
    if (query.size() > kMaxQueryBytes) throw DbError("query exceeds max size");
    if (top_n < 1) top_n = 1;
    const auto tokens = tokenize(query);
    const std::string normalized = join_tokens(tokens);
    const std::string qdigest = "sha256:" + sha256_hex(normalized);
    const std::string generation = catalog_generation();
    const bool use_exact = mode == SearchMode::Hybrid || mode == SearchMode::Exact;
    const bool use_fts = mode == SearchMode::Hybrid || mode == SearchMode::Fts;
    const bool use_fuzzy = mode == SearchMode::Hybrid || mode == SearchMode::Fuzzy;

    const std::map<std::string, double> fts_raw = use_fts ? fts_scores(query)
                                                          : std::map<std::string, double>{};
    double fmin = 0.0, fmax = 0.0;
    bool first_fts = true;
    for (const auto& [id, v] : fts_raw) {
      (void)id;
      if (first_fts) { fmin = fmax = v; first_fts = false; }
      else { fmin = std::min(fmin, v); fmax = std::max(fmax, v); }
    }
    auto normalized_fts = [&](const std::string& id) {
      const auto it = fts_raw.find(id);
      if (it == fts_raw.end()) return 0.0;
      const double n = fmax > fmin ? (it->second - fmin) / (fmax - fmin) : 1.0;
      return 0.5 + 0.5 * n;
    };

    std::vector<SearchHit> hits;
    Stmt sel(db_.raw(),
        "SELECT skill_id,description,keywords,path,state,version,content_hash,search_count,fetch_count"
        " FROM skills");
    while (sel.step()) {
      const State st = state_from_string(sel.col_s(4));
      if (st == State::Archived && !include_archived) continue;
      const std::string skill_id = sel.col_s(0);
      const std::string desc = sel.col_s(1);
      const std::string kw = lower(sel.col_s(2));
      const auto [search_count, fetch_count] = telemetry_counts(skill_id, sel.col_i(7), sel.col_i(8));

      const auto kw_set = split_csv(kw);
      const auto name_toks = tokenize(skill_id);
      const auto desc_toks = tokenize(desc);
      const std::set<std::string> name_set(name_toks.begin(), name_toks.end());
      const std::set<std::string> desc_set(desc_toks.begin(), desc_toks.end());

      SearchHit h;
      h.skill_id = skill_id;
      h.description = desc;
      h.path = sel.col_s(3);
      h.state = st;
      h.skill_version = sel.col_s(5).empty() ? "1.0.0" : sel.col_s(5);
      h.revision_id = sel.col_s(6);
      h.catalog_generation = generation;
      h.query_digest = qdigest;
      h.normalized_tokens = normalized;
      h.search_mode = to_string(mode);
      h.tie_break_key = skill_id;
      h.search_count = search_count;
      h.fetch_count = fetch_count;
      h.fts_min = fmin;
      h.fts_max = fmax;

      if (use_exact) {
        for (const auto& t : tokens) {
          if (kw_set.count(t)) h.exact_keyword_score += 3.0;
          else if (name_set.count(t)) h.exact_name_score += 2.0;
          else if (desc_set.count(t)) h.exact_description_score += 1.0;
        }
      }

      if (use_fuzzy && !tokens.empty()) {
        std::set<std::string> pool = kw_set;
        pool.insert(name_set.begin(), name_set.end());
        pool.insert(desc_set.begin(), desc_set.end());
        double acc = 0.0;
        for (const auto& t : tokens) {
          if (kw_set.count(t) || name_set.count(t) || desc_set.count(t)) continue;
          const int maxd = t.size() <= 4 ? 1 : 2;
          int best = maxd + 1;
          for (const auto& pt : pool) {
            const int d = bounded_edit_distance(t, pt, maxd);
            if (d < best) best = d;
            if (best <= 1) break;
          }
          if (best <= maxd) acc += 1.0 - static_cast<double>(best) / static_cast<double>(t.size());
        }
        h.fuzzy_score = acc / static_cast<double>(tokens.size());
      }

      const auto fit = fts_raw.find(skill_id);
      h.fts_raw_score = fit == fts_raw.end() ? 0.0 : fit->second;
      h.fts_normalized_score = normalized_fts(skill_id);
      const double exact = h.exact_keyword_score + h.exact_name_score + h.exact_description_score;
      h.base_score = exact + kFtsWeight * h.fts_normalized_score + kFuzzyWeight * h.fuzzy_score;
      if (h.base_score <= 0.0) continue;

      const double conversion = search_count > 0
          ? static_cast<double>(fetch_count) / static_cast<double>(search_count) : 0.0;
      h.telemetry_multiplier = 1.0 + std::min(
          0.5, conversion * 0.5 + std::log1p(static_cast<double>(fetch_count)) * 0.05);
      h.state_multiplier = st == State::Deprecated ? 0.3 : 1.0;
      h.score = h.base_score * h.telemetry_multiplier * h.state_multiplier;
      hits.push_back(h);
    }

    std::sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
      if (a.score != b.score) return a.score > b.score;
      return a.skill_id < b.skill_id;
    });
    if (static_cast<int>(hits.size()) > top_n) hits.resize(static_cast<std::size_t>(top_n));

    for (std::size_t i = 0; i < hits.size(); ++i) {
      log_routing_decision(query, static_cast<int>(i), hits[i]);
      log_event(query, hits[i].skill_id, static_cast<int>(i), hits[i].score, "SUGGESTED");
      bump_search_count(hits[i].skill_id);
    }
    return hits;
  }

  std::map<std::string, double> fts_scores(const std::string& query) {
    std::map<std::string, double> out;
    if (!fts_enabled_) return out;
    const auto toks = tokenize(query);
    if (toks.empty()) return out;
    std::string match;
    for (std::size_t i = 0; i < toks.size(); ++i) {
      if (i) match += " OR ";
      match += "\"" + toks[i] + "\"*";
    }
    try {
      Stmt sel(db_.raw(),
          "SELECT skill_id,bm25(skills_fts,2.0,1.0,3.0) FROM skills_fts WHERE skills_fts MATCH ?");
      sel.bind(1, match);
      while (sel.step()) out[sel.col_s(0)] = -sel.col_d(1);
    } catch (const DbError&) {
      out.clear();
    }
    return out;
  }

  FetchResult fetch(const std::string& skill_id, const std::string& query_context = "",
                    const std::string& expected_revision = "",
                    const std::string& expected_catalog_generation = "") {
    Stmt sel(db_.raw(),
        "SELECT path,state,version,content_hash FROM skills WHERE skill_id=?");
    sel.bind(1, skill_id);
    if (!sel.step()) throw DbError("unknown skill_id: " + skill_id);
    const std::string path = sel.col_s(0);
    const State st = state_from_string(sel.col_s(1));
    const std::string version = sel.col_s(2).empty() ? "1.0.0" : sel.col_s(2);
    const std::string indexed_revision = sel.col_s(3);
    const std::string generation = catalog_generation();

    if (!expected_catalog_generation.empty() && expected_catalog_generation != generation) {
      log_fetch_receipt(query_context, skill_id, version, expected_revision, indexed_revision,
                        expected_catalog_generation, generation, "CATALOG_GENERATION_MISMATCH");
      throw DbError("CATALOG_GENERATION_MISMATCH expected=" + expected_catalog_generation +
                    " actual=" + generation);
    }

    std::ifstream f(path, std::ios::binary);
    if (!f) throw DbError("skill file missing on disk: " + path);
    std::ostringstream ss; ss << f.rdbuf();
    const std::string body = ss.str();
    const std::string observed_revision = revision_id(body);

    if (observed_revision != indexed_revision) {
      if (!catalog_read_only()) set_state(skill_id, State::Stale);
      log_fetch_receipt(query_context, skill_id, version, expected_revision, observed_revision,
                        expected_catalog_generation, generation, "INDEX_DRIFT");
      log_event(query_context, skill_id, 0, 0.0, "STALE_DETECTED");
      throw DbError("REVISION_MISMATCH indexed=" + indexed_revision + " observed=" + observed_revision);
    }
    if (!expected_revision.empty() && expected_revision != observed_revision) {
      log_fetch_receipt(query_context, skill_id, version, expected_revision, observed_revision,
                        expected_catalog_generation, generation, "EXPECTED_REVISION_MISMATCH");
      throw DbError("REVISION_MISMATCH expected=" + expected_revision + " actual=" + observed_revision);
    }

    bump_fetch_count(skill_id);
    if (!catalog_read_only() && (st == State::Registered || st == State::Indexed))
      set_state(skill_id, State::Active);
    log_event(query_context, skill_id, 0, 0.0, "FETCHED");
    log_fetch_receipt(query_context, skill_id, version, expected_revision, observed_revision,
                      expected_catalog_generation, generation, "OK");

    return {body, skill_id, version, observed_revision, generation,
            !expected_revision.empty() && !expected_catalog_generation.empty()};
  }

  std::string fetch_body(const std::string& skill_id, const std::string& query_context = "",
                         const std::string& expected_revision = "",
                         const std::string& expected_catalog_generation = "") {
    return fetch(skill_id, query_context, expected_revision, expected_catalog_generation).body;
  }

  void log_used(const std::string& skill_id, const std::string& query_context = "") {
    log_event(query_context, skill_id, 0, 0.0, "USED");
  }

  std::vector<LogEvent> recent_events(int limit = 10) {
    std::vector<LogEvent> out;
    Stmt sel(telemetry_db_.raw(),
        "SELECT ts,query,skill_id,rank,score,event FROM search_log ORDER BY id DESC LIMIT ?");
    sel.bind(1, static_cast<long long>(limit));
    while (sel.step())
      out.push_back({sel.col_s(0),sel.col_s(1),sel.col_s(2),sel.col_s(5),sel.col_i(3),sel.col_d(4)});
    std::reverse(out.begin(), out.end());
    return out;
  }

  long long total_events() {
    Stmt sel(telemetry_db_.raw(), "SELECT COUNT(*) FROM search_log");
    sel.step();
    return sel.col_i(0);
  }

  long long count_events(const std::string& skill_id, const std::string& event) {
    Stmt sel(telemetry_db_.raw(),
        "SELECT COUNT(*) FROM search_log WHERE skill_id=? AND event=?");
    sel.bind(1, skill_id).bind(2, event);
    sel.step();
    return sel.col_i(0);
  }

  long long count_routing_decisions(const std::string& skill_id = "") {
    if (skill_id.empty()) {
      Stmt sel(telemetry_db_.raw(), "SELECT COUNT(*) FROM routing_decisions");
      sel.step(); return sel.col_i(0);
    }
    Stmt sel(telemetry_db_.raw(), "SELECT COUNT(*) FROM routing_decisions WHERE skill_id=?");
    sel.bind(1, skill_id); sel.step(); return sel.col_i(0);
  }

  long long count_fetch_receipts(const std::string& status = "") {
    if (status.empty()) {
      Stmt sel(telemetry_db_.raw(), "SELECT COUNT(*) FROM fetch_receipts");
      sel.step(); return sel.col_i(0);
    }
    Stmt sel(telemetry_db_.raw(), "SELECT COUNT(*) FROM fetch_receipts WHERE status=?");
    sel.bind(1, status); sel.step(); return sel.col_i(0);
  }

  bool get_row(const std::string& skill_id, SkillRow& out) {
    Stmt sel(db_.raw(),
        "SELECT id,skill_id,description,keywords,path,content_hash,size_bytes,version,state,"
        "search_count,fetch_count,registered_at,updated_at,last_searched_at,last_fetched_at"
        " FROM skills WHERE skill_id=?");
    sel.bind(1, skill_id);
    if (!sel.step()) return false;
    const auto [sc, fc] = telemetry_counts(skill_id, sel.col_i(9), sel.col_i(10));
    out = {sel.col_i(0),sel.col_s(1),sel.col_s(2),sel.col_s(3),sel.col_s(4),sel.col_s(5),
           sel.col_s(7),state_from_string(sel.col_s(8)),sel.col_i(6),sc,fc,
           sel.col_s(11),sel.col_s(12),sel.col_s(13),sel.col_s(14)};
    return true;
  }

  std::vector<SkillRow> list_all(int limit = 100000) {
    std::vector<SkillRow> out;
    Stmt sel(db_.raw(),
        "SELECT id,skill_id,description,keywords,path,content_hash,size_bytes,version,state,"
        "search_count,fetch_count,registered_at,updated_at,last_searched_at,last_fetched_at"
        " FROM skills ORDER BY skill_id LIMIT ?");
    sel.bind(1, static_cast<long long>(limit));
    while (sel.step()) {
      const std::string id = sel.col_s(1);
      const auto [sc, fc] = telemetry_counts(id, sel.col_i(9), sel.col_i(10));
      out.push_back({sel.col_i(0),id,sel.col_s(2),sel.col_s(3),sel.col_s(4),sel.col_s(5),
                     sel.col_s(7),state_from_string(sel.col_s(8)),sel.col_i(6),sc,fc,
                     sel.col_s(11),sel.col_s(12),sel.col_s(13),sel.col_s(14)});
    }
    return out;
  }

  std::map<std::string, long long> state_counts() {
    std::map<std::string, long long> out;
    Stmt sel(db_.raw(), "SELECT state,COUNT(*) FROM skills GROUP BY state");
    while (sel.step()) out[sel.col_s(0)] = sel.col_i(1);
    return out;
  }

  std::vector<GraveyardCandidate> graveyard_candidates(long long min_searches = 5,
                                                        double max_conversion = 0.05) {
    std::vector<GraveyardCandidate> out;
    for (const auto& row : list_all()) {
      if (row.state == State::Archived || row.search_count < min_searches) continue;
      const double conversion = row.search_count > 0
          ? static_cast<double>(row.fetch_count) / row.search_count : 0.0;
      if (conversion <= max_conversion)
        out.push_back({row.skill_id,row.search_count,row.fetch_count});
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
      if (a.search_count != b.search_count) return a.search_count > b.search_count;
      return a.skill_id < b.skill_id;
    });
    return out;
  }

  struct Telemetry {
    long long total_searches = 0, total_fetches = 0;
    double overall_conversion = 0.0;
  };

  Telemetry telemetry() {
    Telemetry t;
    Stmt sel(telemetry_db_.raw(),
        "SELECT COALESCE(SUM(search_count),0),COALESCE(SUM(fetch_count),0) FROM skill_telemetry");
    sel.step();
    t.total_searches = sel.col_i(0);
    t.total_fetches = sel.col_i(1);
    t.overall_conversion = t.total_searches > 0
        ? static_cast<double>(t.total_fetches) / t.total_searches : 0.0;
    return t;
  }

 private:
  static bool same_database(const std::string& a, const std::string& b) {
    return a != ":memory:" && a == b;
  }

  static std::string resolve_telemetry_path(const std::string& catalog,
                                            const std::string& requested,
                                            CatalogAccess access) {
    (void)access;
    if (!requested.empty()) return requested;
    if (catalog == ":memory:") return ":memory:";
    return catalog + ".telemetry.db";
  }

  void require_catalog_write(const char* operation) const {
    if (catalog_read_only())
      throw DbError(std::string(operation) + " denied: catalog opened read-only in consumer role");
  }

  static std::set<std::string> split_csv(const std::string& s) {
    std::set<std::string> out;
    std::string cur;
    for (char c : s) {
      if (c == ',') { if (!cur.empty()) out.insert(cur); cur.clear(); }
      else if (!std::isspace(static_cast<unsigned char>(c))) cur += c;
    }
    if (!cur.empty()) out.insert(cur);
    return out;
  }

  void ensure_telemetry_row(const std::string& skill_id,
                            long long legacy_search_count,
                            long long legacy_fetch_count) {
    Stmt ins(telemetry_db_.raw(),
        "INSERT OR IGNORE INTO skill_telemetry(skill_id,search_count,fetch_count) VALUES(?,?,?)");
    ins.bind(1, skill_id).bind(2, legacy_search_count).bind(3, legacy_fetch_count);
    ins.step();
  }

  std::pair<long long,long long> telemetry_counts(const std::string& skill_id,
                                                   long long legacy_search_count,
                                                   long long legacy_fetch_count) {
    ensure_telemetry_row(skill_id, legacy_search_count, legacy_fetch_count);
    Stmt sel(telemetry_db_.raw(),
        "SELECT search_count,fetch_count FROM skill_telemetry WHERE skill_id=?");
    sel.bind(1, skill_id);
    if (!sel.step()) return {legacy_search_count, legacy_fetch_count};
    return {sel.col_i(0), sel.col_i(1)};
  }

  void bump_search_count(const std::string& skill_id) {
    ensure_telemetry_row(skill_id, 0, 0);
    Stmt upd(telemetry_db_.raw(),
        "UPDATE skill_telemetry SET search_count=search_count+1,last_searched_at=datetime('now')"
        " WHERE skill_id=?");
    upd.bind(1, skill_id); upd.step();
  }

  void bump_fetch_count(const std::string& skill_id) {
    ensure_telemetry_row(skill_id, 0, 0);
    Stmt upd(telemetry_db_.raw(),
        "UPDATE skill_telemetry SET fetch_count=fetch_count+1,last_fetched_at=datetime('now')"
        " WHERE skill_id=?");
    upd.bind(1, skill_id); upd.step();
  }

  void log_event(const std::string& query, const std::string& skill_id,
                 int rank, double score, const std::string& event) {
    Stmt ins(telemetry_db_.raw(),
        "INSERT INTO search_log(query,skill_id,rank,score,event) VALUES(?,?,?,?,?)");
    ins.bind(1, query).bind(2, skill_id).bind(3, static_cast<long long>(rank))
       .bind(4, score).bind(5, event);
    ins.step();
  }

  void log_routing_decision(const std::string& query, int rank, const SearchHit& h) {
    Stmt ins(telemetry_db_.raw(),
        "INSERT INTO routing_decisions("
        "query,query_digest,normalized_tokens,skill_id,skill_version,skill_revision,"
        "catalog_generation,ranking_policy,search_mode,rank,exact_keyword,exact_name,"
        "exact_description,fts_raw,fts_normalized,fts_min,fts_max,fuzzy,base_score,"
        "search_count,fetch_count,telemetry_multiplier,state_multiplier,final_score,tie_break_key)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    ins.bind(1, query).bind(2, h.query_digest).bind(3, h.normalized_tokens)
       .bind(4, h.skill_id).bind(5, h.skill_version).bind(6, h.revision_id)
       .bind(7, h.catalog_generation).bind(8, h.ranking_policy).bind(9, h.search_mode)
       .bind(10, static_cast<long long>(rank)).bind(11, h.exact_keyword_score)
       .bind(12, h.exact_name_score).bind(13, h.exact_description_score)
       .bind(14, h.fts_raw_score).bind(15, h.fts_normalized_score)
       .bind(16, h.fts_min).bind(17, h.fts_max).bind(18, h.fuzzy_score)
       .bind(19, h.base_score).bind(20, h.search_count).bind(21, h.fetch_count)
       .bind(22, h.telemetry_multiplier).bind(23, h.state_multiplier)
       .bind(24, h.score).bind(25, h.tie_break_key);
    ins.step();
  }

  void log_fetch_receipt(const std::string& query, const std::string& skill_id,
                         const std::string& version, const std::string& expected_revision,
                         const std::string& actual_revision,
                         const std::string& expected_catalog_generation,
                         const std::string& actual_catalog_generation,
                         const std::string& status) {
    Stmt ins(telemetry_db_.raw(),
        "INSERT INTO fetch_receipts(query,skill_id,skill_version,expected_revision,actual_revision,"
        "expected_catalog_generation,actual_catalog_generation,status) VALUES(?,?,?,?,?,?,?,?)");
    ins.bind(1, query).bind(2, skill_id).bind(3, version).bind(4, expected_revision)
       .bind(5, actual_revision).bind(6, expected_catalog_generation)
       .bind(7, actual_catalog_generation).bind(8, status);
    ins.step();
  }

  std::string catalog_path_;
  std::string telemetry_path_;
  CatalogAccess access_;
  Db db_;
  Db telemetry_db_;
  bool fts_enabled_ = false;
};

}  // namespace skilllib
