#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>

#include "sqlite3.h"

#include "h2o.h"
#include "h2o/http1.h"
#include "h2o/http2.h"
#include "h2o/socket/evloop.h"

#include "json-c/json.h"
#include "json-c/json_object.h"

#include "db.h"
#include "common.h"

static sqlite3 *global_db = NULL;

static h2o_timerwheel_t *timers = NULL;

static h2o_globalconf_t config;
static h2o_context_t ctx;
static h2o_accept_ctx_t accept_ctx;

static h2o_pathconf_t *register_handler(h2o_hostconf_t *hostconf, const char *path, int (*on_req)(h2o_handler_t *, h2o_req_t *));

bool record_context(json_object *context);

// Endpoints
static int ping(h2o_handler_t *, h2o_req_t *);
static int handle_flag(h2o_handler_t *, h2o_req_t *);
static int evaluate_flag(h2o_handler_t *, h2o_req_t *);

static void on_accept(h2o_socket_t *, const char *);
static int create_listener(void);

#define PATH(path, handler, enable_timing)                \
  {                                                       \
    pathconf = register_handler(hostconf, path, handler); \
    if (logfh != NULL)                                    \
      h2o_access_log_register(pathconf, logfh);           \
    if (enable_timing)                                    \
      h2o_server_timing_register(pathconf, 1);            \
  }

int main(int argc, char **argv)
{
  signal(SIGPIPE, SIG_IGN);

  if (initialize_db(&global_db) != 0)
  {
    fprintf(stderr, "failed to initialize db\n");
    return 1;
  }

  h2o_access_log_filehandle_t *logfh = h2o_access_log_open_handle("/dev/stdout", NULL, H2O_LOGCONF_ESCAPE_APACHE);
  h2o_config_init(&config);

  h2o_hostconf_t *hostconf = h2o_config_register_host(&config, h2o_iovec_init(H2O_STRLIT("default")), 65535);
  h2o_pathconf_t *pathconf = NULL;

  PATH("/ping", ping, true);
  PATH("/flag", handle_flag, true);
  PATH("/evaluate", evaluate_flag, true);

  pathconf = h2o_config_register_path(hostconf, "/", 0);
  h2o_file_register(pathconf, "./ui", NULL, NULL, 0);
  if (logfh != NULL)
  {
    h2o_access_log_register(pathconf, logfh);
  }

  // Initialize timerwheel and context.
  h2o_context_init(&ctx, h2o_evloop_create(), &config);
  timers = h2o_timerwheel_create(5, h2o_now(ctx.loop));

  accept_ctx.ctx = &ctx;
  accept_ctx.hosts = config.hosts;
  if (create_listener() < 0)
  {
    fprintf(stderr, "Failed");
    return 1;
  }

  fprintf(stderr, "starting to listen on port 7890\n");
  while (h2o_evloop_run(ctx.loop, INT32_MAX) == 0)
    ;

  fprintf(stderr, "shutting down\n");
  h2o_timerwheel_destroy(timers);
  if (close_db(&global_db) != 0)
    fprintf(stderr, "encountered error while closing db, but we're terminating so nbd\n");
  return 0;
}

static h2o_pathconf_t *register_handler(h2o_hostconf_t *hostconf, const char *path, int (*on_req)(h2o_handler_t *, h2o_req_t *))
{
  h2o_pathconf_t *pathconf = h2o_config_register_path(hostconf, path, 0);
  h2o_handler_t *handler = h2o_create_handler(pathconf, sizeof(*handler));
  handler->on_req = on_req;
  return pathconf;
}

int get_error_code_status(int error_code)
{
  return 415;
}

const char *get_status_reason(int status)
{
  return "Error";
}

const char *get_error_code_message(int error_code)
{
  return "Generic error";
}

int respond_str(h2o_req_t *req, const char *str)
{
  static h2o_generator_t generator = {NULL, NULL};

  h2o_start_response(req, &generator);
  h2o_iovec_t resp = h2o_iovec_init(STRLIT(str));
  h2o_send(req, &resp, 1, H2O_SEND_STATE_FINAL);
  return 0;
}

int respond_error(h2o_req_t *req, int error_code)
{
  req->res.status = get_error_code_status(error_code);
  req->res.reason = get_status_reason(req->res.status);
  const char *message = get_error_code_message(error_code);
  return respond_str(req, message);
}

#define NE_UNSUPPORTED_MEDIA_TYPE 0x0001
#define NE_DB_ERROR 0x0002
#define NE_CONFLICT 0x0003

