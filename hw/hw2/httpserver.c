#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"

/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

void notfound_response(int num, int fd, char *msg1, char *msg2);
void file_response(int fd, int fd2, char *absolutefilepath, struct stat *statbuf);
void dir_response(int fd, char *absolutefilepath, char *request_path);

void delallincline(char *s)
{
  int i = strlen(s) - 1;
  while(i >= 0)
  {
    if (s[i] != '/')
    {
      break;
    }
    else
    {
      s[i] = '\0';
      i--;
    }
  }
}

/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 */
void handle_files_request(int fd) {

  /*
   * TODO: Your solution for Task 1 goes here! Feel free to delete/modify *
   * any existing code.
   */

  struct http_request *request = http_request_parse(fd);
  char *absolutefilepath = (char *) calloc(1024, sizeof(char));
  struct stat *statbuf = (struct stat *)malloc(sizeof(struct stat));
  /*if (getcwd(absolutefilepath, 1024) == NULL)
  {
    perror("getcwd");
    exit(EXIT_FAILURE);
  }*/
  //absolutefilepath[0] = '.';
  //strcat(absolutefilepath, "/");
  strcat(absolutefilepath, server_files_directory);
  strcat(absolutefilepath, "/");
  strcat(absolutefilepath, request->path);
  delallincline(absolutefilepath);
  if (stat(absolutefilepath, statbuf) == 0)
  {
    if (S_ISREG(statbuf->st_mode))
    {
      int fd2 = open(absolutefilepath, O_RDONLY);
      if (fd2 == -1)
      {
        perror("open");
        char *msg = (char *) calloc(12, sizeof(char));
        strcpy(msg, "Can't open ");
        notfound_response(404, fd, msg, absolutefilepath);
        free(msg);
      }
      else
      {
        file_response(fd, fd2, absolutefilepath, statbuf);
        close(fd2);
      }
    }
    else if (S_ISDIR(statbuf->st_mode))
    {
      char *absolutefilepathplusindexhtml = (char *) calloc(1024, sizeof(char));
      strcpy(absolutefilepathplusindexhtml, absolutefilepath);
      strcat(absolutefilepathplusindexhtml, "/");
      strcat(absolutefilepathplusindexhtml, "index.html");
      int fd2 = open(absolutefilepathplusindexhtml, O_RDONLY);
      if (fd2 != -1 && (stat(absolutefilepathplusindexhtml, statbuf) == 0))
      {
        file_response(fd, fd2, absolutefilepathplusindexhtml, statbuf);
        close(fd2);
      }
      else
      {
        dir_response(fd, absolutefilepath, request->path);
      }
      free(absolutefilepathplusindexhtml);
    }
    else
    {
      char *msg = (char *) calloc(30, sizeof(char));
      strcpy(msg, " is not a regular file or dir");
      notfound_response(404, fd, absolutefilepath, msg);
      free(msg);
    }
  }
  else
  {
    char *msg = (char *) calloc(12, sizeof(char));
    strcpy(msg, "Can't find ");
    notfound_response(404, fd, msg, absolutefilepath);
    free(msg);
  }

  free(statbuf);
  free(absolutefilepath);
  free(request->method);
  free(request->path);
  free(request);
  shutdown(fd, SHUT_RD);
  close(fd);
}

void notfound_response(int num, int fd, char *msg1, char *msg2)
{
  http_start_response(fd, num);
  http_send_header(fd, "Content-Type", "text/html");
  http_end_headers(fd);
  http_send_string(fd, "<center>"
                       "<h1>404 NOTFOUND</h1>"
                       "<hr>"
                       "<p>");
  http_send_string(fd, msg1);
  http_send_string(fd, msg2);
  http_send_string(fd, "</p>"
                       "</center>");
}
void file_response(int fd, int fd2, char *absolutefilepath, struct stat *statbuf)
{
  char *lenBuf = (char *) calloc(128, sizeof(char));
  char *buf = (char *) calloc(1024, sizeof(char));
  int read_len;
  sprintf(lenBuf, "%zu", statbuf->st_size);
  http_start_response(fd, 200);
  http_send_header(fd, "Content-type", http_get_mime_type(absolutefilepath));
  http_send_header(fd, "Content-Length", lenBuf);
  http_end_headers(fd);
  while((read_len = read(fd2, buf, 1024)) != 0)
  {
    http_send_data(fd, buf, read_len);
  }
  free(lenBuf);
  free(buf);
}
void dir_response(int fd, char *absolutefilepath, char *request_path)
{
  DIR *currentdir;
  struct dirent *currentdp;
  if ((currentdir = opendir(absolutefilepath)) != NULL)
  {
    http_start_response(fd, 200);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    /*if (request_path[strlen(request_path) - 1] != '/')
    {
        request_path = strrchr(request_path, '/');
        request_path++;
        char *p = (char *) calloc(1024, sizeof(char));
        while((currentdp = readdir(currentdir)) != NULL)
        {
          memset(p, 0, 1024);
          strcpy(p, request_path);
          strcpy(p, "/");
          strcpy(p, currentdp->d_name);
          http_send_string(fd, "<a href=\"");
          http_send_string(fd, p);
          http_send_string(fd, "\">");
          http_send_string(fd, currentdp->d_name);
          http_send_string(fd, "</a><br>");
        }
        free(p);
    }
    else
    {*/
      while((currentdp = readdir(currentdir)) != NULL)
      {
        http_send_string(fd, "<a href=\"");
        http_send_string(fd, currentdp->d_name);
        http_send_string(fd, "\">");
        http_send_string(fd, currentdp->d_name);
        http_send_string(fd, "</a><br>");
      }
    //}
  }
  else
  {
    notfound_response(404, fd, "Can't open dir ", absolutefilepath);
  }
  closedir(currentdir);
}

