default: debug

BIN_NAME=fastforward

L_TLS=`pkg-config --libs --cflags openssl`
L_H2O=`PKG_CONFIG_PATH=libh2o/lib/pkgconfig pkg-config --cflags --libs --static libh2o-evloop`
L_SQLITE=`PKG_CONFIG_PATH=libsqlite/lib/pkgconfig pkg-config --libs --cflags --static sqlite3`

L_UNITY=-Ilibunity/include -Llibunity/lib -lunity

# static linking is broken for json-c
L_JSON=`PKG_CONFIG_PATH=libjson/lib/pkgconfig pkg-config --libs --cflags --static json-c`

LIBS=-DSQLITE_THREADSAFE=0 \
	 $(L_SQLITE) \
	 $(L_TLS) \
	 -lm \
	 -Ilibjson/include \
	 libjson/lib/libjson-c.a \
	 -DH2O_USE_LIBUV=0 \
	 $(L_H2O)

SRCS=db.c evaluation.c main.c

.PHONY: release
release:
	rm -f $(BIN_NAME) && \
	$(CC) $(CFLAGS) $(LDFLAGS) \
	-o $(BIN_NAME) \
	-O3 \
	-std=c99 \
	$(LIBS) \
	$(SRCS)

.PHONY: debug
debug:
	rm -f $(BIN_NAME) && \
	$(CC) $(CFLAGS) $(LDFLAGS) \
	-o $(BIN_NAME)_debug \
	-O0 \
	-std=c99 \
	-g \
	$(LIBS) \
	$(SRCS)

test-db:
	$(CC) $(CFLAGS) $(LDFLAGS) \
	-o $(BIN_NAME)_$@ \
	-O0 \
	-std=c99 \
	-g \
	$(LIBS) \
	$(L_UNITY) \
	evaluation.c \
	db.c \
	test_db.c

test-evaluation:
	$(CC) $(CFLAGS) $(LDFLAGS) \
	-o $(BIN_NAME)_$@ \
	-O0 \
	-std=c99 \
	-g \
	$(LIBS) \
	$(L_UNITY) \
	evaluation.c \
	test_evaluation.c

.PHONY: load
load:
	k6 run -u 100 -d 10s load/ping.js