#define ASSERT_REQ(expr, error_code)       \
  if (!(expr))                             \
  {                                        \
    return respond_error(req, error_code); \
  }

// Evaluate the state of a feature flag.
static int evaluate_flag(h2o_handler_t *self, h2o_req_t *req)
{
  static h2o_generator_t generator = {NULL, NULL};
  const char *_unused = NULL;

  ASSERT_REQ(strncmp(
                 req->headers.entries[h2o_find_header(&req->headers, H2O_TOKEN_CONTENT_TYPE, -1)].value.base,
                 STRLIT("application/json")) == 0,
             NE_UNSUPPORTED_MEDIA_TYPE);

  ASSERT_REQ(!db_begin(global_db), NE_DB_ERROR);

  sqlite3_stmt *statement = NULL;
  sqlite3_prepare_v2(
      global_db,
      STRLIT("SELECT * FROM feature_flags WHERE name = @name OR key = @key"),
      &statement,
      &_unused);

  int rows_found = 0;
  for (;;)
  {
    int result = sqlite3_step(statement);
    switch (result)
    {
    case SQLITE_ROW:
      rows_found++;
      break;
    case SQLITE_DONE:
      sqlite3_finalize(statement);
      break;
    default:
      sqlite3_finalize(statement);
      break;
    }
  }
  ASSERT_REQ(rows_found == 0, NE_CONFLICT);
  // req->res.reason = "Conflict";
  // h2o_send_inline(req, STRLIT("N0001 - a flag with the provided name or key already exists"));
  // return 0;

  // Get parameters for evaluation and record them.
  json_object *evaluation_parameters = json_tokener_parse(req->entity.base);

  h2o_iovec_t body = h2o_strdup(&req->pool, "true", SIZE_MAX);

  req->res.status = 200;
  req->res.reason = "OK";
  h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, NULL, H2O_STRLIT("text/plain; charset=utf-8"));
  h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_ACCESS_CONTROL_ALLOW_METHODS, NULL, H2O_STRLIT("*"));
  h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_ACCESS_CONTROL_ALLOW_HEADERS, NULL, H2O_STRLIT("*"));

  // Issue request to SQLite and wait for result.
  if (sqlite3_prepare_v2(
          global_db,
          // TODO: Replace with our own macro since this could change.
          STRLIT("SELECT id, name, key FROM feature_flags"),
          &statement,
          &_unused) != SQLITE_OK)
    return 1;

  struct row
  {
    int32_t id;
    char *name;
    char *key;
  };

  struct row *rows = NULL;
  int n_rows = 0;
  for (;;)
  {
    int result = sqlite3_step(statement);
    if (result == SQLITE_BUSY)
    {
      continue;
    }
    if (result == SQLITE_DONE)
    {
      break;
    }
    if (result == SQLITE_ROW)
    {
      struct row row;
      row.id = sqlite3_column_int(statement, 0);
      const unsigned char *name = sqlite3_column_text(statement, 1);
      const size_t name_len = strlen((const char *)name);
      const unsigned char *key = sqlite3_column_text(statement, 2);
      const size_t key_len = strlen((const char *)key);

      row.name = malloc((name_len + 1) * sizeof(char));
      row.key = malloc((key_len + 1) * sizeof(char));
      strlcpy(row.name, (const char *)name, name_len + 1);
      strlcpy(row.key, (const char *)key, key_len + 1);

      if (n_rows == 0)
      {
        rows = calloc(1, sizeof(struct row));
        memset(rows, 0, 1 * sizeof(struct row));
        n_rows = 0;
      }
      else
      {
        rows = realloc(rows, (n_rows + 1) * sizeof(struct row));
      }
      rows[n_rows] = row;
      n_rows++;
      continue;
    }

    sqlite3_finalize(statement);
    req->res.status = 500;
    req->res.reason = "Encountered an error while querying SQLite.";
    h2o_send(req, NULL, 0, H2O_SEND_STATE_FINAL);
    return 0;
  }

  h2o_start_response(req, &generator);

  // Build response data
  char str[4096];
  for (int k = 0; k < n_rows; k++)
  {
    struct row r = rows[k];
    strlcat(str, r.name, 4096);
    strlcat(str, "\n", 4096);
  }
  h2o_iovec_t resp_body = h2o_iovec_init(str, strlen(str));
  h2o_send(req, &resp_body, 1, H2O_SEND_STATE_FINAL);
  return 0;
}

