#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>

#include "common.h"
#include "json-c/json.h"
#include "evaluation.h"

static void profile_db(void *context, const char *sql, sqlite3_uint64 ns);
int initialize_db_base(sqlite3 **db, int inmemory);

static void profile_db(void *context, const char *sql, sqlite3_uint64 ns)
{
  fprintf(stderr, "Query: %s\n", sql);
  fprintf(stderr, "Execution Time: %llu ms\n", ns / 1000000);
}

int initialize_db_base(sqlite3 **db, int inmemory)
{
  int config_result = sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);
  if (config_result == SQLITE_MISUSE)
  {
    fprintf(stderr, "misuse of sqlite3 interface detected\n");
    return 1;
  }
  if (config_result != SQLITE_OK)
  {
    fprintf(stderr, "failed to configure sqlite for single-threaded use\n");
    return 1;
  }

  if (sqlite3_initialize() != SQLITE_OK)
  {
    fprintf(stderr, "failed to initialize sqlite3 library\n");
    return 1;
  }

  if (sqlite3_open_v2(
          inmemory ? "test_db" : getenv("FF_DB_PATH"),
          db,
          SQLITE_OPEN_READWRITE | (inmemory ? SQLITE_OPEN_MEMORY : SQLITE_OPEN_CREATE),
          NULL) != SQLITE_OK)
  {
    fprintf(stderr, "failed to open sqlite database: %s\n", sqlite3_errmsg(*db));
    return 1;
  }
  sqlite3_profile(*db, &profile_db, NULL);

  // TODO: verify and correct schema
  if (migrate(*db) != 0)
  {
    fprintf(stderr, "failed to initialize schema: %s\n", sqlite3_errmsg(*db));
    return 1;
  }
  return 0;
}

int initialize_db_mem(sqlite3 **db)
{
  return initialize_db_base(db, 1);
}

int initialize_db(sqlite3 **db)
{
  return initialize_db_base(db, 0);
}

int close_db(sqlite3 **db)
{
  if (sqlite3_close(*db) != SQLITE_OK)
    return 1;
  sqlite3_shutdown();
  return 0;
}

int db_begin(sqlite3 *db)
{
  return sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
}

int db_commit(sqlite3 *db)
{
  return sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
}

#define MUST_EXEC(expr) \
  assert(sqlite3_exec(db, expr, NULL, NULL, NULL) == SQLITE_OK);

int migrate(sqlite3 *db)
{
  assert(db_begin(db) == SQLITE_OK);

  MUST_EXEC(
      "CREATE TABLE IF NOT EXISTS "
      "feature_flags ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "name VARCHAR(256) NOT NULL,"
      "key VARCHAR(256) NOT NULL UNIQUE"
      ")");

  // Represents all observed data keys over the lifetime of the application.
  MUST_EXEC(
      "CREATE TABLE IF NOT EXISTS "
      "request_meta_key ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "key_name VARCHAR(128) UNIQUE NOT NULL"
      ")");

  // Binds observed meta to feature flags.
  MUST_EXEC("CREATE TABLE IF NOT EXISTS "
            "flags_meta ("
            "flag_id INTEGER REFERENCES feature_flags (id),"
            "meta_key_id INTEGER REFERENCES request_meta_key (id)"
            ")");

  // Observed values for a given key over the lifetime of the application.
  MUST_EXEC(
      "CREATE TABLE IF NOT EXISTS "
      "request_meta_values ("
      "meta_key_id INTEGER REFERENCES request_meta_key(id),"
      "key_name VARCHAR(128) NOT NULL,"
      "n_observed INT NOT NULL DEFAULT 1"
      ")");

  MUST_EXEC(
      "CREATE TABLE IF NOT EXISTS "
      "feature_flag_default_state ("
      "feature_flag_id INTEGER REFERENCES feature_flags (id),"
      "enabled BOOLEAN NOT NULL DEFAULT 'false'"
      ")");

  assert(db_commit(db) == SQLITE_OK);
  return 0;
}

bool record_context_metrics(sqlite3 *db, struct json_object *context)
{
  if (!is_valid_context(context))
    return false;

  if (db_begin(db) != SQLITE_OK)
  {
    return false;
  }

  sqlite3_stmt *insertion = NULL;
  if (sqlite3_prepare_v2(db,
                         STRLIT(
                             "INSERT INTO request_meta_key (key_name) "
                             "VALUES (@keyName) "
                             "ON CONFLICT (key_name) DO NOTHING"),
                         &insertion,
                         NULL) != SQLITE_OK)
  {
    fprintf(stderr, "failed to initialize statement: %s\n", sqlite3_errmsg(db));
    return false;
  }

  json_object_object_foreach(context, entry_key, entry_val)
  {
    const char *serialized = json_object_to_json_string(entry_val);
    if (sqlite3_bind_text(insertion, sqlite3_bind_parameter_index(insertion, "@keyName"), STRLIT(serialized), NULL) != SQLITE_OK)
    {
      sqlite3_finalize(insertion);
      return false;
    }

    int stmt_result = 0;
    do
    {
      stmt_result = sqlite3_step(insertion);
    } while (stmt_result != SQLITE_DONE);
  }

  sqlite3_finalize(insertion);
  return db_commit(db) == SQLITE_OK;
}