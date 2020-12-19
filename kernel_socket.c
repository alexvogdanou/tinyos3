
#include "tinyos.h"
#include "util.h"
#include "kernel_socket.h"
#include "kernel_sched.h"
#include "kernel_cc.h"

/* MAXPORT is also a legit port */
socket_cb *PORT_MAP[MAX_PORT + 1] = {NULL};

void *fail_open(uint minor){
	return NULL;	
}	

int socket_read(void *socketcb_t, char *buf, unsigned int n){
	
	socket_cb *socket = (socket_cb *)socketcb_t;

	if(socket == NULL)
		return -1;
	
	/* No connection established */
	if(socket->type != SOCKET_PEER)
		return -1;
	
	pipe_cb *read_pipe = socket->peer_s->read_pipe;
	
	return pipe_read(read_pipe, buf, n);
}

int socket_write(void *socketcb_t, const char *buf, unsigned int n){
	
	socket_cb *socket = (socket_cb *)socketcb_t;

	if(socket == NULL)
		return -1;
	
	/* No connection established */
	if(socket->type != SOCKET_PEER)
		return -1;

	pipe_cb *write_pipe = socket->peer_s->write_pipe;

	return pipe_write(write_pipe, buf, n);
}

int socket_close(void *_socketcb){
	
	socket_cb *socket = (socket_cb *)_socketcb;
	
	if(socket == NULL)
		return -1;

	switch(socket->type){

		case SOCKET_UNBOUND:
			break;
		case SOCKET_LISTENER:
			PORT_MAP[socket->port] = NULL; //port can be used by another socket now
			
			while(!is_rlist_empty(&socket->listener_s->queue)){
				rlnode *req = rlist_pop_front(&socket->listener_s->queue);
				kernel_signal(&req->request->connected_cv);
			}
			kernel_signal(&socket->listener_s->req_available);	//signal the listener socket

			free(socket->listener_s);
			break;
		case SOCKET_PEER:
			/* Case we are the first of the two sockets to close so we need to free the two pipes */
			if(socket->peer_s->peer != NULL){
				/* Close the two pipes */
				pipe_reader_close(socket->peer_s->read_pipe);
				pipe_writer_close(socket->peer_s->write_pipe);
		
				/* The other peer won't point back to us and won't try to free the already freed pipes */
				socket->peer_s->peer->peer_s->peer = NULL;
			}
			free(socket->peer_s);
			break;
		default:
			break;
	}
	free(socket);
	return 0;
}

static file_ops socket_file_ops = {
	.Open = fail_open,
	.Read = socket_read,
	.Write = socket_write,
	.Close = socket_close
};

Fid_t sys_Socket(port_t port)
{
	/* Check if this is a legal port */
	if(port < NOPORT || port > MAX_PORT)
		return NOFILE;

	Fid_t fid;
	FCB *fcb;

	/* check whether the available file ids for the process are exhausted */
	if(FCB_reserve(1, &fid, &fcb) == 0)
		return NOFILE;
	
	socket_cb *socket = (socket_cb *)xmalloc(sizeof(socket_cb));

	fcb->streamfunc = &socket_file_ops;
	fcb->streamobj = socket;

	/* Initialize the new socket as unbound */
	socket->refcount = 0;
	socket->fcb = fcb; 
	socket->type = SOCKET_UNBOUND;
	socket->port = port;

	return fid;
}

int sys_Listen(Fid_t sock)
{
	FCB *fcb = get_fcb(sock);

	/* the file id is not legal */
	if(fcb == NULL)
		return -1;

	socket_cb *socket = (socket_cb *)fcb->streamobj;
	
	if(socket == NULL)
		return -1;

	/* the socket is not bound to a port */
	if(socket->port == NOPORT)	
		return -1;

	/* the port bound to the socket is occupied by another listener */
	if(PORT_MAP[socket->port] != NULL)
		return -1;

	/* the socket has already been initialized */
	if(socket->type != SOCKET_UNBOUND)
		return -1;


	PORT_MAP[socket->port] = socket;
	socket->type = SOCKET_LISTENER;

	socket->listener_s = (listener_socket *)xmalloc(sizeof(listener_socket));

	rlnode_init(&socket->listener_s->queue, NULL);
	socket->listener_s->req_available = COND_INIT;

	return 0;
}


