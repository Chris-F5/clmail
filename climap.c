#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>
#include <stdarg.h>
#include "ssltcp.h"

static int is_digits(const char *str);
static int write_file(const char *str, const char *fname_format, ...);
static int strcmps(const char *str, const char **cmps);
static int read_greeting(void);
static int handle_untagged_response(void);
static int read_response(const char *tag);

static int verbose;
static char IMAP_BUFFER[1024];
static FILE *swpipe, *crpipe;

static int
is_digits(const char *str)
{
  if (str[0] == '\0')
    return 0;
  for (; *str != '\0'; str++)
    if (!isdigit(*str))
      return 0;
  return 1;
}

static int
write_file(const char *str, const char *fname_format, ...)
{
  va_list args;
  char fname_buffer[256];
  FILE *file;
  va_start(args, fname_format);
  if (vsnprintf(fname_buffer, sizeof(fname_buffer), fname_format, args) > 255) {
    fprintf(stderr, "Filename too long %s...\n", fname_buffer);
    return 1;
  }
  va_end(args);
  if (verbose)
    fprintf(stderr, "W: %s (%s)\n", fname_buffer, str);
  file = fopen(fname_buffer, "w");
  if (file == NULL) {
    fprintf(stderr, "Failed to open file %s for writing\n", fname_buffer);
    return 1;
  }
  fwrite(str, 1, strlen(str), file);
  if (ferror(file)) {
    fprintf(stderr, "Failed to write to %s\n", fname_buffer);
    return 1;
  }
  fclose(file);
  return 0;
}

static int
strcmps(const char *str, const char **cmps)
{
  for(; *cmps != NULL; cmps++)
    if (strcmp(str, *cmps) == 0)
      return 1;
  return 0;
}

static int
read_greeting(void)
{
  if (tcp_read_line(IMAP_BUFFER, sizeof(IMAP_BUFFER)))
    return 1;
  if (strncmp(IMAP_BUFFER, "* OK", strlen("* OK"))) {
    fprintf(stderr, "Unexpected server greeting.\n");
    return 1;
  }
  return 0;
}

static int
handle_untagged_response(void)
{
  char *head, *tail;
  if (IMAP_BUFFER[1] == '+')
    return 0; /* I dont support continue requests. */
  if (IMAP_BUFFER[1] != ' ') {
    fprintf(stderr, "Expected space in untagged response.\n");
    return 1;
  }
  head = IMAP_BUFFER + 2;
  tail = strchrnul(head, ' ') + 1;
  *(tail- 1) = '\0';
  if (strcmps(head, (const char *[]){"OK", "NO", "BAD", "BYE", NULL})) {
    write_file(tail, "./condition/%s", head);
  } else if(strcmp(head, "CAPABILITY") == 0)  {
    write_file(tail, "./CAPABILITY");
  } else if (is_digits(head) && strcmps(tail, (const char *[]){"EXISTS", "RECENT", NULL})) {
    write_file(head, "./mailbox/%s", tail);
  }

  return 0;
}

static int
read_response(const char *tag)
{
  for (;;) {
    if (tcp_read_line(IMAP_BUFFER, sizeof(IMAP_BUFFER)))
      return 1;
    if (IMAP_BUFFER[0] == '*') {
      handle_untagged_response();
      continue;
    }
    if (strncmp(IMAP_BUFFER, tag, strlen(tag))) {
      fprintf(stderr, "Unexpected tag in imap server response.\n");
      return 1;
    }
    if (strncmp(IMAP_BUFFER + strlen(tag), " OK ", strlen(" OK ")) == 0)
      return 0;
    else
      return 1;
  }
}

int
main(int argc, char *argv[])
{
  int spipefd[2], cpipefd[2];
  unsigned int tag_num;
  char tag_str[5];
  int cpid, status, ret, i;
  char *hostname, *port, *script_path;
  char *script_arg[2];

  ret = 0;
  swpipe = crpipe = NULL;
  spipefd[0] = spipefd[1] = -1;
  cpipefd[0] = cpipefd[1] = -1;
  cpid = -1;
  tag_num = 0;

  script_path = "./imap-test.sh";
  script_arg[0] = script_path;
  script_arg[1] = NULL;

  hostname = "imap.gmail.com";
  port = "993";
  verbose = 1;

  if (pipe(spipefd) || pipe(cpipefd)) {
    fprintf(stderr, "Failed to open pipe (%s)\n", strerror(errno));
    ret = 1;
    goto cleanup;
  }

  cpid = fork();
  if (cpid < 0) {
    fprintf(stderr, "Failed to fork (%s)\n", strerror(errno));
    goto failure;
  }
  if (cpid == 0) {
    close(cpipefd[0]);
    close(spipefd[1]);
    dup2(cpipefd[1], 1);
    dup2(spipefd[0], 0);
    putenv("DIR=.");
    execvp(script_path, script_arg);
    fprintf(stderr, "Failed to exec '%s' (%s)", script_path, strerror(errno));
    goto failure;
  }
  close(cpipefd[1]);
  close(spipefd[0]);
  crpipe = fdopen(cpipefd[0], "r");
  swpipe = fdopen(spipefd[1], "w");

  if(tcp_open(hostname, port, verbose))
    goto failure;
  if (read_greeting())
    goto failure;

  while (fgets(IMAP_BUFFER + 5, sizeof(IMAP_BUFFER) - 5, crpipe)) {
    snprintf(tag_str, 5, "%04d", tag_num++);
    tag_num = tag_num % 9999;
    memcpy(IMAP_BUFFER, tag_str, 4);
    IMAP_BUFFER[4] = ' ';
    for (i = 0; IMAP_BUFFER[i] != '\n' && IMAP_BUFFER[i] != '\0'; i++);
    IMAP_BUFFER[i] = '\0';
    if (tcp_write_line(IMAP_BUFFER, strlen(IMAP_BUFFER)))
      goto failure;
    if (read_response(tag_str))
      goto failure;
  }

  goto cleanup;
failure:
  ret = 1;
cleanup:
  tcp_close();
  if (cpid > 0) {
    waitpid(cpid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      fprintf(stderr, "Child process did not exit normally.\n");
      ret = 1;
    }
  }
  if (crpipe) fclose(crpipe);
  if (swpipe) fclose(swpipe);
  if (cpipefd[0] > 0) close(cpipefd[0]);
  if (cpipefd[1] > 0) close(cpipefd[1]);
  if (spipefd[0] > 0) close(spipefd[0]);
  if (spipefd[1] > 0) close(spipefd[1]);
  return ret;
}
