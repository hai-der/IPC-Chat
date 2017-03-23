/*
 * Haider Tiwana
 * Operating Systems
 * Lab 2
 * client.c
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

char buf[1024];

/**
 * nonblock - a function that makes a file descriptor non-blocking
 * @param fd file descriptor
 */
void nonblock(int fd) {
  int flags;

  if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
    perror("fcntl (get):");
    exit(1);
  }
  if (fcntl(fd, F_SETFL, flags | FNDELAY) == -1) {
    perror("fcntl (set):");
    exit(1);
  }
}

int main(int argc, char **argv) {
  struct sockaddr_un remote;
  int sfd, rbytes, max;
  fd_set myfds;

  // create new socket, define path
  sfd = socket(AF_UNIX, SOCK_STREAM, 0);
  nonblock(sfd);
  remote.sun_family = AF_UNIX;
  sprintf(remote.sun_path, "%s", ".chatsock");

  // attempt connection to socket .chatsock
  if(connect(sfd, (struct sockaddr *)&remote, sizeof(remote)) == -1) {
    perror("connect error");
    exit(-1);
  } else
    printf("Connection accepted!");

  while(1) {
    // initialize fd_set
    FD_ZERO(&myfds);
    FD_SET(sfd, &myfds);
    FD_SET(STDIN_FILENO, &myfds);

    // set max to appropriate descriptor
    if (STDIN_FILENO > sfd)
      max = STDIN_FILENO;
    else
      max = sfd;
    nonblock(max);

    // call asynchronous I/O
    int rv = select(max+1, &myfds, NULL, NULL, NULL);
    if (rv == -1)
      perror("select error");

    // activity detected
    if (rv > 0) {
      if (FD_ISSET(STDIN_FILENO, &myfds)) {
        rbytes = read(STDIN_FILENO, &buf, sizeof(buf));
        // write to server
        write(sfd, &buf, rbytes);
      }
      if (FD_ISSET(sfd, &myfds)) {
        rbytes = read(sfd, &buf, sizeof(buf));
        // write to STDOUT
        write(STDOUT_FILENO, &buf, rbytes);
      }
    }
  }

  // close open fds
  close(sfd);
}