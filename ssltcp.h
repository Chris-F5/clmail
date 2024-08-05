int tcp_open(const char *hostname, const char *port, int verbose);
void tcp_close(void);
int tcp_read_line(char *buffer, int buffer_size);
int tcp_write_line(const char *buffer, int buffer_len);
