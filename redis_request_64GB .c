#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <math.h>
#include "hiredis.h"
#include "MT.h"

#define SET_VALUE (10 * 32 * 1024)
#define VALUE_SIZE (32 * 1024)
#define MEMHOG_SIZE (6 * 1024 * 1024)

static unsigned long count = 0;
static int bFinish = 0;
static pthread_mutex_t redis_lock;

double Uniform() {
	return genrand_real3();
}

double rand_normal(double mu, double sigma)
{
	double z = sqrt(-2.0*log(Uniform())) * sin(2.0*M_PI*Uniform());
	return mu + sigma*z;
}

void geneRandString(char *str, int size)
{
	int i;
	for (i = 0; i < size - 1; i++)
		str[i] = 'a' + genrand_int32() % 26;
	str[i] = '\0';
}

int checkReqCmdErr(redisContext *c, redisReply *rep)
{
	if (rep == NULL) {
                printf("rep == NULL\n");
                redisFree(c);
                return -1;
        }
        if (rep->type == REDIS_REPLY_ERROR) {
                printf("rep err\n");
                freeReplyObject(rep);
                redisFree(c);
                return -1;
        }
	freeReplyObject(rep);
	return 0;
}

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

void *pthread_redis_save(void *arg)
{
	redisContext *c ;//= redisConnect("127.0.0.1", 6379);
        redisReply *rep = NULL;
	long long stime;
	int i, j, t;

	c = (redisContext *)arg;

	sleep(5);
	printf("bgsave\n");
	pthread_mutex_lock(&redis_lock);
	rep = redisCommand(c, "BGSAVE");
	pthread_mutex_unlock(&redis_lock);
	if (checkReqCmdErr(c, rep))
		return NULL;
}

int redis_thread_make(pthread_t *pth, void *(*th_func)(void *), redisContext *c, redisReply *rep)
{
	if (pthread_create(pth, NULL, th_func, c)) {
                printf("pthread_create fail\n");
                rep = redisCommand(c, "flushall");
                if (rep == NULL || rep->type == REDIS_REPLY_ERROR)
                        printf("flushall fail\n");
                freeReplyObject(rep);
                redisFree(c);
                return -1;
        }
	//pthread_detach(pth);
	return 0;
}

int getAccessKey(void)
{
	int r;
	int key;
	static int i = 0, key_rocal = 0;

	if (i % 80 == 0) {
		key_rocal = genrand_int32() % SET_VALUE;
	}

	r = genrand_int32() % 100;
	if (r < 95) {
		do {
			key = (int)rand_normal(key_rocal, 8);
		} while (key < 0 || key >= SET_VALUE);
	} else 
		key = genrand_int32() % SET_VALUE;
	if (key < 0) printf("key minus\n");
	i++;
	return key;
}

int main(void)
{
	int i, j, t, k;
	char str[VALUE_SIZE];
	redisContext *c = redisConnect("127.0.0.1", 6379);
	redisReply *rep = NULL;
	pthread_t pth_time, pth_save;
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_ANONYMOUS | MAP_PRIVATE;
	struct timespec start, end;
	unsigned long long acc_time;

	if (c->err) {
		printf("Connect fail: %s\n", c->errstr);
		return -1;
	}
	pthread_mutex_init(&redis_lock, NULL);
	srand((unsigned)time(NULL));
	init_genrand((unsigned)time(NULL));
	
	for (i = 0; i < SET_VALUE; i++) {
		geneRandString(str, VALUE_SIZE - 1);

		pthread_mutex_lock(&redis_lock);
		rep = redisCommand(c, "SET %d %s", i, str);
		pthread_mutex_unlock(&redis_lock);
		if (checkReqCmdErr(c, rep))
			return -1;
	}
	
	if (redis_thread_make(&pth_time, pthread_time_calc, c, rep))
		return -1;
	pthread_detach(pth_time);
	//if (redis_thread_make(&pth_save, pthread_redis_save, c, rep))
	//	return -1;
	t = 0;
	k = 0;
	clock_gettime(CLOCK_REALTIME, &start);
	for (i = 0; i < SET_VALUE*1; i++) {
		int key = getAccessKey();
		geneRandString(str, VALUE_SIZE - 1);

		pthread_mutex_lock(&redis_lock);
		rep = redisCommand(c, "SET %d %s", key, str);
		pthread_mutex_unlock(&redis_lock);
		if (checkReqCmdErr(c, rep))
			return -1;
		if (k == 30000) {
		        pthread_mutex_lock(&redis_lock);
        		rep = redisCommand(c, "BGSAVE");
        		pthread_mutex_unlock(&redis_lock);
        		if (checkReqCmdErr(c, rep))
        		        return 0;
		}

/*		if (k > 10000 && (t % 100) == 0) {
			pthread_mutex_lock(&redis_lock);
			rep = redisCommand(c, "DEL %d", key);
			pthread_mutex_unlock(&redis_lock);
			if (checkReqCmdErr(c, rep))
				return -1;
		}
*/
		t++;
		k++;
		count++;
	}
	clock_gettime(CLOCK_REALTIME, &end);

	bFinish = 1;	
	acc_time += (end.tv_sec - start.tv_sec) * 1000000000
			+ (end.tv_nsec - start.tv_nsec);
	printf("%lld\n", acc_time);

	pthread_join(pth_save, NULL);
	sleep(10);
	rep = redisCommand(c, "flushall");
	if (rep == NULL || rep->type == REDIS_REPLY_ERROR)
		printf("flushall fail\n");
	rep = redisCommand(c, "bgsave");
	if (rep == NULL || rep->type == REDIS_REPLY_ERROR)
		printf("last bgsave fail\n");
	freeReplyObject(rep);
	redisFree(c);

	return 0;
}