struct inandout_fds
{
  int in_fd;
  int out_fd;
};

void * transfer_data(void *arg)
{
  struct inandout_fds *fds = (struct inandout_fds *) arg;
  int in = fds->in_fd;
  int out = fds->out_fd;
  char buf[1024];
  int inlen;
  while(1)
  {
    memset(buf, 0, sizeof(buf));
    inlen = read(in, buf, sizeof(buf));
    if (inlen <= 0)
    {
      break;
    }
    http_send_data(out, buf, inlen);
  }
  close(out);
  return NULL;
}

/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
void handle_proxy_request(int fd) {

  /*
  * The code below does a DNS lookup of server_proxy_hostname and 
  * opens a connection to it. Please do not modify.
  */

  struct sockaddr_in target_address;
  memset(&target_address, 0, sizeof(target_address));
  target_address.sin_family = AF_INET;
  target_address.sin_port = htons(server_proxy_port);

  struct hostent *target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);

  int client_socket_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (client_socket_fd == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
    exit(errno);
  }

  if (target_dns_entry == NULL) {
    fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
    exit(ENXIO);
  }

  char *dns_address = target_dns_entry->h_addr_list[0];

  memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
  int connection_status = connect(client_socket_fd, (struct sockaddr*) &target_address,
      sizeof(target_address));

  if (connection_status < 0) {
    /* Dummy request parsing, just to be compliant. */
    http_request_parse(fd);

    http_start_response(fd, 502);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    http_send_string(fd, "<center><h1>502 Bad Gateway</h1><hr></center>");
    return;

  }

  /* 
  * TODO: Your solution for task 3 belongs here! 
  */
  pthread_t tid1, tid2;
  struct inandout_fds fds1, fds2;

  fds1.in_fd = fd;
  fds1.out_fd = client_socket_fd;
  if (pthread_create(&tid1, NULL, transfer_data, &fds1) != 0)
  {
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }
  fds2.out_fd = fd;
  fds2.in_fd = client_socket_fd;
  if (pthread_create(&tid2, NULL, transfer_data, &fds2) != 0)
  {
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }
  if (pthread_join(tid1, NULL) != 0)
  {
    perror("pthread_join");
    exit(EXIT_FAILURE);
  }
  if (pthread_join(tid2, NULL) != 0)
  {
    perror("pthread_join");
    exit(EXIT_FAILURE);
  }
  shutdown(fd, SHUT_RD);
  close(fd);
  shutdown(client_socket_fd, SHUT_RD);
  close(client_socket_fd);
}

void *start_routine(void *arg)
{
  void (*request_handler)(int) = arg;
  pthread_detach(pthread_self());
  int fd2;
  while(1){
    fd2 = wq_pop(&work_queue);
    request_handler(fd2);
    close(fd2);
  }
  return NULL;
}


void init_thread_pool(int num_threads, void (*request_handler)(int)) {
  /*
   * TODO: Part of your solution for Task 2 goes here!
   */
  if (num_threads == 0)
  {
    return;
  }
  wq_init(&work_queue);
  pthread_t tid[num_threads];
  for (unsigned int i = 0; i < num_threads; i++)
  {
    if(pthread_create(&tid[i], NULL, start_routine, request_handler) != 0)
    {
      perror("pthread_create");
      exit(EXIT_FAILURE);
    }
  }
}

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *) &server_address,
        sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  init_thread_pool(num_threads, request_handler);

  while (1) {
    client_socket_number = accept(*socket_number,
        (struct sockaddr *) &client_address,
        (socklen_t *) &client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);

    // TODO: Change me?
    if (num_threads == 0)
    {
      request_handler(client_socket_number);
      close(client_socket_number);
    }
    else
    {
      wq_push(&work_queue, client_socket_number);
    }
    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);
  }

  shutdown(*socket_number, SHUT_RDWR);
  close(*socket_number);
}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char *num_threads_str = argv[++i];
      if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
