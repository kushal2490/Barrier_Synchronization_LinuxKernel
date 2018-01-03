#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/types.h>


#define BARRIER_INIT		359
#define BARRIER_WAIT		360
#define BARRIER_DESTROY		361
#define BARRIER_RESET		362

#define barrier_init(x, y, z) syscall(BARRIER_INIT, x, y, z);
#define barrier_wait(x) syscall(BARRIER_WAIT, x);
#define barrier_destroy(x) syscall(BARRIER_DESTROY, x);
#define barrier_reset(x) syscall(BARRIER_RESET, x);


#define THREAD_SET1			5
#define THREAD_SET2			20

typedef struct barrier_data{
	int barrier_id;
	int BROKEN;
	int count;
}bdata_t;

typedef struct thread_data{
	int 	 threadId;
	bdata_t	*my_barrier;
	int 	 max_count;
	pthread_cond_t *cond;
	pthread_mutex_t *mutex;
}thread_data_t;

int AVG_SLEEP_TIME;

void* thread_func(void *arg)
{
	int i;
	int ret;
	thread_data_t *tlocaldata = (thread_data_t *)arg;
	int sleep_time;

	//Number of rounds of synchronization
	for(i=0;i<100;i++)
	{
		sleep_time = rand() % 5 + AVG_SLEEP_TIME;
		
		/* Call SYS_BARRIER_WAIT only if BARRIER is not BROKEN i.e. TIMEOUT has not happened */
		if(tlocaldata->my_barrier->BROKEN==0)
		{
			printf("[BARRIER:%d]\tThread:%d\tentering barrier wait\n", tlocaldata->my_barrier->barrier_id, tlocaldata->threadId);
			ret = barrier_wait(tlocaldata->my_barrier->barrier_id);

			/* If TIMEOUT has occurred (BARRIER has broken), set barrier's BROKEN flag to 1  */
			if(ret<0)
			{
				tlocaldata->my_barrier->BROKEN = 1;
			}
		}

		pthread_mutex_lock(tlocaldata->mutex);
			/* Increment barrier count for this round of synchronization */
			tlocaldata->my_barrier->count+=1;		
			
			/*-----WAIT FOR ALL THREADS WORKING WITH THIS BARRIER TO FINISH THIS ROUND BEFORE STARTING ANOTHER ROUND OF SYNCHRONIZATION----*/
			if (tlocaldata->my_barrier->count < tlocaldata->max_count) 
			{
				printf("\n[BARRIER:%d] \t COUNT = %d thread woken up  ", tlocaldata->my_barrier->barrier_id, tlocaldata->my_barrier->count);
				if(tlocaldata->my_barrier->BROKEN!=0) 
					printf("\t FOUND BARRIER BROKEN. WAITING FOR OTHER THREADS BEFORE STARTING NEXT ROUND \n");

				pthread_cond_wait(tlocaldata->cond, tlocaldata->mutex);
			}
			/*----------------------------------------------------------------------------------------------------------------------------*/
		pthread_mutex_unlock(tlocaldata->mutex);


		/* If this is the last thread in the current round of synchronization, reset the barrier */
		if (tlocaldata->my_barrier->count == tlocaldata->max_count)
		{
			printf("[BARRIER:%d] \t COUNT = %d. \t FINAL THREAD ARRIVES \n", tlocaldata->my_barrier->barrier_id, tlocaldata->my_barrier->count);
			
			/* If the barrier was BROKEN, RESET BARRIER for next round of synchronization */
			if (tlocaldata->my_barrier->BROKEN == 1)
			{
				ret = barrier_reset(tlocaldata->my_barrier->barrier_id);
				if(ret < 0) {
					perror("Barrier Reset failed: ");
				}
				else {
					printf("xxxxxxx [Barrier:%d] WAS BROKEN BECAUSE OF TIMEOUT --> RESET successfully xxxxxxx\n", tlocaldata->my_barrier->barrier_id);
				}
			}

			pthread_mutex_lock(tlocaldata->mutex);
				tlocaldata->my_barrier->count = 0;
				tlocaldata->my_barrier->BROKEN = 0;
				/*----------- BROADCAST TO ALL THREADS WAITING IN CURRENT ROUND OF SYNCHRONIZATION-----------------------------------------*/
				pthread_cond_broadcast(tlocaldata->cond);
				/*-----------------------------------------------------------------------------------------------------------------*/
			pthread_mutex_unlock(tlocaldata->mutex);
			printf("\n---------------------------ALL THREADS OF [BARRIER:%d] LEAVE ROUND %d ---------------------------\n\n", tlocaldata->my_barrier->barrier_id, i);
		}	

		/* Random sleep before starting next round of synchronization */
		usleep(sleep_time);
	}

	return NULL;
}

