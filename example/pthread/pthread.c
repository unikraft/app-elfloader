#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

static void *thread_fn(void *argp)
{
	printf("Hello from thread!\n");
	fflush(stdout);
}

int main(int argc, char *argv[])
{
	pthread_t tid;
	int rc;

	printf("Hello world!\n");
	fflush(stdout);
	rc = pthread_create(&tid, NULL, thread_fn, NULL);
	if (rc != 0) {
		printf("Error while creating a thread\n");
		fflush(stdout);
		exit(1);
	}
	fflush(stdout);
}