Fid_t sys_Accept(Fid_t lsock)
{
	FCB *fcb = get_fcb(lsock);

	/* the file id is not legal */
	if(fcb == NULL)
		return NOFILE;

	socket_cb *socket = (socket_cb *)fcb->streamobj;

	if(socket == NULL)
		return NOFILE;
	
	/* the file id is not initialized by @c Listen() */
	if(socket->type != SOCKET_LISTENER)
		return NOFILE;

	socket->refcount++;
	while(is_rlist_empty(&socket->listener_s->queue) && PORT_MAP[socket->port] != NULL)
		kernel_wait(&socket->listener_s->req_available, SCHED_PIPE);

	/* check if while waiting, the listening socket lsock closed */
	if(PORT_MAP[socket->port] == NULL){
		socket->refcount--;
		return NOFILE;
	}

	rlnode *req = rlist_pop_front(&socket->listener_s->queue);

	req->request->admitted = 1;
	
	/* Try to construct a new peer */
	Fid_t server_peer = sys_Socket(NOPORT);
	
	if(server_peer == NOFILE){
		socket->refcount--;
		req->request->admitted = 0;
		kernel_signal(&req->request->connected_cv);
		return NOFILE;
	}

	FCB *server_fcb = get_fcb(server_peer);
	
	socket_cb *server_socket_cb = (socket_cb *)server_fcb->streamobj;
	socket_cb *client_socket_cb = req->request->peer;

	/* Establish a peer to peer connection */

	/* Create two pipe_cb's for the communication 
	between server and client */

	pipe_cb *pipe_cb1 = (pipe_cb *)xmalloc(sizeof(pipe_cb));
	pipe_cb *pipe_cb2 = (pipe_cb *)xmalloc(sizeof(pipe_cb));


	server_socket_cb->peer_s = (peer_socket *)xmalloc(sizeof(peer_socket));
	client_socket_cb->peer_s = (peer_socket *)xmalloc(sizeof(peer_socket));

	/* Set the two socket_cb's as peers */
	server_socket_cb->type = SOCKET_PEER;
	client_socket_cb->type = SOCKET_PEER;
	
	/* Server's peer_socket */
	server_socket_cb->peer_s->peer = client_socket_cb;
	server_socket_cb->peer_s->write_pipe = pipe_cb1;
	server_socket_cb->peer_s->read_pipe = pipe_cb2;

	/* Client's peer_socket */
	client_socket_cb->peer_s->peer = server_socket_cb;
	client_socket_cb->peer_s->write_pipe = pipe_cb2;
	client_socket_cb->peer_s->read_pipe = pipe_cb1;

	/* Initialize pipe_cb1 */
	pipe_cb1->reader = client_socket_cb->fcb;
	pipe_cb1->writer = server_fcb;
	pipe_cb1->has_space = pipe_cb1->has_data = COND_INIT;
	pipe_cb1->w_position = pipe_cb1->r_position = 0;

	/* Initialize pipe_cb2 */
	pipe_cb2->reader = server_fcb;
	pipe_cb2->writer = client_socket_cb->fcb;
	pipe_cb2->has_space = pipe_cb2->has_data = COND_INIT;
	pipe_cb2->w_position = pipe_cb2->r_position = 0;

	kernel_signal(&req->request->connected_cv);
	socket->refcount--;

	return server_peer;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	if(port <= NOPORT || port > MAX_PORT)
		return -1;
	
	/* No socket bound on that port */
	if(PORT_MAP[port] == NULL)
		return -1;

	/* Check if the port we want to connect has a listener socket */
	if(PORT_MAP[port]->type != SOCKET_LISTENER)
		return -1;

	FCB *fcb = get_fcb(sock);
	if(fcb == NULL)
		return -1;

	socket_cb *client = (socket_cb *)fcb->streamobj;
	
	if(client == NULL)
		return -1;

	if(client->type != SOCKET_UNBOUND)
		return -1;

	client->refcount++;

	connection_request *request = (connection_request *)xmalloc(sizeof(connection_request));
	
	/* Create a new request and push it back to the listener's request queue */
	request->admitted = 0;
	request->peer = client;
	request->connected_cv = COND_INIT;
	rlnode_init(&request->queue_node, request);
	rlist_push_back(&PORT_MAP[port]->listener_s->queue, &request->queue_node);

	kernel_signal(&PORT_MAP[port]->listener_s->req_available);
	kernel_timedwait(&request->connected_cv, SCHED_PIPE, timeout);

	client->refcount--;

	rlist_remove(&request->queue_node);
	int retval = request->admitted ? 0 : -1;
	
	free(request);
	return retval;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	FCB *fcb = get_fcb(sock);

	if(fcb == NULL)
		return -1;

	socket_cb *socket = (socket_cb *)fcb->streamobj;

	if(socket == NULL)
		return -1;

	/* No connection established */
	if(socket->type != SOCKET_PEER)
		return -1;

	int retval;
	switch(how){
		case SHUTDOWN_READ:
			retval = pipe_reader_close(socket->peer_s->read_pipe);
			break;
		case SHUTDOWN_WRITE:
			retval = pipe_writer_close(socket->peer_s->write_pipe);
			break;
		case SHUTDOWN_BOTH:
			retval = socket_close(socket);
			break;
		default:
			return -1;
	}
	
	return retval;
}

