CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11
LDFLAGS = -lpthread -lssl -lcrypto
TARGET  = httpserver
SRCS    = main.c http_request.c http_response.c static_files.c \
          router.c threadpool.c io.c tls.c

.PHONY: all debug release sanitize cert clean

# Default: optimized build with debug info
all: CFLAGS += -O2 -g
all: $(TARGET)

# Unoptimized build — faster recompilation during development
debug: CFLAGS += -O0 -g
debug: $(TARGET)

# Optimized, no debug symbols — for production/benchmarking
release: CFLAGS += -O3 -DNDEBUG
release: $(TARGET)

# AddressSanitizer + UBSan — catch memory bugs at runtime
sanitize: CFLAGS  += -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize: LDFLAGS += -fsanitize=address,undefined
sanitize: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Generate a self-signed certificate for local TLS testing
cert:
	openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem \
	    -days 365 -nodes -subj "/CN=localhost"

clean:
	rm -f $(TARGET) cert.pem key.pem
