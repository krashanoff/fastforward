#ifndef DB_H_
#define DB_H_

#include <stdbool.h>

#include "sqlite3.h"

struct json_object;

int initialize_db(sqlite3 **db);
int close_db(sqlite3 **db);

// Initialize the database but keep everything in memory. Useful for tests.
int initialize_db_mem(sqlite3 **db);

// Database stuff
int migrate(sqlite3 *db);
int db_begin(sqlite3 *db);
int db_commit(sqlite3 *db);

// Record metrics about the request's context in the database.
bool record_context_metrics(sqlite3 *db, struct json_object *context);

#endif // DB_H_
