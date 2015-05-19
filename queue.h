#ifndef QUEUE_H
#define QUEUE_H


typedef struct QueueNode {
  void *data;
  struct QueueNode *next;
} QueueNode;

typedef struct {
  QueueNode *front;
  QueueNode *rear;
  long length;
} Queue;


int queue_init(Queue *queue); 

long queue_length(const Queue *queue);

int queue_enqueue(Queue *queue, const void *data); 

int queue_dequeue(Queue *queue, void **data); 
 
#endif