static int ping(h2o_handler_t *self, h2o_req_t *req)
{
  static h2o_generator_t generator = {NULL, NULL};
  req->res.status = 200;
  req->res.reason = "OK";
  h2o_send_inline(req, H2O_STRLIT("pong"));
  return 0;
}

int get_flag_state(h2o_handler_t *self, h2o_req_t *req)
{
  return 0;
}

int update_flag(h2o_handler_t *self, h2o_req_t *req)
{
  static h2o_generator_t generator = {NULL, NULL};
  req->res.status = 201;
  req->res.reason = "Created";
  h2o_start_response(req, &generator);
  h2o_iovec_t body = h2o_strdup(&req->pool, H2O_STRLIT("Created flagname"));
  h2o_send(req, &body, 1, H2O_SEND_STATE_FINAL);
  return 0;
}

int create_flag(h2o_handler_t *self, h2o_req_t *req)
{
  sqlite3_stmt *statement = NULL;
  sqlite3_exec(global_db, "BEGIN", NULL, NULL, NULL);

  // TODO: Parse the body into a JSON blob.
  sqlite3_prepare_v2(
      global_db,
      STRLIT("INSERT INTO feature_flags (name, key) VALUES (@name, replace(@key, ' ', '-'))"),
      &statement,
      NULL);
  sqlite3_bind_text(
      statement,
      sqlite3_bind_parameter_index(statement, "@name"),
      req->entity.base,
      req->entity.len,
      NULL);
  sqlite3_bind_text(
      statement,
      sqlite3_bind_parameter_index(statement, "@key"),
      req->entity.base,
      req->entity.len,
      NULL);

  int64_t ff_id = -1;
  for (;;)
  {
    int result = sqlite3_step(statement);
    if (result == SQLITE_DONE)
    {
      ff_id = sqlite3_last_insert_rowid(global_db);
      break;
    }
    if (result == SQLITE_CONSTRAINT)
    {
      fprintf(stderr, "encountered error: %s\n", sqlite3_errmsg(global_db));
    }

    sqlite3_finalize(statement);
    req->res.status = 500;
    req->res.reason = "Failed to make query";
    h2o_send_inline(req, H2O_STRLIT("N0100 - failed to issue query"));
    return 1;
  }

  sqlite3_finalize(statement);
  sqlite3_exec(global_db, "COMMIT", NULL, NULL, NULL);

  static h2o_generator_t generator = {NULL, NULL};
  req->res.status = 200;
  req->res.reason = "OK";
  req->content_length = SIZE_MAX;
  h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, NULL, H2O_STRLIT("text/plain; charset=utf-8"));
  h2o_start_response(req, &generator);
  h2o_iovec_t vec = h2o_iovec_init(req->entity.base, SIZE_MAX);
  h2o_send(req, &vec, 1, H2O_SEND_STATE_FINAL);
  return 0;
}

static int handle_flag(h2o_handler_t *self, h2o_req_t *req)
{
  // Get flag state
  if (h2o_memis(req->method.base, req->method.len, H2O_STRLIT("GET")))
  {
    // Get specific flag by ID
    if (h2o_memis(req->entity.base, req->entity.len, H2O_STRLIT("leo")))
    {
      return 0;
    }
    return get_flag_state(self, req);
  }

  // Update flag
  if (h2o_memis(req->method.base, req->method.len, H2O_STRLIT("PUT")))
    return update_flag(self, req);

  // Create flag
  if (h2o_memis(req->method.base, req->method.len, H2O_STRLIT("POST")) &&
      h2o_memis(req->path_normalized.base, req->path_normalized.len, H2O_STRLIT("/flag/")))
    return create_flag(self, req);

  return -1;
}

static void on_accept(h2o_socket_t *listener, const char *err)
{
  h2o_socket_t *sock;

  if (err != NULL)
  {
    fprintf(stderr, "error accepting connection: %s", err);
    return;
  }

  if ((sock = h2o_evloop_socket_accept(listener)) == NULL)
    return;
  h2o_accept(&accept_ctx, sock);
}

static int create_listener(void)
{
  struct sockaddr_in addr;
  int fd, reuseaddr_flag = 1;
  h2o_socket_t *sock;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(0x7f000001); // 127.0.0.1
  addr.sin_port = htons(7890);

  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ||
      setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_flag, sizeof(reuseaddr_flag)) != 0 ||
      bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, SOMAXCONN) != 0)
    return -1;

  sock = h2o_evloop_socket_create(ctx.loop, fd, H2O_SOCKET_FLAG_DONT_READ);
  h2o_socket_read_start(sock, on_accept);
  return 0;
}
