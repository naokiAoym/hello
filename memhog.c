#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

#define MEMHOG_SIZE (14 * 1024 * 1024)
#define FMI (2)
int main(void)
{
    int i;
    int **memhog;
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_ANONYMOUS | MAP_PRIVATE;

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

    printf("memhog finish!\n");
    sleep(900);

    return 0;
}
