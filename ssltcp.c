#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "ssltcp.h"

static void init_ssl(void);
static void end_ssl(void);
static int tcp_write(const char *buffer, int buffer_len);

static int verbose;
static int sock = -1;
static SSL_CTX *ssl_ctx;
static SSL *ssl;

static void
init_ssl(void)
{
  const SSL_METHOD *method;

  assert(ssl_ctx == NULL);

  SSL_library_init();
  OpenSSL_add_all_algorithms();
  SSL_load_error_strings();
  method = TLS_client_method();
  ssl_ctx = SSL_CTX_new(method);
  if (ssl_ctx == NULL) {
    ERR_print_errors_fp(stderr);
    exit(1);
  }
}

static void
end_ssl(void)
{
  SSL_CTX_free(ssl_ctx);
  ssl_ctx = NULL;
}

static int
tcp_write(const char *buffer, int buffer_len)
{
  int n, written;

  written = 0;
  do {
    n = SSL_write(ssl, buffer + written, buffer_len - written);
    if (n <= 0) {
      fprintf(stderr, "Write failed\n");
      return -1;
    }
    written += n;
  } while(written < buffer_len);
  return 0;
}

int
tcp_open(const char *hostname, const char *port, int verbose_)
{
  struct addrinfo hints;
  struct addrinfo *result, *addr;
  int err;

  verbose = verbose_;
  init_ssl();

  assert(sock == -1);
  assert(ssl == NULL);

  memset(&hints, 0, sizeof(hints));
  hints.ai_flags = 0;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  err = getaddrinfo(hostname, port, &hints, &result);
  if (err) {
    fprintf(stderr, "Failed to resolve server %s:%s : %s\n",
        hostname, port, gai_strerror(err));
    return 1;
  }
  for (addr = result; addr; addr = addr->ai_next) {
    sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (sock < 0)
      continue;
    if (connect(sock, addr->ai_addr, addr->ai_addrlen) < 0) {
      close(sock);
      continue;
    }
    break;
  }
  freeaddrinfo(result);
  if (addr == NULL) {
    fprintf(stderr, "Failed to connect to %s:%s\n", hostname, port);
    end_ssl();
    return 1;
  }

  ssl = SSL_new(ssl_ctx);
  if (ssl == NULL || SSL_set_fd(ssl, sock) == 0 || SSL_connect(ssl) != 1) {
    ERR_print_errors_fp(stderr);
    tcp_close();
    return 1;
  }
  if (verbose)
    fprintf(stderr, "* Connected to %s:%s\n", hostname, port);
  return 0;
}

void
tcp_close(void)
{
  SSL_free(ssl);
  if (sock != -1)
    close(sock);
  ssl = NULL;
  sock = -1;
  end_ssl();
}

int
tcp_write_line(const char *buffer, int buffer_len)
{
  const char crlf[] = "\r\n";
  if (verbose)
    printf("C: %.*s\n", buffer_len, buffer);
  if (tcp_write(buffer, buffer_len) || tcp_write(crlf, 2))
    return -1;
  return 0;
}

int
tcp_read_line(char *buffer, int buffer_size)
{
  int buffer_len, old_buffer_len, eol;
  buffer_len = eol = 0;
  for (;;) {
    old_buffer_len = buffer_len;
    buffer_len += SSL_peek(ssl, buffer + buffer_len, buffer_size - buffer_len);
    if (buffer_len < old_buffer_len) {
      fprintf(stderr, "Read failed.\n");
      return -1;
    }
    for (; eol < buffer_len - 1; eol++)
      if (buffer[eol] == '\r' && buffer[eol+1] == '\n')
        goto eol_found;
    SSL_read(ssl, buffer + buffer_len, buffer_len - old_buffer_len);
  }
eol_found:
  buffer_len = eol + 2;
  SSL_read(ssl, buffer + buffer_len, buffer_len - old_buffer_len);
  buffer[eol] = '\0';
  if (verbose)
    printf("S: %s\n", buffer);
  return 0;
}
