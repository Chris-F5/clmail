#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "ssltcp.h"

static int smtp_response();

char SMTP_BUFFER[1024];

static int
smtp_response()
{
  for (;;) {
    if (tcp_read_line(SMTP_BUFFER, sizeof(SMTP_BUFFER)))
      return -1;
    if (isdigit(SMTP_BUFFER[0]) && isdigit(SMTP_BUFFER[1]) && isdigit(SMTP_BUFFER[2]) && SMTP_BUFFER[3] == ' ') {
      return (SMTP_BUFFER[0] - '0') * 100
           + (SMTP_BUFFER[1] - '0') * 10
           + (SMTP_BUFFER[2] - '0');
    }
  }
}

int
main(int argc, char *argv[])
{
  int status, line, i, intermediate, verbose;
  char *hostname, *port;

  hostname = "smtp.gmail.com";
  port = "465";
  line = 0;
  verbose = 1;
  intermediate = 0;

  if(tcp_open(hostname, port, verbose))
    return 1;

  status = smtp_response();
  if (status / 100 != 2) {
    fprintf(stderr, "STMP failed to initialize with status %d.\n", status);
    goto smtp_error;
  }
  while (fgets(SMTP_BUFFER, sizeof(SMTP_BUFFER), stdin)) {
    line++;
    for (i = 0; SMTP_BUFFER[i] != '\n' && SMTP_BUFFER[i] != '\0'; i++);
    SMTP_BUFFER[i] = '\0';
    if (tcp_write_line(SMTP_BUFFER, strlen(SMTP_BUFFER)))
      goto smtp_error;
    if (SMTP_BUFFER[0] == '.' && SMTP_BUFFER[1] == '\0')
      intermediate = 0;
    if (!intermediate) {
      status = smtp_response();
      if (status / 100 == 3) {
        intermediate = 1;
      } else if (status / 100 != 2) {
        fprintf(stderr, "STMP failed on line %d with status %d.\n", line, status);
        goto smtp_error;
      }
    }
  }
  tcp_close();
  return 0;
smtp_error:
  tcp_close();
  return 1;
}
