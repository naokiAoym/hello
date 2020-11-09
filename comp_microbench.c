#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>

#define MEMHOG_SIZE (56 * 1024 * 1024)
#define ARRAY_SIZE (28 * 1024 * 1024)
#define FMI (2)
int main(void)
{
    int i;
    int **memhog;
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_ANONYMOUS | MAP_PRIVATE;
	int pArray;
	struct timespec start, end;
	unsigned long long acc_time;

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
		madvise(memhog[i], 4096, MADV_FREE);

    if ((pArray = (int *)malloc(ARRAY_SIZE) == NULL) {
		printf("fail malloc pArray\n");
		return -1;
	}

	clock_gettime(CLOCK_REALTIME, &start);
	for (i = 0; i < ARRAY_SIZE; i += 512 * 1024)
		pArray[i] = i;
	clock_gettime(CLOCK_REALTIME, &end);

	cc_time += (end.tv_sec - start.tv_sec) * 1000000000
			+ (end.tv_nsec - start.tv_nsec);
	printf("%lld\n", acc_time);

	free(pArray);
	for (i = 0; i < MEMHOG_SIZE * 2; i += 2) {
		if (memhog[i][0] != i)
			printf("!memhog[%d] = %d\n", i, memhog[i][0]);
		munmap(memhog[i], 4096);
	}
	free(memhog);
    return 0;
}
