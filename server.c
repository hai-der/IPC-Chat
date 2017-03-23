/* Haider Tiwana
 * Operating Systems
 * Lab 2
 * server.c - a chat server (and monitor) that uses pipes and sockets
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/wait.h>

#define MAX_CLIENTS 10

// constants for pipe FDs
#define WFD 1
#define RFD 0
char buf[1024];

/**
 * nonblock - a function that makes a file descriptor non-blocking
 * @param fd file descriptor
 */
void nonblock(int fd) {
  int flags;
  //  printf("%d\n", fd);
  if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
    perror("fcntl (get):");
    exit(1);
  }
  if (fcntl(fd, F_SETFL, flags | FNDELAY) == -1) {
    perror("fcntl (set):");
    exit(1);
  }
}

/*
 * monitor - provides a local chat window
 * @param srfd - server read file descriptor
 * @param swfd - server write file description
 */

void monitor(int srfd, int swfd) {
  fd_set myfds;
  int rbytes, max;
  char buf[1024];

  while(1) {
    // initialize fd_set
    FD_ZERO(&myfds);
    FD_SET(srfd, &myfds);
    FD_SET(STDIN_FILENO, &myfds);

    // set max to appropriate descriptor
    if (STDIN_FILENO > srfd) {
      max = STDIN_FILENO;
    } else {
      max = srfd;
    }
    nonblock(max);

    // call asynchronous I/O
    int rv = select(max+1, &myfds, NULL, NULL, NULL);
    if (rv == -1)
      perror("select error");

    // activity detected
    if (rv > 0) {
      if (FD_ISSET(STDIN_FILENO, &myfds)) {
        rbytes = read(STDIN_FILENO, &buf, sizeof(buf));
        write(swfd, &buf, rbytes); // write to server
      }
      if (FD_ISSET(srfd, &myfds)) {
        rbytes = read(srfd, &buf, sizeof(buf));
        write(STDOUT_FILENO, &buf, rbytes); // write to STDOUT 
      }
    }
  }

  // close open fds
  close(srfd);
  close(swfd);
}

/*
 * server - relays chat messages
 * @param mrfd - monitor read file descriptor
 * @param mwfd - monitor write file descriptor
 */

void server(int mrfd, int mwfd) {
  struct timeval tv;
  int master_socket, new_socket, max_sd, sd, activity, rbytes;
  int client_socket[MAX_CLIENTS];
  fd_set listfds;
  struct sockaddr_un local, remote;
  socklen_t socklen;

  // create new socket, define its path, unlink, bind, and listen for connections
  master_socket = socket(AF_UNIX, SOCK_STREAM, 0);
  nonblock(master_socket);
  local.sun_family = AF_UNIX;
  remote.sun_family = AF_UNIX;
  sprintf(local.sun_path, "%s", ".chatsock");
  unlink(local.sun_path);
  bind(master_socket, (struct sockaddr *)&local, sizeof(local));
  listen(master_socket, MAX_CLIENTS);

  // initialize all client_socket[] to -1 so not checked
  memset(client_socket, -1, sizeof(client_socket));


  while(1) {
    // (possibly) handle new connection
    new_socket = accept(master_socket, (struct sockaddr *)&remote, &socklen);
    if ((new_socket == -1) && (errno != EWOULDBLOCK)) {
      perror("error accepting");
    }
    else if (new_socket > -1) {
      nonblock(new_socket);
      // handle new connection, i.e. set it to active in fd array
      for(int i =0; i < MAX_CLIENTS; i++) {
        if(client_socket[i] == -1) {
          client_socket[i] = new_socket;
          break;
        }
      }
    }

    // clear the fd array
    FD_ZERO(&listfds);
    FD_SET(mrfd, &listfds);
    max_sd = mrfd;

    // check if fd is active, if so then add it to fd_set
    for (int i=0; i < MAX_CLIENTS; i++) {
      if (client_socket[i] > -1) {
        FD_SET(client_socket[i], &listfds);
        // set max_sd to highetsest active fd
        if(client_socket[i] > max_sd)
          max_sd = client_socket[i];
      }
    }

    // wait for activity on sockets indefinitely
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    activity = select(max_sd+1, &listfds, NULL, NULL, &tv);

    // error
    if(activity < 0)
      printf("select error");

    // broadcast any monitor message to all clients
    if(activity > 0) {
      // check if monitor has anything to say, if so read()
      if(FD_ISSET(mrfd, &listfds)) {
        rbytes = read(mrfd, &buf, sizeof(buf));

        // error
        if (rbytes < 0)
          perror("monitor read:");

        // write monitor message to all active fds
        for(int i=0; i < MAX_CLIENTS; i++) {
          if(client_socket[i] > -1)
            write(client_socket[i], &buf, rbytes);
          // signal EOF
          if (rbytes == 0)
            close(mrfd);
        }
      }

      //loop over every fd in client_socket[], checking for fds that have message
      // if so, then read() and broadcast
      for(int i=0; i < MAX_CLIENTS; i++) {
        if(FD_ISSET(client_socket[i], &listfds)) {
          // sd is current active fd with message
          sd = client_socket[i];
          rbytes = read(sd, &buf, sizeof(buf));

          // error
          if (rbytes == -1)
            perror("socket descriptor");

          // write client message to monitor
          write(mwfd, &buf, rbytes);

          // write to all active fds other than itself
          for(int j = 0; j < MAX_CLIENTS; j++) {
            if(client_socket[j] != client_socket[i]) {
              write(client_socket[j], &buf, rbytes);
              // signal EOF
              if(rbytes == 0) {
                close(client_socket[i]);
                // set client_socket back to default
                client_socket[i] = -1;
              }
            }
          }
        }
      }
    }
  }
}

int main(int argc, char **argv) {

  int s2mp[2];
  int m2sp[2];
  pid_t pid;

  // initialize pipes
  nonblock(pipe(s2mp));
  nonblock(pipe(m2sp));

  // initialize fork
  pid = fork();
  if (pid < 0) {
    perror("fork error:");
    return 1;
  }
  else if (pid == 0) {
    // in child process
    // close unneeded pipes
    close(m2sp[RFD]);
    close(s2mp[WFD]);

    // invoke listener
    monitor(s2mp[RFD], m2sp[WFD]);

    // close needed pipes
    close(m2sp[WFD]);
    close(s2mp[RFD]);
    exit(0);

  } else {
    // in parent process
    // close uneeeded pipes
    close(s2mp[RFD]);
    close(m2sp[WFD]);

    // invoke talker
    server(m2sp[RFD], s2mp[WFD]);

    // close needed pipes
    close(s2mp[WFD]);
    close(m2sp[RFD]);
    wait(NULL);
  }
  return 0;
}