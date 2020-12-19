#ifndef __KERNEL_PIPE_H
#define __KERNEL_PIPE_H

/* Pipes */
#define PIPE_BUFFER_SIZE 4096
typedef struct pipe_control_block {

  FCB *reader, *writer;
  CondVar has_space;
  CondVar has_data;
  int w_position, r_position;
  char BUFFER[PIPE_BUFFER_SIZE];
  
}pipe_cb;

int pipe_read(void *, char *, unsigned int);
int pipe_reader_close(void *);
int pipe_write(void *, const char *, unsigned int);
int pipe_writer_close(void *);

#endif