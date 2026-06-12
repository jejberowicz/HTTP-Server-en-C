CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -g
TARGET  = httpserver
SRCS    = main.c http_request.c http_response.c static_files.c \
          router.c threadpool.c io.c tls.c
LDFLAGS = -lpthread -lssl -lcrypto

.PHONY: all clean cert

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Generate a self-signed certificate for local TLS testing
cert:
	openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem \
	    -days 365 -nodes -subj "/CN=localhost"

clean:
	rm -f $(TARGET) cert.pem key.pem
