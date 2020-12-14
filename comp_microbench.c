#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>

#define MEMHOG_SIZE (14 * 1024 * 1024)
#define ARRAY_SIZE (28 * 1024)
#define FMI (2)
int main(void)
{
    int i, j;
//	int **memhog;
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_ANONYMOUS | MAP_PRIVATE;
	int **pArray;
	struct timespec start, end;
	unsigned long long acc_time;

    /*if ((memhog = (int **)malloc(sizeof(int *) * MEMHOG_SIZE)) == NULL) {
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
		madvise(memhog[i], 4096, MADV_FREE);
*/
    if ((pArray = (int **)malloc(sizeof(int *) * ARRAY_SIZE)) == NULL) {
		printf("fail malloc pArray\n");
		return -1;
	}
    	for (i = 0; i < ARRAY_SIZE; i++) {
		pArray[i] = (int *)mmap(NULL, 2 * 1024 * 1024,
				prot, flags, -1, 0);
		if (pArray[i] == MAP_FAILED)
			printf("fail pArray[%d]\n", i);
//		madvise(pArray[i], 2*1024*1024, MADV_HUGEPAGE);
	}

	clock_gettime(CLOCK_REALTIME, &start);
	for (i = 0; i < ARRAY_SIZE; i++)
		for (j = 0; j < (2 * 1024* 1024) / sizeof(int); j++)
			pArray[i][j] = i+j;
	clock_gettime(CLOCK_REALTIME, &end);

	acc_time += (end.tv_sec - start.tv_sec) * 1000000000
			+ (end.tv_nsec - start.tv_nsec);
	printf("%lld\n", acc_time);

	for (i = 0; i < ARRAY_SIZE; i++) {
		munmap(pArray[i], 2 * 1024 * 1024);
	}
	free(pArray);
/*	for (i = 0; i < MEMHOG_SIZE; i += FMI) {
		if (memhog[i][0] != i)
			printf("!memhog[%d] = %d\n", i, memhog[i][0]);
		munmap(memhog[i], 4096);
	}
	free(memhog);
*/
    return 0;
}
