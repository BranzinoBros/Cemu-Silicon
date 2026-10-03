#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAEINPROGRESS EINPROGRESS
#define WSAESHUTDOWN ESHUTDOWN
#define WSAECONNABORTED ECONNABORTED
#define WSAHOST_NOT_FOUND EAI_NONAME
#define WSAENOTCONN ENOTCONN

#define GETLASTERR errno

#define SOCKET int
#define closesocket close
#define SOCKET_ERROR -1
#define INVALID_SOCKET -1

