#ifndef INCLUDE_PT_CHANGE_H
#define INCLUDE_PT_CHANGE_H

void change_page_table_for_compaction(unsigned long new_hva,
					unsigned long old_hva);
void foo_compaction(void);
void change_page_table_for_batch_compaction(unsigned long *new_hva,
                        unsigned long *old_hva, int page_num);
void change_page_table_for_batch_thread(unsigned long *new_hva,
                unsigned long *old_hva, int page_num);
unsigned long walk_hva_to_pfn(unsigned long hva);
#define EPT_THREAD_NUM 1
extern struct semaphore *entry_sem;
//extern struct semaphore page_sem;

struct data_exec_time_t {
        unsigned long entire_time;
        unsigned long ept_time;
        unsigned long vmm_time;
        unsigned long ept_vmm_sem_time;
        unsigned long thread_time;
};

#define TIME_HYPERCALL_COMPACTION
#ifdef TIME_HYPERCALL_COMPACTION
#include <linux/time.h>
extern struct data_exec_time_t data_exec_time;
unsigned long calc_exec_time(
		struct timespec64 start, struct timespec64 end)
{
	unsigned long exec_time;
	exec_time = (end.tv_sec - start.tv_sec) * NSEC_PER_SEC
		+ (end.tv_nsec - start.tv_nsec);
	return exec_time;
}

struct time_exec_func2_t {
	unsigned long entire_time;
	unsigned long ept_time;
	unsigned long ept_rmap_time;
	unsigned long ept_entry_time;
	unsigned long vmm_time;
	unsigned long vmm_rmap_time;
	unsigned long vmm_entry_time;
	unsigned long vmm_extra_time;
};
extern struct time_exec_func2_t time_exec_func2;
#endif

#define COMPACTION_THREAD_SUM 1
extern struct semaphore sem_change_PT_and_EPT_entry;
extern struct semaphore sem_page_num;
void change_PTentry_thread2_func(unsigned long *new_hva,
                unsigned long *old_hva, unsigned long page_num);
extern struct semaphore sem_vmm_th1;
extern struct semaphore sem_vmm_th2;

void change_single_PTentry(unsigned long new_hva, unsigned long old_hva);

#endif
