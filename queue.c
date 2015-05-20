#include "queue.h"
#include <stdlib.h>
 

int queue_init(Queue *queue) {
  queue->front = NULL;
  queue->rear = NULL;
  queue->length = 0;
  return EXIT_SUCCESS;
}

long queue_length(const Queue *queue) {
  return queue->length;
}

int queue_enqueue(Queue *queue, const void *data) {
  if (NULL == queue->rear) {
    queue->rear = (QueueNode *) malloc(sizeof(QueueNode));
    queue->rear->next = NULL;
    queue->rear->data = (void *) data;
    queue->front = queue->rear;
  } else {
    QueueNode *newNode = (QueueNode *) malloc(sizeof(QueueNode));
    newNode->data = (void *) data;
    newNode->next = NULL;
    queue->rear->next = newNode;
    queue->rear = newNode;
  }
  queue->length++; 
  return EXIT_SUCCESS;
}

int queue_dequeue(Queue *queue, void **data) {
  QueueNode *newFront = queue->front;
  if (NULL == newFront) { // cannot dequeue from an empty queue
    return EXIT_FAILURE;
  }
  if (NULL != newFront->next) {
    newFront = newFront->next;
    *data = queue->front->data; 
    free(queue->front);
    queue->front = newFront;
  } else { // dequeue the last element
    *data = queue->front->data; 
    free(queue->front);
    queue->front = NULL;
    queue->rear = NULL;
  }
  queue->length--; 
  return EXIT_SUCCESS;
}

void queue_for_each(const Queue *queue, const QueueIterator f, void *f_args) {
  for (QueueNode *iterator = queue->front; iterator != NULL; iterator = iterator->next) {
    f(iterator->data, f_args);
  }
}
 

// test code...
#ifdef QUEUE_TEST
#include <stdio.h>

void test_print_element(void *element, void *args) {
  printf("%s\n", (char *) element); 
}

int main() {
  printf("Welcome to the Queue test program!\n");
  Queue q;
  queue_init(&q);
  printf("Initialiazed q. Current length: %ld\n", queue_length(&q));
  queue_enqueue(&q, "First Element");
  queue_enqueue(&q, "Second Element");
  queue_enqueue(&q, "Third Element");
  printf("Added 3 strings. Current length: %ld\n", queue_length(&q));
  printf("Printing q via queue_for_each:\n");
  queue_for_each(&q, &test_print_element, NULL);
  char *element;
  printf("Dequeuing q until it is empty...\n");
  while (queue_length(&q) > 0) {
    queue_dequeue(&q, (void **) &element);
    printf("  ...got element: %s\n", element);
  }
  printf("Current length: %ld\n", queue_length(&q));
  printf("All done, exiting.\n");
 
}
#endif