void child_process(int childId)
{
	int i;
	int ret;
	int id1, id2;
	thread_data_t tdata[THREAD_SET1 + THREAD_SET2];
	pthread_t childthread[THREAD_SET1+THREAD_SET2];
	bdata_t my_barrier1, my_barrier2;
	signed int timeout;	

	pthread_cond_t cond1, cond2;
	pthread_mutex_t mutex1, mutex2;

	pthread_cond_init(&cond1, NULL);
	pthread_cond_init(&cond2, NULL);

	pthread_mutex_init(&mutex1, NULL);
	pthread_mutex_init(&mutex2, NULL);

	timeout=10000;	//timeout for first barrier
	barrier_init(THREAD_SET1, &id1, timeout);
	printf("\n[BARRIER:%d] initialized with timeout=%d in Process:%d \n\n", id1, timeout, getpid());

	timeout=0;		//timeout for second barrier
	barrier_init(THREAD_SET2, &id2, timeout);
	printf("\n[BARRIER:%d] initialized with timeout=%d in Process:%d \n\n", id2, timeout, getpid());

	my_barrier1.barrier_id = id1;
	my_barrier1.BROKEN = 0;
	my_barrier1.count = 0;
	
	my_barrier2.barrier_id = id2;
	my_barrier2.BROKEN = 0;
	my_barrier2.count = 0;

#if 1
	for(i=0;i<(THREAD_SET1 + THREAD_SET2);i++)
	{
		tdata[i].threadId = i;
		// printf("child process:%d entered\n", getpid());
		if(i<THREAD_SET1)
		{
			tdata[i].my_barrier = &my_barrier1;
			tdata[i].cond = &cond1;
			tdata[i].mutex = &mutex1;
			tdata[i].max_count = THREAD_SET1;
			// printf("Process:%d -- Thread:%d created\n", getpid(), tdata[i].threadId);
		}
		else {
			tdata[i].my_barrier = &my_barrier2;
			tdata[i].cond = &cond2;
			tdata[i].mutex = &mutex2;
			tdata[i].max_count = THREAD_SET2;
			// printf("Process:%d -- Thread:%d created\n", getpid(), tdata[i].threadId);
		}

		pthread_create(&childthread[i], NULL, thread_func, &tdata[i]);
	}

	for(i=0;i<(THREAD_SET1 + THREAD_SET2);i++)
	{
		pthread_join(childthread[i], NULL);
	}
#endif
	printf("\n\t\t********* initiating BARRIER DESTROY for process:%d **********\n\n", getpid());
	ret = barrier_destroy(id1);
	if(ret<0) {
		perror("barrier destory failed with error:\n");
	}
	ret = barrier_destroy(id2);
	if(ret<0) {
		perror("barrier destory failed with error:\n");
	}

	exit(0);
}

int main()
{
	int childproc1_pid, childproc2_pid;
	int child_id = 1;
	int pid;
	int retStatus;

	printf("Enter average sleep time value\n");
	scanf("%d", &AVG_SLEEP_TIME);

	childproc1_pid = fork();
	if(childproc1_pid==0){
		printf("\nchild process #%d created\n\n", getpid());
		child_process(child_id);
	}
	else {
		child_id++;
		childproc2_pid = fork();
		if(childproc2_pid==0){
			printf("\nchild process #%d created\n\n", getpid());
			child_process(child_id);
		}
		else {

			printf("\nprocess:%d waiting for exit\n", getpid());
			pid = wait(&retStatus);
			printf("\nprocess:%d finished\n", pid);

			printf("\nprocess:%d waiting for exit\n", getpid());
			pid = wait(&retStatus);
			printf("\nprocess:%d finished\n", pid);
		}
	}
	return 0;
}