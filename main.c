#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#define THREAD_POOL_SIZE 8
#define QUEUE_SIZE 100

typedef struct {
	void (*fn)(void* arg);
	void* arg;
} task_t;

typedef struct {
	pthread_mutex_t lock; // global lock that gives access to the threadpool
	pthread_cond_t notify; // conditional that blocks the threads and notify them that a task is available
	pthread_t threads[THREAD_POOL_SIZE]; // array of threads
	task_t task_queue[QUEUE_SIZE]; // array of tasks to give to the threads
	int queued; // max size of task queued
	int queue_front; // front of task queue
	int queue_back; // back of task queue
	int stop; // to stop and free the threadpool
} threadpool_t;

// function executed by every thread inside the pool
void* thread_function(void* threadpool) {
	// cast the pointer to access the pool attributes
	threadpool_t* pool = (threadpool_t*)threadpool;

	// run an infinite loop until the pool is stopped
	while (1) {
		// lock the pool so other threads cannot access the pool while this thread is working on it
		pthread_mutex_lock(&(pool->lock));

		// if the pool is not stopped and there aren't task, just wait
		while (pool->queued == 0 && !pool->stop) {
			// thread is put to sleep and the lock is released
			// the queue can be accessed until notified
			// after the thread is awakened the mutex is locked again
			pthread_cond_wait(&(pool->notify), &(pool->lock));
		}

		// a task entered the queue or the stop signal is sent

		// after waking up the thread checks if it is supposed to stop
		if (pool->stop) {
			// if stopped unlock the mutex and exit the thread
			// because if the mutex is locked we're deadlocking other threads
			pthread_mutex_unlock(&(pool->lock));
			pthread_exit(NULL);
		}

		// the queue has tasks, grab the one at the front
		task_t task = pool->task_queue[pool->queue_front];
		// use the modulo operator to wrap around the queue
		pool->queue_front = (pool->queue_front + 1) % QUEUE_SIZE;
		pool->queued--;

		// free the lock so other thread can access the queue
		pthread_mutex_unlock(&(pool->lock));

		// execute the task with the arguments
		(*(task.fn))(task.arg);
	}

	return NULL;
}

void threadpool_init(threadpool_t* pool) {
	// the threadpool is initialized without tasks
	pool->queued = 0;
	pool->queue_front = 0;
	pool->queue_back = 0;
	pool->stop = 0;

	// the global mutex that give a static number of threads access is initialized
	pthread_mutex_init(&(pool->lock), NULL);
	// the conditional variable that notifies a new task is available to the threads
	pthread_cond_init(&(pool->notify), NULL);

	for (int i = 0; i < THREAD_POOL_SIZE; i++) {
		// pool is the argument of the function so the thread can access the queue
		pthread_create(&(pool->threads[i]), NULL, thread_function, pool);
	}
}

// handle graceful shutdown
void threadpool_destroy(threadpool_t* pool) {
	// lock the pool
	pthread_mutex_lock(&(pool->lock));
	// set stop signal and notify all threads
	pool->stop = 1;
	pthread_cond_broadcast(&(pool->notify));
	// unlock the pool, threads get the stop signal
	pthread_mutex_unlock(&(pool->lock));

	// join every thread to wait for the execution to finish
	for (int i = 0; i < THREAD_POOL_SIZE; i++) {
		pthread_join(pool->threads[i], NULL);
	}

	// destroy mutex and conditional signal
	pthread_mutex_destroy(&(pool->lock));
	pthread_cond_destroy(&(pool->notify));
}

void threadpool_add_task(threadpool_t* pool, void (*function)(void*), void* arg) {
	pthread_mutex_lock(&(pool->lock));

	// populate the back of the queue with the task (function to execute and argument)
	int next_rear = (pool->queue_back + 1) % QUEUE_SIZE;
	if (pool->queued < QUEUE_SIZE) {
		pool->task_queue[pool->queue_back].fn = function;
		pool->task_queue[pool->queue_back].arg = arg;
		pool->queue_back = next_rear;
		pool->queued++;
		pthread_cond_signal(&(pool->notify));
	} else {
		printf("Task queue is full! Cannot add more tasks.\n");
	}

	pthread_mutex_unlock(&(pool->lock));
}

void example_task(void* arg) {
	int* num = (int*)arg;
	printf("Processing task %d\n", *num);
	sleep(1);
	free(arg);
}

int main() {
	threadpool_t pool;
	threadpool_init(&pool);

	for (int i = 0; i < 100; i++) {
		int* task_num = malloc(sizeof(int));
		*task_num = i;
		threadpool_add_task(&pool, example_task, task_num);
		// we cannot free the memory of malloc because the thread has to use it
		// but can be freed inside the task
	}

	sleep(20);

	threadpool_destroy(&pool);

	return 0;
}
