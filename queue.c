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
  if (NULL == queue->rear) { // adding to an empty queue...
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

int queue_add_front(Queue *queue, const void *data) {
  if (NULL == queue->rear) { // adding to an empty queue...
    queue->rear = (QueueNode *) malloc(sizeof(QueueNode));
    queue->rear->next = NULL;
    queue->rear->data = (void *) data;
    queue->front = queue->rear;
  } else {
    QueueNode *newNode = (QueueNode *) malloc(sizeof(QueueNode));
    newNode->data = (void *) data;
    newNode->next = queue->front;
    queue->front = newNode;
  }
  queue->length++; 
  return EXIT_SUCCESS;
}

int queue_dequeue(Queue *queue, void **data) {
  QueueNode *new_front = queue->front;
  if (NULL == new_front) { // cannot dequeue from an empty queue
    return EXIT_FAILURE;
  }
  if (NULL != new_front->next) {
    new_front = new_front->next;
    *data = queue->front->data; 
    free(queue->front);
    queue->front = new_front;
  } else { // dequeue the last element
    *data = queue->front->data; 
    free(queue->front);
    queue->front = NULL;
    queue->rear = NULL;
  }
  queue->length--; 
  return EXIT_SUCCESS;
}

int queue_remove_index(Queue *queue, const long index, void **data) {
  if (NULL == queue->front) { // cannot remove from an empty queue
    return EXIT_FAILURE;
  } else if (index == 0) { // remove front element
    return queue_dequeue(queue, data);
  } else { // remove middle element
    long current_index = 0;
    for (QueueNode *iterator = queue->front; iterator != NULL; iterator = iterator->next) {
      if (current_index == index - 1 && iterator->next != NULL) {
        QueueNode *node_to_remove = iterator->next;
        *data = node_to_remove->data;
        // splice out node_to_remove...
        iterator->next = node_to_remove->next;
        if (NULL == iterator->next) { // update rear pointer
          queue->rear = iterator;
        }
        free(node_to_remove);
        queue->length--;
        return EXIT_SUCCESS;
      } else {
        current_index++;
      }
    }
  }
  return EXIT_FAILURE; // index not found
}

void queue_for_each(const Queue *queue, const QueueMapping f, void *f_args) {
  for (QueueNode *iterator = queue->front; iterator != NULL; iterator = iterator->next) {
    f(iterator->data, f_args);
  }
}

long queue_first_index_of(const Queue *queue, const void *what, const QueueEqualPredicate equal_predicate) {
  long index = 0;
  for (QueueNode *iterator = queue->front; iterator != NULL; iterator = iterator->next) {
    if (equal_predicate == NULL && what == iterator->data) {
      return index; // 'what' found at index via pointer equality
    } else if (equal_predicate != NULL && equal_predicate(what, iterator->data)) {
      return index; // 'what' found at index via equal predicate
    } else {
      index++; // not found yet, continue search
    }
  }
  return -1; // no 'what' found in queue
}


// test code...
#ifdef QUEUE_TEST
#include <stdio.h>
#include <string.h>

void test_print_element(void *element, void *args) {
  printf("%s\n", (char *) element); 
}

int test_string_equal(const void *a, const void *b) {
  return (0 == strcmp((char *) a, (char *) b));
}

int main() {
  printf("Welcome to the Queue test program!\n");
  char *element;
  Queue q;
  queue_init(&q);
  printf("Initialiazed q. Current length: %ld\n", queue_length(&q));
  queue_enqueue(&q, "First Element");
  queue_enqueue(&q, "Second Element");
  queue_enqueue(&q, "Third Element");
  printf("Added 3 strings. Current length: %ld\n", queue_length(&q));
  queue_add_front(&q, "Zeroth Element");
  printf("Added 1 string to the front. Current length: %ld\n", queue_length(&q));
  printf("Printing q via queue_for_each:\n");
  queue_for_each(&q, &test_print_element, NULL);
  printf("First index of 'Zeroth Element' via pointer equality: %ld\n",
         queue_first_index_of(&q, "Zeroth Element", NULL));
  printf("First index of 'First Element' via pointer equality: %ld\n",
         queue_first_index_of(&q, "First Element", NULL));
  printf("First index of 'Second Element' via pointer equality: %ld\n",
         queue_first_index_of(&q, "Second Element", NULL));
  printf("First index of 'Third Element' via pointer equality: %ld\n",
         queue_first_index_of(&q, "Third Element", NULL));
  printf("First index of 'No Element' via pointer equality: %ld\n",
         queue_first_index_of(&q, "No Element", NULL));
  printf("First index of 'Zeroth Element' via test_string_equal: %ld\n",
         queue_first_index_of(&q, "Zeroth Element", &test_string_equal));
  printf("First index of 'First Element' via test_string_equal: %ld\n",
         queue_first_index_of(&q, "First Element", &test_string_equal));
  printf("First index of 'Second Element' via test_string_equal: %ld\n",
         queue_first_index_of(&q, "Second Element", &test_string_equal));
  printf("First index of 'Third Element' via test_string_equal: %ld\n",
         queue_first_index_of(&q, "Third Element", &test_string_equal));
  printf("First index of 'No Element' via test_string_equal: %ld\n",
         queue_first_index_of(&q, "No Element", &test_string_equal));
  printf("Removing index 2 from q:\n");
  queue_remove_index(&q, 2, (void **) &element);
  queue_for_each(&q, &test_print_element, NULL);
  printf("Removing index 0 from q:\n");
  queue_remove_index(&q, 0, (void **) &element);
  queue_for_each(&q, &test_print_element, NULL);
  printf("Removing index 1 from q:\n");
  queue_remove_index(&q, 1, (void **) &element);
  queue_for_each(&q, &test_print_element, NULL);
  printf("Current length: %ld\n", queue_length(&q));
  printf("Adding two new elements to q:\n");
  queue_enqueue(&q, "First New Element");
  queue_enqueue(&q, "Second New Element");
  queue_for_each(&q, &test_print_element, NULL);
  printf("Dequeuing q until it is empty...\n");
  while (queue_length(&q) > 0) {
    queue_dequeue(&q, (void **) &element);
    printf("...dequeued element: %s\n", element);
  }
  printf("Current length: %ld\n", queue_length(&q));
  printf("All done, exiting.\n");
 
}
#endif

