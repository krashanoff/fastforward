#include <stdio.h>
#include <string.h>

#include "unity/unity.h"
#include "json-c/json.h"

#include "db.h"

static sqlite3 *global_db = NULL;

// Intermediate query results test harness
#define MAX_ROWS 256

struct row
{
  int ncols;
  char *colstrings[MAX_ROWS];
};

static struct row results[MAX_ROWS];
static int nrows = 0;

static int add_row(void *_ignored, int ncols, char **colstrings, char **colnames)
{
  struct row *current_row = &results[nrows];
  current_row->ncols = ncols;
  for (int k = 0; k < ncols; k++)
  {
    int current_col_len = strlen(colstrings[k]);
    current_row->colstrings[k] = (char *)malloc(current_col_len * sizeof(char));
    memcpy(current_row->colstrings[k], colstrings[k], current_col_len);
  }
  nrows++;
  return 0;
}

void reset_rows()
{
  for (int k = 0; k < nrows; k++)
    for (int c = 0; c < results[k].ncols; c++)
      if (results[k].colstrings[c] != NULL)
        free(results[k].colstrings[c]);
  nrows = 0;
}

int dbexec(const char *sql)
{
  if (nrows != 0)
  {
    reset_rows();
  }
  return sqlite3_exec(global_db, sql, add_row, NULL, NULL);
}

void setUp(void)
{
  reset_rows();
  TEST_ASSERT_FALSE(initialize_db_mem(&global_db));
}

void tearDown(void)
{
  reset_rows();
  TEST_ASSERT_EQUAL(0, close_db(&global_db));
}

void test_smoke(void)
{
  TEST_ASSERT_EQUAL(SQLITE_OK, sqlite3_exec(global_db, "SELECT 1", NULL, NULL, NULL));
}

void test_record_context(void)
{
  TEST_ASSERT_TRUE(record_context_metrics(global_db, json_tokener_parse("{ \"keyName\": \"keyVal\" }")));
  TEST_ASSERT_EQUAL(SQLITE_OK, dbexec("SELECT COUNT(*) FROM request_meta_key"));
  TEST_ASSERT_EQUAL(1, nrows);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_smoke);
  RUN_TEST(test_record_context);
  return UNITY_END();
}
