#include <stddef.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <linux/sched.h>
#include <stdio.h>
#include <unistd.h>

// for queue integration
typedef struct s_my_node {
    struct task_struct* data;
    struct s_my_node* next_node;
} my_node;

typedef struct queue {
	my_node* root;
	int count;
} queue;

typedef struct cs1550_sem{
	int value; // the number of available resources // negative value == num of things waiting to use that resource
	struct mutex *lock;
	my_node* root;
	queue the_queue;
} cs1550_sem;

void seminit(cs1550_sem *sem, int value) {
    syscall(441, sem, value);
}

void down(cs1550_sem *sem) {
    syscall(442, sem);
}

void up(cs1550_sem *sem) {
    syscall(443, sem);
}

#define BUFFER_SIZE 16
#define N BUFFER_SIZE

typedef struct lane{

    int buffer[BUFFER_SIZE];
    int counter;
    int in;
    int out;
    cs1550_sem mutex;
    cs1550_sem empty;
    cs1550_sem full;

} lane;

typedef struct shared_mem{

    lane north;
    lane south;
    int total;
    struct timespec start_time;

} shared_mem;

// to keep track of lane states
typedef enum wake_e{
    AWAKE,
    ASLEEP,
    NORTH,
    SOUTH
} wake;

void flagperson(shared_mem* sm){

    // honking is the alert to wake up
    // each thread is its own queue
    // signal is the honk -> wake up and consume as long
    // as there are not 8 cars in the other queue

    // starts out asleep
    wake state = ASLEEP;

    while(1){

        if( state == ASLEEP ){

            if( sm->north.counter >= 1 ){

                // wakes up
                int car = sm->north.buffer[sm->north.out];
                state = NORTH;
                printf("The flagperson is now awake.\n");

		// to keep track of time
                struct timespec end_time;
                int end = clock_gettime(CLOCK_MONOTONIC, &end_time);
                int time_secs = end_time.tv_sec - sm->start_time.tv_sec;

                printf("Car %d coming from the %c direction, blew their horn at time %d\n", car, 'N', time_secs);

            }else if( sm->south.counter >= 1 ){

		 // car honks, wakes up
                int car = sm->south.buffer[sm->south.out];
                state = SOUTH;
                printf("The flagperson is now awake.\n");

		// to keep track of time
                struct timespec end_time;
                int end = clock_gettime(CLOCK_MONOTONIC, &end_time);
                int time_secs = end_time.tv_sec - sm->start_time.tv_sec;

                printf("Car %d coming from the %c direction, blew their horn at time %d\n", car, 'S', time_secs);

       	}

        }else if( state == NORTH ){

		// each car takes one second to go through construction area
            sleep(1);

            int car = sm->north.buffer[sm->north.out];

	    // to keep track of time
            struct timespec end_time;
            int end = clock_gettime(CLOCK_MONOTONIC, &end_time);
            int time_secs = end_time.tv_sec - sm->start_time.tv_sec;

            printf("Car %d coming from the %c direction left the construction zone at time %d\n", car, 'N', time_secs);
            down(&(sm->north.full));
            down(&(sm->north.mutex));
            sm->north.out = (sm->north.out + 1) % BUFFER_SIZE;
            sm->north.counter--;
            up(&(sm->north.mutex));
            up(&(sm->north.empty));

		// switches to south lane if there are 8 or more cars in south lane OR if north lane is empty
            if( (BUFFER_SIZE - sm->south.empty.value) >= 8 || (BUFFER_SIZE - sm->north.empty.value) == 0 ){
                state = SOUTH;
            }

		// switches to asleep if both lanes are empty
            if( (BUFFER_SIZE - sm->south.empty.value) == 0 && (BUFFER_SIZE - sm->north.empty.value) == 0 ){
                state = ASLEEP;
                printf("The flagperson is now asleep.\n");
            }

        }else{

        	// each car takes one second to go through construction area
            sleep(1);

            int car = sm->south.buffer[sm->south.out];

		// logs end time and calculates total
            struct timespec end_time;
            int end = clock_gettime(CLOCK_MONOTONIC, &end_time);
            int time_secs = end_time.tv_sec - sm->start_time.tv_sec;

            printf("Car %d coming from the %c direction left the construction zone at time %d\n", car, 'S', time_secs);
            down(&(sm->south.full));
            down(&(sm->south.mutex));
            sm->south.out = (sm->south.out + 1) % BUFFER_SIZE;
            sm->south.counter--;
            up(&(sm->south.mutex));
            up(&(sm->south.empty));

		// switches to south lane if there are 8 or more cars in south lane OR if north lane is empty
            if( (BUFFER_SIZE - sm->north.empty.value) >= 8 || (BUFFER_SIZE - sm->south.empty.value) == 0 ){
                state = NORTH;
            }

		// switches to asleep if both lanes are empty
            if( (BUFFER_SIZE - sm->south.empty.value) == 0 && (BUFFER_SIZE - sm->north.empty.value) == 0 ){
                state = ASLEEP;
                printf("The flagperson is now asleep.\n");
            }

        }


    }

}

void producer(shared_mem* sm, lane* l){

    while(1){

        int r;

        do{

            down(&(l->empty)); // empty must be done first to prevent deadlock! know there's at least one empty spot
            down(&(l->mutex)); // entering crit region
            l->buffer[l->in] = sm->total++;
            l->in = (l->in + 1) % BUFFER_SIZE;
            l->counter++;
            up(&(l->mutex));
            up(&(l->full));
            r = rand();
            r = r % 100;

        }while(r < 75);

        // first fleet of cars is sent and then it waits
        sleep(8);

    }


}

int main()
{
    // for rand()
    srand(time(NULL));

    // allocates memory for use, a min amount of a page
    shared_mem *ptr = (shared_mem*) mmap(NULL, sizeof(shared_mem), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);

    // initializes shared_mem ptr for use
    seminit(&(ptr->north.full), 0);
    seminit(&(ptr->south.full), 0);
    seminit(&(ptr->north.empty), BUFFER_SIZE);
    seminit(&(ptr->south.empty), BUFFER_SIZE);
    seminit(&(ptr->north.mutex), 1);
    seminit(&(ptr->south.mutex), 1);

    ptr->north.counter = 0;
    ptr->south.counter = 0;
    ptr->north.in = 0;
    ptr->south.in = 0;
    ptr->north.out = 0;
    ptr->south.out = 0;
    ptr->total = 0;

    pid_t pid = fork();

    // keeps track of starting time for later calculations
    int start = clock_gettime(CLOCK_MONOTONIC, &(ptr->start_time));

    // child
    if(pid == 0) {
        flagperson(ptr);
    }

    else if(pid > 0) {

        pid = fork();
        // producer north or south
        if(pid == 0) {
            producer(ptr, &(ptr->north));
        }
        // the opposing producer
        else if(pid > 0) {
            producer(ptr, &(ptr->south));
        }
        else {
            perror("Unable to create child process.");
            exit(EXIT_FAILURE);
        }
    }
    else {
        perror("Unable to create child process.");
        exit(EXIT_FAILURE);
    }

	return 0;
}

