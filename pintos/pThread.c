#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t foo_mutex = PTHREAD_MU

typedef struct account {
	pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZED;
	int balance;
	long uuid;
} account_t;

void transfer(account_t *donor, account_t *recipient, float amount) {
	pthread_mutex_lock(&donor -> lock);
	pthread_mutex_lock(&recipient -> lock);
	
	if (donor -> balance < amount) {
		printf("Insufficient funds.\n");
	} else {
		donor -> balance -= amount;:q
     		recipient -> balance += amount;
	}

	pthread_mutex_unlock(&recipient -> lock);
	pthread_mutex_unlock(&donor -> lock);
}
