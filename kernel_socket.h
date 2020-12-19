#ifndef __KERNEL_SOCKET_H
#define __KERNEL_SOCKET_H

#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_pipe.h"

/* Sockets */

typedef struct socket_control_block socket_cb;

typedef enum {
  SOCKET_LISTENER,
  SOCKET_UNBOUND,
  SOCKET_PEER
}socket_type;


typedef struct listener_socket
{
	rlnode queue;
	CondVar req_available;
}listener_socket;


typedef struct unbound_socket
{
	rlnode unbound_socket;
}unbound_socket;


typedef struct peer_socket
{
	socket_cb *peer;
	pipe_cb *write_pipe;
	pipe_cb *read_pipe;
}peer_socket;


typedef struct connection_request
{
  int admitted;
	socket_cb *peer;
	CondVar connected_cv;
	rlnode queue_node;
}connection_request;


typedef struct socket_control_block {

  uint refcount;
  FCB *fcb;
  socket_type type;
  port_t port;

  union {
    listener_socket *listener_s;
    unbound_socket *unbound_s;
    peer_socket *peer_s;
  };

}socket_cb;

#endif