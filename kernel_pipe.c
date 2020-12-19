
#include "tinyos.h"
#include "util.h"
#include "kernel_streams.h"
#include "kernel_pipe.h"
#include "kernel_cc.h"


int error_read(void *streamobj, char *buf, unsigned int size){
	return -1;
}

int error_write(void *streamobj, const char *buf, unsigned int size){
	return -1;
}

void *error_open(uint minor){
	return NULL;	
}	

int pipe_read(void *pipecb_t, char *buf, unsigned int n){
	
	pipe_cb *pipe = (pipe_cb *)pipecb_t;
	if(pipe == NULL)
		return -1;

	//make sure the read end is not closed;
	if(pipe->reader == NULL)
		return -1;

	if(buf == NULL)
		return -1;

	/* When the write end is closed and there are 
	no data to read calls to read return 0*/
	if(pipe->writer == NULL && pipe->w_position == pipe->r_position)
		return 0;

	int i;
	for(i = 0; i < n; i++){
		
		pipe->r_position = pipe->r_position % PIPE_BUFFER_SIZE;

		/* Block until more data are written in the pipe
		and make sure write end is not closed */
		while(pipe->r_position == pipe->w_position && pipe->writer != NULL){
			kernel_broadcast(&pipe->has_space); //wake up the writer
			kernel_wait(&pipe->has_data, SCHED_PIPE);	//block reader
		}
		
		/* Case where write end is closed and there are no data to read */
		if(pipe->writer == NULL && pipe->w_position == pipe->r_position)
			return i;
		
		buf[i] = pipe->BUFFER[pipe->r_position];
		pipe->r_position++;

	}

	kernel_broadcast(&pipe->has_space);
	return i;
}

int pipe_write(void *pipecb_t, const char *buf, unsigned int n){
	
	pipe_cb *pipe = (pipe_cb *)pipecb_t;
	if(pipe == NULL)
		return -1;

	if(buf == NULL)
		return -1;

	/* If the read end is closed writing in the pipe is illegal */
	/* Also writing when the write end is closed is illegal */
	if(pipe->reader == NULL || pipe->writer == NULL)
		return -1;

	int i;
	for(i = 0; i < n; i++){
		
		pipe->w_position = pipe->w_position % PIPE_BUFFER_SIZE;

		while((pipe->r_position - pipe->w_position == 1 || (pipe->w_position - pipe->r_position == PIPE_BUFFER_SIZE-1)) && pipe->reader != NULL){
			kernel_broadcast(&pipe->has_data); //wake up the reader
			kernel_wait(&pipe->has_space, SCHED_PIPE);	//block writer
		}
		
		/* If the read end is closed calls
		on Write() to it return error */
		if(pipe->reader == NULL)
			return -1;
		
		/* If the write end is closed return the number of 
		data that have been written until that point */
		if(pipe->writer == NULL)
			return i;

		pipe->BUFFER[pipe->w_position] = buf[i];
		pipe->w_position++;

	}

	kernel_broadcast(&pipe->has_data); 
	return i;
}

int pipe_reader_close(void *_pipecb){
	
	pipe_cb *pipe = (pipe_cb *)_pipecb;
	if(pipe == NULL)
		return -1;

	pipe->reader = NULL;
	
	if(pipe->writer == NULL)	//check if the write end is also closed to free the pipe_cb
		free(pipe);
	
	kernel_broadcast(&pipe->has_space); //wake up writer
	
	return 0;
}

int pipe_writer_close(void *_pipecb){
	
	pipe_cb *pipe = (pipe_cb *)_pipecb;
	if(pipe == NULL)
		return -1;

	pipe->writer = NULL;

	if(pipe->reader == NULL)	//check if the read end is also closed to free the pipe_cb
		free(pipe);
	
	kernel_broadcast(&pipe->has_data); //wake up reader

	return 0;
}

static file_ops reader_file_ops = {
  .Open = error_open,
  .Read = pipe_read,
  .Write = error_write,
  .Close = pipe_reader_close
};

static file_ops writer_file_ops = {
  .Open = error_open,
  .Read = error_read,
  .Write = pipe_write,
  .Close = pipe_writer_close
};

int sys_Pipe(pipe_t *pipe)
{
	if(pipe == NULL)
		return -1;

	Fid_t fid[2];
	FCB *fcb[2];

	if(FCB_reserve(2, fid, fcb) != 1) 
		return -1;	//did not success

	/* Associate the read and write end with the 2 
	file descriptors we got from FCB_reserve() */
	pipe->read = fid[0];
	pipe->write = fid[1];
	
	pipe_cb *new_pipe = (pipe_cb *)xmalloc(sizeof(pipe_cb));

	/* Initialize pipe content */
	new_pipe->reader = fcb[0];
	new_pipe->writer = fcb[1];
	new_pipe->r_position = new_pipe->w_position = 0;
	new_pipe->has_space = COND_INIT;
	new_pipe->has_data = COND_INIT;

	/* Set the FCB's streamobj and streamfunc appropriately */
	fcb[0]->streamobj = fcb[1]->streamobj = new_pipe;	//both reader and writer point to the same pipe_cb
	
	fcb[0]->streamfunc = &reader_file_ops;
	fcb[1]->streamfunc = &writer_file_ops;

	return 0;
}

