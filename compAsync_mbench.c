#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>

#define MEMHOG_SIZE (14 * 1024 * 1024)
#define ARRAY_SIZE (28 * 1024)
#define FMI (2)
#define LOOP_MAX 10

static unsigned long count = 0;
static int bFinish = 0;

void *pthread_time_calc(void *arg)
{
	int i;
	unsigned long n = 0;

	while(bFinish == 0) {
		printf("count:%ld\n", n);
		sleep(1);
		n = count, count = 0;
	}
}

int main(void)
{
    int i, j, loop;
	int **memhog;
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_ANONYMOUS | MAP_PRIVATE;
	int **pArray;
	struct timespec start, end;
	unsigned long long acc_time;
	pthread_t pth_time;

	if (pthread_create(&pth_time, NULL, pthread_time_calc, NULL)) {
		printf("fail thread_make")
		return -1;
	}
	pthread_detach(pth_time);

	if ((memhog = (int **)malloc(sizeof(int *) * MEMHOG_SIZE)) == NULL) {
		printf("fail malloc memhog\n");
		return -1;
	}
	for (i = 0; i < MEMHOG_SIZE ; i++) {
		memhog[i] = (int *)mmap(NULL, 4096, prot, flags, -1, 0);
		if (memhog[i] == MAP_FAILED)
			printf("fail memhog[%d] mmap\n", i);
	}
	for (i = 0; i < MEMHOG_SIZE; i++) {
		madvise(memhog[i], 4096, MADV_NOHUGEPAGE);
		memhog[i][0] = i;
		madvise(memhog[i], 4096, MADV_NOHUGEPAGE);
	}
	for (i = 1; i < MEMHOG_SIZE; i += FMI)
		madvise(memhog[i], 4096, MADV_DONTNEED);

    if ((pArray = (int **)malloc(sizeof(int *) * ARRAY_SIZE)) == NULL) {
		printf("fail malloc pArray\n");
		return -1;
	}
    	for (i = 0; i < ARRAY_SIZE; i++) {
		pArray[i] = (int *)mmap(NULL, 2 * 1024 * 1024,
				prot, flags, -1, 0);
		if (pArray[i] == MAP_FAILED)
			printf("fail pArray[%d]\n", i);
		madvise(pArray[i], 2*1024*1024, MADV_NOHUGEPAGE);
//		madvise(pArray[i], 2*1024*1024, MADV_HUGEPAGE);
	}

	clock_gettime(CLOCK_REALTIME, &start);
	for (i = 0; i < ARRAY_SIZE; i++)
		for (j = 0; j < (2 * 1024* 1024) / sizeof(int); j += 4096)
			pArray[i][j] = i+j;
	clock_gettime(CLOCK_REALTIME, &end);

	acc_time += (end.tv_sec - start.tv_sec) * 1000000000
			+ (end.tv_nsec - start.tv_nsec);
	printf("%lld\n", acc_time);

	for (i = 0; i < MEMHOG_SIZE; i++) {
		if (memhog[i][0] != i)
			printf("!memhog[%d] = %d\n", i, memhog[i][0]);
		munmap(memhog[i], 4096);
	}
	free(memhog);

	for (loop = 0; loop < LOOP_MAX; loop++) {
		for (i = 0; i < ARRAY_SIZE; i++)
			for (j = 0; j < (2 * 1024* 1024) / sizeof(int); j += 4096)
				pArray[i][j] = i+j+k;
	}

	for (i = 0; i < ARRAY_SIZE; i++) {
		munmap(pArray[i], 2 * 1024 * 1024);
	}
	free(pArray);

    return 0;
}
