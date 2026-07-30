#ifndef TORAFIRMA_SKILLLIB_C_H
#define TORAFIRMA_SKILLLIB_C_H

#include <stddef.h>

#if defined(_WIN32)
  #if defined(SKILLLIB_BUILD_SHARED)
    #define SKILLLIB_API __declspec(dllexport)
  #elif defined(SKILLLIB_USE_SHARED)
    #define SKILLLIB_API __declspec(dllimport)
  #else
    #define SKILLLIB_API
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #define SKILLLIB_API __attribute__((visibility("default")))
#else
  #define SKILLLIB_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct skilllib_t skilllib_t;

typedef enum skilllib_status_t {
  SKILLLIB_OK = 0,
  SKILLLIB_INVALID_ARGUMENT = 1,
  SKILLLIB_NOT_FOUND = 2,
  SKILLLIB_REVISION_MISMATCH = 3,
  SKILLLIB_CATALOG_GENERATION_MISMATCH = 4,
  SKILLLIB_READ_ONLY = 5,
  SKILLLIB_IO_ERROR = 6,
  SKILLLIB_DATABASE_ERROR = 7,
  SKILLLIB_INTERNAL_ERROR = 255
} skilllib_status_t;

typedef struct skilllib_buffer_t {
  char* data;
  size_t len;
} skilllib_buffer_t;

/* Engine metadata. Returned strings have static lifetime. */
SKILLLIB_API const char* skilllib_version(void);
SKILLLIB_API const char* skilllib_ranking_policy(void);

/*
 * Opens a catalog and a separate telemetry database.
 * telemetry_path may be NULL or empty to use the engine default.
 * read_only != 0 opens the catalog in consumer mode.
 */
SKILLLIB_API skilllib_status_t skilllib_open(
    const char* catalog_path,
    const char* telemetry_path,
    int read_only,
    skilllib_t** out_lib);

SKILLLIB_API void skilllib_close(skilllib_t* lib);

/*
 * Returns the most recent handle-local error. The pointer remains valid until
 * the next call using the same handle or until skilllib_close().
 */
SKILLLIB_API const char* skilllib_last_error(const skilllib_t* lib);

/* Frees any buffer returned by this ABI and resets it to {NULL, 0}. */
SKILLLIB_API void skilllib_buffer_free(skilllib_buffer_t* buffer);

/* JSON result: registration identity and change flags. Operator mode only. */
SKILLLIB_API skilllib_status_t skilllib_register(
    skilllib_t* lib,
    const char* skill_md_path,
    const char* keywords,
    skilllib_buffer_t* out_json);

/* JSON result: ordered array with complete score decomposition. */
SKILLLIB_API skilllib_status_t skilllib_search(
    skilllib_t* lib,
    const char* query,
    int top_n,
    const char* mode,
    int include_archived,
    skilllib_buffer_t* out_json);

/*
 * JSON result: verified body and identity tuple.
 * expected_revision and expected_catalog_generation are mandatory and must
 * be copied from the selected search result. Empty values fail closed.
 */
SKILLLIB_API skilllib_status_t skilllib_fetch(
    skilllib_t* lib,
    const char* skill_id,
    const char* query_context,
    const char* expected_revision,
    const char* expected_catalog_generation,
    skilllib_buffer_t* out_json);

/* UTF-8 string result containing the current sha256: catalog generation. */
SKILLLIB_API skilllib_status_t skilllib_catalog_generation(
    skilllib_t* lib,
    skilllib_buffer_t* out_generation);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TORAFIRMA_SKILLLIB_C_H */
