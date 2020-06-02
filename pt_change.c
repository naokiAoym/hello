#include <linux/kernel.h>
#include <linux/pt_change.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/mm_inline.h>
#include <linux/rmap.h>
#include <linux/pagemap.h>
#include <linux/migrate.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>

struct semaphore *entry_sem;
EXPORT_SYMBOL(entry_sem);
#ifdef TIME_HYPERCALL_COMPACTION
struct data_exec_time_t data_exec_time;
EXPORT_SYMBOL(data_exec_time);
struct time_exec_func2_t time_exec_func2;
EXPORT_SYMBOL(time_exec_func2);
#endif
//struct semaphore page_sem;
//EXPORT_SYMBOL(page_sem);
//struct semaphore sem_change_PT_and_EPT_entry;
//EXPORT_SYMBOL(sem_change_PT_and_EPT_entry);
struct semaphore sem_page_num;
EXPORT_SYMBOL(sem_page_num);
struct semaphore sem_vmm_th1;
EXPORT_SYMBOL(sem_vmm_th1);
struct semaphore sem_vmm_th2;
EXPORT_SYMBOL(sem_vmm_th2);

static pte_t *walk_page_table(struct mm_struct *, unsigned long, pmd_t **);
static void isolated_lru_page(struct page *);
static bool should_defer_flush(struct mm_struct *mm, enum ttu_flags flags);
static void set_tlb_ubc_flush_pending(struct mm_struct *mm, bool writable);

static void migrate_page_state_naoki(struct page *new_page, struct page *old_page)
{
	struct address_space *mapping;
	void **pslot;

	mapping = page_mapping(old_page);
	if (!mapping) {
		if (new_page->index != old_page->index) {
	                //printk("[vmm] != index\n");
	                old_page->index = new_page->index;
	        }
	        if (new_page->mapping != old_page->mapping) {
			struct address_space *new_mapping;
			new_mapping = page_mapping(new_page);
	                printk("[vmm] != mapping\n");
			printk("[vmm] new_mapping:%p\n", new_mapping);
			/* old_spte !present */
	                old_page->mapping = new_page->mapping;
	        }
	} else {
		printk("[vmm] mapping old_page\n");
		xa_lock_irq(&mapping->i_pages);
		pslot = radix_tree_lookup_slot(&mapping->i_pages,
				page_index(old_page));
		if (new_page->index != old_page->index) {
	                //printk("[vmm] != index\n");
	                old_page->index = new_page->index;
	        }
	        if (new_page->mapping != old_page->mapping) {
	 		printk("[vmm] != mapping\n");
	                old_page->mapping = new_page->mapping;
	        }
		radix_tree_replace_slot(&mapping->i_pages, pslot, old_page);
		xa_unlock(&mapping->i_pages);
	}
	if (PageSwapBacked(new_page)) {
		if (!PageSwapBacked(old_page)) {
			printk("[vmm] !old_page SwapBacked\n");
			SetPageSwapBacked(old_page);
		}
	}
}

void change_page_table_for_compaction(unsigned long new_hva,
					unsigned long old_hva)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *new_vma, *old_vma;
	pmd_t *new_pmd, *old_pmd;
	pte_t *new_pte, *old_pte, _pte;
	struct page *new_page, *old_page;
	unsigned long new_pfn, old_pfn;
	spinlock_t *new_pte_ptl, *old_pte_ptl;
	int rc;
	int page_was_mapped = 0;
	enum ttu_flags mig_flags = 
		TTU_MIGRATION|TTU_IGNORE_MLOCK|TTU_IGNORE_ACCESS;
	int printk_pagecount = 0;
	int b_vma_diff = 0;

//	down_write(&mm->mmap_sem);
	new_pte = walk_page_table(mm, new_hva, &new_pmd);
	old_pte = walk_page_table(mm, old_hva, &old_pmd);
	if (new_pte == NULL)
		printk("[vmm] new_pte == NULL\n");
	if (old_pte == NULL)
		printk("[vmm] old_pte == NULL\n");
	new_pte_ptl = pte_lockptr(mm, new_pmd);
	old_pte_ptl = pte_lockptr(mm, old_pmd);
	_pte = *old_pte;
	//printk("[Walk] new_pte:%lx old_pte:%lx",
	//		new_pte->pte, old_pte->pte);
	/*
	if (!new_pte) {
		printk("!new_pte");
		up_write(&mm->mmap_sem);
		return -1;
	}
	if (!old_pte) {
		printk("!old_pte");
		up_write(&mm->mmap_sem);
		return -1;
	}
	*/
	new_page = pte_page(*new_pte);
	old_page = pte_page(*old_pte);
	new_pfn = page_to_pfn(new_page);
	old_pfn = page_to_pfn(old_page);
	//printk("[Walk] new_pfn:%lx old_pfn:%lx",
	//		new_pfn, old_pfn);

	new_vma = find_vma(mm, new_hva);
	old_vma = find_vma(mm, old_hva);
	if (old_vma == NULL)
		printk("[vmm] old_vma == NULL\n");
	if (new_vma != old_vma)
		printk("[walk] != vma");

	if (!trylock_page(new_page))
                lock_page(new_page);
        if (!trylock_page(old_page))
                lock_page(old_page);
	if (printk_pagecount)
		printk("[walk] new_page ref:%d map:%d old_page ref:%d map:%d\n",
			new_page->_refcount.counter,
			new_page->_mapcount.counter,
			old_page->_refcount.counter,
			old_page->_mapcount.counter);

	if (pte_none(*new_pte)) {
		//printk("[vmm] new_pte_none pte:%lx page:%p vma:%p\n",
		//		new_pte->pte, old_page, new_vma);
		//printk("[vmm] old_vma:%p new_vma:%p\n", old_vma, new_vma);
		if (old_page->mapping != 
				(struct address_space *)(new_vma->anon_vma)) {
			//printk("[vmm] old_page->mapping(%p) != new_vma\n",
			//		old_page->mapping);
			b_vma_diff = 1;
			old_page->mapping = 
				(struct address_space *)(new_vma->anon_vma);
		}
		old_page->index = linear_page_index(new_vma,  new_hva);
	} else
		//migrate_page_states(old_page, new_page);
		migrate_page_state_naoki(new_page, old_page);

/*	if (page_mapped(new_page)) {
		pte_t pteval;
		//try_to_unmap(new_page, TTU_MIGRATION
		//		|TTU_IGNORE_MLOCK|TTU_IGNORE_ACCESS);
		flush_cache_page(new_vma, new_hva, new_pfn);
		if (should_defer_flush(mm, mig_flags)) {
			pteval = ptep_get_and_clear(mm, new_hva, new_pte);
			set_tlb_ubc_flush_pending(mm, pte_dirty(pteval));
		} else
			pteval = ptep_clear_flush(new_vma, new_hva, new_pte);
		if (pte_dirty(pteval)) set_page_dirty(new_page);
		page_was_mapped = 1;
	}
	if (page_mapped(old_page)) {
		pte_t pteval;
		//try_to_unmap(old_page, TTU_MIGRATION
		//	|TTU_IGNORE_MLOCK|TTU_IGNORE_ACCESS);
		if (should_defer_flush(mm, mig_flags)) {
                        pteval = ptep_get_and_clear(mm, old_hva, old_pte);
                        set_tlb_ubc_flush_pending(mm, pte_dirty(pteval));
                } else
                        pteval = ptep_clear_flush(old_vma, old_hva, old_pte);
                if (pte_dirty(pteval)) set_page_dirty(old_page);
		flush_cache_page(old_page, old_hva, old_pfn);
	}
*/
	if (!pte_none(*new_pte)) {
		int bFree_new_page = 1;
		//printk("[walk] new_pte !none");
		if (!page_mapped(new_page)) {
			printk("[walk] !new_page mapped (%lx)", new_pfn);
			bFree_new_page = 0;
		}
		if (!pte_present(*new_pte)) {
			printk("[walk] !new_pte present (%lx)", new_pfn);
			bFree_new_page = 0;
		}
		if (is_zero_pfn(pte_pfn(*new_pte))) {
			printk("[walk] zero_pfn pte:%lx", new_pte->pte);
			bFree_new_page = 0;
		}
		//if (PageCompound(new_page))
		//	printk("[walk] PageCompound");

		if (page_mapcount(new_page) != 1) {
			printk("[walk] page_mapcount != 1 (%lx, %d (ref:%d))",
				new_pfn, new_page->_mapcount.counter,
				new_page->_refcount.counter);
			bFree_new_page = 0;
			//spin_lock(new_pte_ptl);
			//pte_clear(new_vma->vm_mm, new_hva, new_pte);
                        //page_remove_rmap(new_page, false);
                        //spin_unlock(new_pte_ptl);
		}

		if (bFree_new_page) {
			if (!PageAnon(new_page))
				printk("[walk] !new_page anon (%lx)", new_pfn);
			//if (!trylock_page(new_page))
			//	printk("[walk] !trylock new_page (%lx)",
			//					new_pfn);
			spin_lock(new_pte_ptl);
			isolated_lru_page(new_page);
			spin_unlock(new_pte_ptl);
		
			//release_pte_page(new_page);
			//dec_node_page_state(new_page,
			//  NR_ISOLATED_ANON + page_is_file_cache(new_page));
			//unlock_page(new_page);
			lru_cache_add(new_page);
			put_page(new_page);

			spin_lock(new_pte_ptl);
			pte_clear(new_vma->vm_mm, new_hva, new_pte);
			page_remove_rmap(new_page, false);
			spin_unlock(new_pte_ptl);
			atomic_long_dec(&mm->rss_stat.count[MM_ANONPAGES]);
		}
	}
	
	//if (!trylock_page(new_page))
	//	lock_page(new_page);
	spin_lock(old_pte_ptl);
	pte_clear(old_vma->vm_mm, old_hva, old_pte);
	//page_remove_rmap(old_page, false);
	/*if (new_vma != old_vma) {
		printk("[walk] new_vma != old_vma");
		page_remove_rmap(old_page, false);
	//spin_unlock(old_pte_ptl);
	
	//spin_lock(old_pte_ptl);
		page_add_new_anon_rmap(old_page, old_vma, new_hva, true);
	}*/

	set_pte_at(new_vma->vm_mm, new_hva, new_pte, _pte);
	
	if (b_vma_diff) {
		page_remove_rmap(old_page, false);
		page_add_anon_rmap(old_page, new_vma, new_hva, false);
	}

//	if (page_was_mapped) {
//		page_remove_rmap(old_page, false);
//		page_add_anon_rmap(old_page, old_vma, new_hva, false);
//	}

	update_mmu_cache(new_vma, new_hva, new_pte);
	spin_unlock(old_pte_ptl);

	unlock_page(old_page);
        unlock_page(new_page);

	free_page_and_swap_cache(new_page);
	if(printk_pagecount) {
		printk("[walk] new_page ref:%d map:%d",
				new_page->_refcount.counter,
				new_page->_mapcount.counter);
		printk("[walk] old_page ref:%d map:%d",
				old_page->_refcount.counter,
				old_page->_mapcount.counter);
	}

/*	if (!trylock_page(new_page))
		lock_page(new_page);
	if (!trylock_page(old_page))
		lock_page(old_page);
	remove_migration_ptes(new_page, old_page, false);
	unlock_page(old_page);
	unlock_page(new_page);
*/
	//new_pte = walk_page_table(mm, new_hva, &new_pmd);
        //old_pte = walk_page_table(mm, old_hva, &old_pmd);
        //printk("[Walk] new_pte:%lx old_pte:%lx",
        //                new_pte->pte, old_pte->pte);

//	up_write(&mm->mmap_sem);

	return;
}
EXPORT_SYMBOL(change_page_table_for_compaction);

static void get_vmalloc_array(unsigned long **array, int page_num) {
	//static int bInit = 1;
	int bInit = 1;

	if (bInit) {
		*array = (unsigned long *)
				vmalloc(sizeof(unsigned long) * page_num);
		bInit = 0;
	}
	return;
}

#define NUM_CHANGE_PT_THREAD 2
DEFINE_SPINLOCK(spin_th_index);
static int get_thread_index(void)
{
	static int th_index = 0;
	int ret;
	spin_lock(&spin_th_index);
	ret = th_index;
	th_index++;
	if (th_index >= NUM_CHANGE_PT_THREAD) th_index = 0;
	spin_unlock(&spin_th_index);
	return ret;
}

static int bSemDown = 0;
DEFINE_SPINLOCK(spin_free_page);
//DEFINE_MUTEX(mutex_test);
static struct semaphore sem_th;
void change_page_table_for_batch_compaction(unsigned long *new_hva,
			unsigned long *old_hva, int page_num)
{
        struct mm_struct *mm = current->mm;
        struct vm_area_struct *new_vma, *old_vma;
        pmd_t *new_pmd, *old_pmd;
        pte_t *new_pte, *old_pte, _pte, _pte2;
        struct page *new_page, *old_page;
        unsigned long new_pfn, old_pfn;
	struct page *_new_page;
	unsigned long *_new_pfn;
//static unsigned long *_new_pfn = NULL;
        spinlock_t *new_pte_ptl, *old_pte_ptl;
	int i;
	//int ept_array_pivot = page_num / EPT_THREAD_NUM;
	int rc = -EAGAIN;
	int b_vma_diff = 0;
	//int th_index;
#ifdef TIME_HYPERCALL_COMPACTION
	struct timespec64 start, end;
	unsigned long exec_time;

	getnstimeofday64(&start);
#endif

	//__asm__ volatile ("rdtsc" : "=A" (vmm_start_time));
	//printk("[VMM] current->mm:%p\n", current->mm);
//	printk("[VMM] start PTchange\n");
	spin_lock(&spin_free_page);
	get_vmalloc_array(&_new_pfn, page_num);
	if (!_new_pfn) {
		printk("[VMM] !vmalloc _new_pfn\n");
	}
	spin_unlock(&spin_free_page);
	//th_index = get_thread_index();

        //down_write(&mm->mmap_sem);
	for (i = 0; i < page_num; i++) {
//		down(&page_sem);
		//down(&entry_sem);
		new_pte = walk_page_table(mm, new_hva[i], &new_pmd);
        	old_pte = walk_page_table(mm, old_hva[i], &old_pmd);
        	new_pte_ptl = pte_lockptr(mm, new_pmd);
        	old_pte_ptl = pte_lockptr(mm, old_pmd);
        	_pte = *old_pte;
		_pte2 = *new_pte;
        	//printk("[Walk] new_pte:%lx old_pte:%lx",
        	//              new_pte->pte, old_pte->pte);
        new_page = pte_page(*new_pte);
        old_page = pte_page(*old_pte);
        new_pfn = page_to_pfn(new_page);
        old_pfn = page_to_pfn(old_page);
	//_new_pfn[i + (page_num * th_index)] = new_pfn;
	_new_pfn[i] = new_pfn;
	//printk("[Walk] new_pfn:%lx old_pfn:%lx",
	//			new_pfn, old_pfn);

        new_vma = find_vma(mm, new_hva[i]);
        old_vma = find_vma(mm, old_hva[i]);

	if (!trylock_page(old_page))
		lock_page(old_page);
	if (!trylock_page(new_page))
                lock_page(new_page);


	if (pte_none(*new_pte)) {
                if (old_page->mapping !=
			(struct address_space *)(new_vma->anon_vma)) {
			b_vma_diff = 1;
                        old_page->mapping =
                                (struct address_space *)(new_vma->anon_vma);
                }
                old_page->index = linear_page_index(new_vma,  new_hva[i]);
        } else {
                if (old_page->mapping !=
                        (struct address_space *)(new_vma->anon_vma)) {
                        b_vma_diff = 1;
                        old_page->mapping =
                                (struct address_space *)(new_vma->anon_vma);
			old_page->index = linear_page_index(new_vma, new_hva[i]);
			if (PageSwapBacked(new_page)) {
                		if (!PageSwapBacked(old_page)) {
                		        printk("[vmm] !old_page SwapBacked\n");
                		        SetPageSwapBacked(old_page);
                		}
        		}
		} else
			migrate_page_state_naoki(new_page, old_page);
	}

	if (!pte_none(*new_pte)) {
                int bFree_new_page = 1;
                //printk("[walk] new_pte !none");
                if (!page_mapped(new_page)) {
                        printk("[walk] !new_page mapped (%lx)", new_pfn);
                        bFree_new_page = 0;
                }
                if (!pte_present(*new_pte)) {
                        printk("[walk] !new_pte present (%lx)", new_pfn);
                        bFree_new_page = 0;
                }
                if (is_zero_pfn(pte_pfn(*new_pte))) {
                        printk("[walk] zero_pfn pte:%lx", new_pte->pte);
                        bFree_new_page = 0;
                }
                //if (PageCompound(new_page))
                //      printk("[walk] PageCompound");

                if (page_mapcount(new_page) != 1) {
                        printk("[walk] page_mapcount != 1 (%lx, %d (ref:%d))",
                                new_pfn, new_page->_mapcount.counter,
                                new_page->_refcount.counter);
                        bFree_new_page = 0;
                        //spin_lock(new_pte_ptl);
                        //pte_clear(new_vma->vm_mm, new_hva[i], new_pte);
                        //page_remove_rmap(new_page, false);
                        //spin_unlock(new_pte_ptl);
                }

                if (bFree_new_page) {
                        if (!PageAnon(new_page))
                                printk("[walk] !new_page anon (%lx)", new_pfn);
                        //if (!trylock_page(new_page))
                        //        printk("[walk] !trylock new_page (%lx)",
                        //                                        new_pfn);
                        
			spin_lock(new_pte_ptl);
			spin_lock(&spin_free_page);
                        isolated_lru_page(new_page);
			spin_unlock(&spin_free_page);
                        spin_unlock(new_pte_ptl);

                        //release_pte_page(new_page);
                        //dec_node_page_state(new_page,
                        //  NR_ISOLATED_ANON + page_is_file_cache(new_page));
                        //unlock_page(new_page);
			spin_lock(&spin_free_page);
                        lru_cache_add(new_page);
			spin_unlock(&spin_free_page);
                        put_page(new_page);

                        spin_lock(new_pte_ptl);
                        pte_clear(new_vma->vm_mm, new_hva[i], new_pte);
			spin_lock(&spin_free_page);
                        page_remove_rmap(new_page, false);
			spin_unlock(&spin_free_page);
                        spin_unlock(new_pte_ptl);
                        //free_page_and_swap_cache(new_page);
                        atomic_long_dec(&mm->rss_stat.count[MM_ANONPAGES]);
                        //printk("[walk] new_page ref:%d map:%d",
                        //      new_page->_refcount.counter,
                        //      new_page->_mapcount.counter);
                }
        }

	spin_lock(old_pte_ptl);
        pte_clear(old_vma->vm_mm, old_hva[i], old_pte);

        set_pte_at(new_vma->vm_mm, new_hva[i], new_pte, _pte);
	if (b_vma_diff) {
		spin_lock(&spin_free_page);
		page_remove_rmap(old_page, false);
                page_add_anon_rmap(old_page, new_vma, new_hva[i], false);
		spin_unlock(&spin_free_page);
        }
        update_mmu_cache(new_vma, new_hva[i], new_pte);
        spin_unlock(old_pte_ptl);

	unlock_page(old_page);
        unlock_page(new_page);
	}
	//printk("[walk] walk_time:%llx, change_time:%llx, memory_time:%llx",
	//		vmm_walk_time, vmm_change_time, vmm_memory_time);
	
	//__asm__ volatile ("rdtsc" : "=A" (vmm_end_time));

	//down(&entry_sem);

	spin_lock(&spin_free_page);
	if (bSemDown == 0) {
#ifdef TIME_HYPERCALL_COMPACTION
		struct timespec64 start2, end2;
		getnstimeofday64(&start2);
#endif
		for (i = 0; i < EPT_THREAD_NUM; i++)
			down(&entry_sem[i]);
		bSemDown = 1;
#ifdef TIME_HYPERCALL_COMPACTION
		getnstimeofday64(&end2);
		data_exec_time.ept_vmm_sem_time = 
			calc_exec_time(start2, end2);
#endif
	}
	//mutex_unlock(&mutex_free_page);
	//__asm__ volatile ("rdtsc" : "=A" (sem_sync_time));

	for (i = 0; i < page_num; i++) {
		//_new_page = pfn_to_page(_new_pfn[i + (page_num * th_index)]);
		_new_page = pfn_to_page(_new_pfn[i]);
		free_page_and_swap_cache(_new_page);
	}
	vfree(_new_pfn);
	spin_unlock(&spin_free_page);
//	printk("[VMM] end PTchange\n");
#ifdef TIME_HYPERCALL_COMPACTION
	getnstimeofday64(&end);
	exec_time = calc_exec_time(start, end);
	if (data_exec_time.vmm_time < exec_time)
		data_exec_time.vmm_time = exec_time;
#endif
        return;

}
EXPORT_SYMBOL(change_page_table_for_batch_compaction);

struct change_pt_thread_t {
	struct mm_struct *mm;
	unsigned long *new_hva;
	unsigned long *old_hva;
	int page_num;
	int thread_number;
};

static int change_pt_thread_func(void *arg)
{
	unsigned long *new_hva;
	unsigned long *old_hva;
	int page_num;
	struct change_pt_thread_t *change_pt_thread;
	int thread_number;

	change_pt_thread = (struct change_pt_thread_t *)arg;

	while (1) {
		current->mm = change_pt_thread->mm;
		new_hva = change_pt_thread->new_hva;
		old_hva = change_pt_thread->old_hva;
		page_num = change_pt_thread->page_num;
		thread_number = change_pt_thread->thread_number;

		change_page_table_for_batch_compaction(
				new_hva, old_hva, page_num);
		up(&sem_th);
		if (thread_number == (NUM_CHANGE_PT_THREAD - 1))
			return 0;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule();
	}

	return 0;
}

void change_page_table_for_batch_thread(unsigned long *new_hva,
		unsigned long *old_hva, int page_num)
{
	int i;
	static struct task_struct *my_kthread[NUM_CHANGE_PT_THREAD];
	static struct change_pt_thread_t change_pt_thread[NUM_CHANGE_PT_THREAD];
	static int init = 1;
	int pivot = page_num / NUM_CHANGE_PT_THREAD;

	bSemDown = 0;
	sema_init(&sem_th, 0);
	if (NUM_CHANGE_PT_THREAD > 1) {
		if (init) {
			for (i = 0; i < NUM_CHANGE_PT_THREAD; i++)
				my_kthread[i] = NULL;
			init = 0;
		}
		for (i = 0; i < NUM_CHANGE_PT_THREAD; i++) {
			change_pt_thread[i].mm = current->mm;
			change_pt_thread[i].new_hva = &new_hva[pivot * i];
			change_pt_thread[i].old_hva = &old_hva[pivot * i];
			change_pt_thread[i].page_num = pivot;
			change_pt_thread[i].thread_number = i;
		}
		if (page_num % NUM_CHANGE_PT_THREAD != 0) {
			change_pt_thread[i-1].page_num += 
				page_num % NUM_CHANGE_PT_THREAD;
		}

		for (i = 0; i < NUM_CHANGE_PT_THREAD-1; i++) {
			if (my_kthread[i] == NULL) {
				my_kthread[i] = kthread_run(
					change_pt_thread_func,
					&change_pt_thread[i],
					"PT_change_kth");
				if (IS_ERR(my_kthread)) {
					printk("[PT] !kthread_run");
					change_pt_thread_func(
						(void *)&change_pt_thread[i]);
				}
			} else {
				wake_up_process(my_kthread[i]);
			}
		}
		change_pt_thread_func((void *)&change_pt_thread[i]);
		for (i = 0; i < NUM_CHANGE_PT_THREAD; i++)
			down(&sem_th);
	} else 
		change_page_table_for_batch_compaction(
				new_hva, old_hva, page_num);
	return;
}
EXPORT_SYMBOL(change_page_table_for_batch_thread);

struct arg_PTentry_thread1_t {
	struct mm_struct *mm;
	unsigned long *new_hva;
	unsigned long *old_hva;
	//pte_t **new_pte;
	//pte_t **old_pte;
	unsigned long page_num;
	struct semaphore sem_th;
};

static int change_PTrmap_thread1_func(void *arg)
{
	static struct arg_PTentry_thread1_t *arg_thread1;
	struct mm_struct *mm;
	unsigned long *new_hva, *old_hva;
	pte_t *new_pte, *old_pte;
	pmd_t *new_pmd, *old_pmd;
	spinlock_t *old_pte_ptl;
	unsigned long page_num;
	struct page *new_page, *old_page;
	struct vm_area_struct *new_vma;
	int i;
#ifdef TIME_HYPERCALL_COMPACTION
	struct timespec64 start, end;
#endif

	arg_thread1 = (struct arg_PTentry_thread1_t *)arg;
	while (1) {
#ifdef TIME_HYPERCALL_COMPACTION
		getnstimeofday64(&start);
#endif
		current->mm = arg_thread1->mm;
		new_hva = arg_thread1->new_hva;
		old_hva = arg_thread1->old_hva;
		page_num = arg_thread1->page_num;
		mm = current->mm;

		for (i = 0; i < page_num; i++) {
			new_pte = walk_page_table(mm, new_hva[i], &new_pmd);
			old_pte = walk_page_table(mm, old_hva[i], &old_pmd);
			old_pte_ptl = pte_lockptr(mm, old_pmd);
			new_page = pte_page(*new_pte);
			old_page = pte_page(*old_pte);
			lock_page(new_page);
			lock_page(old_page);
			new_vma = find_vma(mm, new_hva[i]);

			if (old_page->mapping !=
					(struct address_space *)(new_vma->anon_vma)) {
				old_page->mapping = 
					(struct address_space *)(new_vma->anon_vma);
				old_page->index = linear_page_index(new_vma, new_hva[i]);
				if (PageSwapBacked(new_page))
					SetPageSwapBacked(old_page);
				spin_lock(old_pte_ptl);
				page_remove_rmap(old_page, false);
				page_add_anon_rmap(old_page, new_vma, new_hva[i], false);
				spin_unlock(old_pte_ptl);
			} else
				migrate_page_state_naoki(new_page, old_page);
			unlock_page(new_page);
			unlock_page(old_page);
//			up(&sem_vmm_th1);
		}

		up(&arg_thread1->sem_th);
#ifdef TIME_HYPERCALL_COMPACTION
		getnstimeofday64(&end);
		time_exec_func2.vmm_rmap_time = calc_exec_time(start, end);
#endif

#if COMPACTION_THREAD_SUM > 2
                //up(&arg_thread1->sem_th);
		set_current_state(TASK_UNINTERRUPTIBLE);
                //up(&arg_thread1->sem_th);
                schedule();
#else
                //up(&arg_thread1->sem_th);
		return 0;
#endif
	}
	return 0;
}

static int change_PTentry_thread1_func(void *arg)
{
	struct arg_PTentry_thread1_t *arg_thread1;
	struct mm_struct *mm;
	struct vm_area_struct *new_vma;
	unsigned long *new_hva;
	pte_t *new_pte;
	pmd_t *new_pmd;
	spinlock_t *new_pte_ptl;
	unsigned long page_num;
	struct page *new_page;
	int i;
#ifdef TIME_HYPERCALL_COMPACTION
	struct timespec64 start, end;
#endif

#ifdef TIME_HYPERCALL_COMPACTION
	getnstimeofday64(&start);
#endif
	arg_thread1 = (struct arg_PTentry_thread1_t *)arg;
	current->mm = arg_thread1->mm;
	new_hva = arg_thread1->new_hva;
	page_num = arg_thread1->page_num;
	mm = current->mm;

	for (i = 0; i < page_num; i++) {
		new_pte = walk_page_table(mm, new_hva[i], &new_pmd);
		new_pte_ptl = pte_lockptr(mm, new_pmd);
		new_page = pte_page(*new_pte);
		new_vma = find_vma(mm, new_hva[i]);

		if (!pte_none(*new_pte)) {
			int bFree_new_page = 1;
                	if (!page_mapped(new_page)) {
                	        printk("[walk] !new_page mapped\n");
                	        bFree_new_page = 0;
                	}
                	if (!pte_present(*new_pte)) {
                	        printk("[walk] !new_pte present\n");
                	        bFree_new_page = 0;
                	}
                	if (is_zero_pfn(pte_pfn(*new_pte))) {
                	        printk("[walk] zero_pfn pte:%lx\n", new_pte->pte);
                	        bFree_new_page = 0;
                	}
                	//if (PageCompound(new_page))
                	//      printk("[walk] PageCompound");

                	if (page_mapcount(new_page) != 1) {
                        	printk("[walk] page_mapcount != 1 (%d (ref:%d))\n",
                        	        new_page->_mapcount.counter,
                        	        new_page->_refcount.counter);
                        	//bFree_new_page = 0;
                	}

			if (bFree_new_page) {
				spin_lock(new_pte_ptl);
				isolated_lru_page(new_page);
				spin_unlock(new_pte_ptl);

				lru_cache_add(new_page);
				put_page(new_page);

				spin_lock(new_pte_ptl);
				//pte_clear(new_vma->vm_mm, new_hva[i], new_pte);
				page_remove_rmap(new_page, false);
				spin_unlock(new_pte_ptl);
				atomic_long_dec(&mm->rss_stat.count[MM_ANONPAGES]);
				//free_page_and_swap_cache(new_page);
			}
		}
//		up(&sem_vmm_th2);
	}		
#ifdef TIME_HYPERCALL_COMPACTION
	getnstimeofday64(&end);
	time_exec_func2.vmm_entry_time = calc_exec_time(start, end);
#endif

	return 0;
}

static int update_PTentry_thread1_func(void *arg)
{
	static struct arg_PTentry_thread1_t *arg_thread1;
	struct mm_struct *mm;
	struct vm_area_struct *new_vma, *old_vma;
	unsigned long *new_hva, *old_hva;
	pmd_t *new_pmd, *old_pmd;
	pte_t *new_pte, *old_pte;
	int page_num;
	int i;
#ifdef TIME_HYPERCALL_COMPACTION
	struct timespec64 start, end;
#endif

	arg_thread1 = (struct arg_PTentry_thread1_t *)arg;
	while (1) {
#ifdef TIME_HYPERCALL_COMPACTION
		getnstimeofday64(&start);
#endif
		current->mm = arg_thread1->mm;
		new_hva = arg_thread1->new_hva;
		old_hva = arg_thread1->old_hva;
		page_num = arg_thread1->page_num;
		mm = current->mm;

		for (i = 0; i < page_num; i++) {
			struct page *new_page = NULL;
			pte_t _pte;

			new_pte = walk_page_table(mm, new_hva[i], &new_pmd);
			old_pte = walk_page_table(mm, old_hva[i], &old_pmd);
			_pte = *old_pte;

			down(&sem_page_num);
//			down(&sem_vmm_th1);
//			down(&sem_vmm_th2);
			new_vma = find_vma(mm, new_hva[i]);
			old_vma = find_vma(mm, old_hva[i]);
			if(!pte_none(*new_pte))
				new_page = pte_page(*new_pte);
			pte_clear(new_vma->vm_mm, new_hva[i], new_pte);
			pte_clear(old_vma->vm_mm, old_hva[i], old_pte);
			set_pte_at(new_vma->vm_mm, new_hva[i], new_pte, _pte);
			if (new_page)
				free_page_and_swap_cache(new_page);
		}

		up(&arg_thread1->sem_th);
#ifdef TIME_HYPERCALL_COMPACTION
		getnstimeofday64(&end);
		time_exec_func2.vmm_extra_time = calc_exec_time(start, end);
#endif

#if COMPACTION_THREAD_SUM > 3
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule();
#else
		return 0;
#endif
	}
	return 0;
}


void change_PTentry_thread2_func(unsigned long *new_hva,
		unsigned long *old_hva, unsigned long page_num)
{
#if COMPACTION_THREAD_SUM > 2
	static struct task_struct *PTrmap_kth = NULL;
#endif
#if COMPACTION_THREAD_SUM > 3
	static struct task_struct *PTentry_kth = NULL;
#endif
	static struct arg_PTentry_thread1_t arg_PTentry_thread1;
	static struct arg_PTentry_thread1_t arg_PTentry_thread2;
	//struct page *new_page, *old_page;
#ifdef TIME_HYPERCALL_COMPACTION
	struct timespec64 start, end;
#endif

#ifdef TIME_HYPERCALL_COMPACTION
	getnstimeofday64(&start);
#endif
	
	arg_PTentry_thread1.mm = current->mm;
	arg_PTentry_thread1.new_hva = new_hva;
	arg_PTentry_thread1.old_hva = old_hva;
	//arg_PTentry_thread1.new_pte = new_pte;
	//arg_PTentry_thread1.old_pte = old_pte;
	arg_PTentry_thread1.page_num = page_num;
	sema_init(&arg_PTentry_thread1.sem_th, 0);
#if COMPACTION_THREAD_SUM > 2
	if (!PTrmap_kth) {
		PTrmap_kth = kthread_run(change_PTrmap_thread1_func,
			   &arg_PTentry_thread1, "PTrmap_kth");
		if (IS_ERR(PTrmap_kth)) {
			printk("[VMM] !PTrmap_kth run\n");
			change_PTrmap_thread1_func((void *)&arg_PTentry_thread1);
		}
	} else {
		wake_up_process(PTrmap_kth);
	}
#else
	change_PTrmap_thread1_func((void *)&arg_PTentry_thread1);
#endif

	arg_PTentry_thread2.mm = current->mm;
	arg_PTentry_thread2.new_hva = new_hva;
	arg_PTentry_thread2.old_hva = old_hva;
	arg_PTentry_thread2.page_num = page_num;
	sema_init(&arg_PTentry_thread2.sem_th, 0);

#if COMPACTION_THREAD_SUM > 3
	if (!PTentry_kth) {
		PTentry_kth = kthread_run(update_PTentry_thread1_func,
				&arg_PTentry_thread2, "PTentry_kth");
		if (IS_ERR(PTentry_kth)) {
			printk("[vmm] !PTentry_kth run\n");
			update_PTentry_thread1_func((void *)&arg_PTentry_thread2);
		}
	} else {
		wake_up_process(PTentry_kth);
	}
#endif

	change_PTentry_thread1_func((void *)&arg_PTentry_thread1);
#if COMPACTION_THREAD_SUM <= 3
	update_PTentry_thread1_func((void *)&arg_PTentry_thread2);
#endif

	down(&arg_PTentry_thread1.sem_th);
	down(&arg_PTentry_thread2.sem_th);

	//down(&sem_change_PT_and_EPT_entry);
#ifdef TIME_HYPERCALL_COMPACTION
	getnstimeofday64(&end);
	time_exec_func2.vmm_time = calc_exec_time(start, end);
#endif

	return;
}
EXPORT_SYMBOL(change_PTentry_thread2_func);

void change_single_PTentry(unsinged long new_hva, unsigned long old_hva)
{
	struct mm_struct *mm;
	struct vm_area_struct *new_vma, *old_vma;
	pmd_t *new_pmd, *old_pmd;
	pte_t *new_pte, *old_pte;
	pte_t _pte;
	struct page *new_page, *old_page;
	spinlock_t *new_pte_ptl, *old_pte_ptl;

	mm = current->mm;
	new_pte = walk_page_table(mm, new_hva, &new_pmd);
	old_pte = walk_page_table(mm, old_hva, &old_pmd);
	new_pte_ptl = pte_lockptr(mm, new_pmd);
	old_pte_ptl = pte_lockptr(mm, old_pmd);
	new_page = pte_page(*new_pte);
	old_page = pte_page(*old_pte);
	lock_page(new_page);
	lock_page(old_page);
	new_vma = find_vma(mm, new_hva);
	old_vma = find_vma(mm, old_hva);

	if (old_page->mapping !=
				(struct address_space *)(new_vma->anon_vma)) {
		old_page->mapping = 
				(struct address_space *)(new_vma->anon_vma);
		old_page->index = linear_page_index(new_vma, new_hva);
		if (PageSwapBacked(new_page))
			SetPageSwapBacked(old_page);
		spin_lock(old_pte_ptl);
		page_remove_rmap(old_page, false);
		page_add_anon_rmap(old_page, new_vma, new_hva, false);
		spin_unlock(old_pte_ptl);
	} else {
		migrate_page_state_naoki(new_page, old_page);
	}

	if (!pte_none(*new_pte)) {
		int bFree_new_page = 1;
      	if (!page_mapped(new_page)) {
       	        printk("[walk] !new_page mapped\n");
       	        bFree_new_page = 0;
       	}
       	if (!pte_present(*new_pte)) {
       	        printk("[walk] !new_pte present\n");
       	        bFree_new_page = 0;
       	}
       	if (is_zero_pfn(pte_pfn(*new_pte))) {
       	        printk("[walk] zero_pfn pte:%lx\n", new_pte->pte);
       	        bFree_new_page = 0;
       	}
       	//if (PageCompound(new_page))
      	//      printk("[walk] PageCompound");

       	if (page_mapcount(new_page) != 1) {
               	printk("[walk] page_mapcount != 1 (%d (ref:%d))\n",
    	      	        new_page->_mapcount.counter,
	          	        new_page->_refcount.counter);
              	//bFree_new_page = 0;
       	}

		if (bFree_new_page) {
			spin_lock(new_pte_ptl);
			isolated_lru_page(new_page);
			spin_unlock(new_pte_ptl);

			lru_cache_add(new_page);
			put_page(new_page);

			spin_lock(new_pte_ptl);
			//pte_clear(new_vma->vm_mm, new_hva[i], new_pte);
			page_remove_rmap(new_page, false);
			spin_unlock(new_pte_ptl);
			atomic_long_dec(&mm->rss_stat.count[MM_ANONPAGES]);
			//free_page_and_swap_cache(new_page);
		}
	}

	spin_lock(new_pte_ptl);
	spin_lock(old_pte_ptl);
	pte_clear(new_vma->vm_mm, new_hva, new_pte);
	pte_clear(old_vma->vm_mm, old_hva, old_pte);
	set_pte_at(new_vma->vm_mm, new_hva, new_pte, _pte);
	update_mmu_cache(new_vma, new_hva, new_pte);
	spin_unlock(old_pte_ptl);
	spin_unlock(new_pte_ptl);

	unlock_page(old_page);
	unlock_page(new_page);
	free_page_and_swap_cache(new_page);
	return;
}
EXPORT_SYMBOL(change_single_PTentry);

static pte_t *walk_page_table(struct mm_struct *mm, unsigned long hva,
				pmd_t **pmd)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	//pmd_t *pmd;
	pte_t *pte;
	
	pgd = pgd_offset(mm, hva);
	if (!pgd_present(*pgd))
		return NULL;
	p4d = p4d_offset(pgd, hva);
	if (!p4d_present(*p4d))
		return NULL;
	pud = pud_offset(p4d, hva);
	if (!pud_present(*pud))
		return NULL;
	*pmd = pmd_offset(pud, hva);
	if (!pmd_present(**pmd) || pmd_trans_huge(**pmd))
		return NULL;
	pte = pte_offset_map(*pmd, hva);
	return pte;
}

unsigned long walk_hva_to_pfn(unsigned long hva) {
	struct mm_struct *mm = current->mm;
	pgd_t *pgd;
        p4d_t *p4d;
        pud_t *pud;
        pmd_t *pmd;
        pte_t *pte;

        pgd = pgd_offset(mm, hva);
        if (!pgd_present(*pgd))
                return -1;
        p4d = p4d_offset(pgd, hva);
        if (!p4d_present(*p4d))
                return -1;
        pud = pud_offset(p4d, hva);
        if (!pud_present(*pud))
                return -1;
        pmd = pmd_offset(pud, hva);
        if (!pmd_present(*pmd) || pmd_trans_huge(*pmd))
                return -1;
        pte = pte_offset_map(pmd, hva);

	//if (pte_none(*pte))
	//	printk("[EPTwalk] pte_none:%lx\n", pte->pte);

	return pte_pfn(*pte);
}
EXPORT_SYMBOL(walk_hva_to_pfn);

static void isolated_lru_page(struct page *page)
{
	int ret = -1;
	VM_BUG_ON_PAGE(!page_count(page), page);
	WARN_RATELIMIT(PageTail(page), "trying to isolate tail page");
	if (PageLRU(page)) {
		struct zone *zone = page_zone(page);
		struct lruvec *lruvec;
		
		spin_lock_irq(zone_lru_lock(zone));
		lruvec = mem_cgroup_page_lruvec(page, zone->zone_pgdat);
		if (PageLRU(page)) {
			int lru = page_lru(page);
			get_page(page);
			ClearPageLRU(page);
			del_page_from_lru_list(page, lruvec, lru);
			ret = 0;
		}
		spin_unlock_irq(zone_lru_lock(zone));
	}
	if (ret)
		printk("[walk] !clear LRUi flag; page:%lx", page_to_pfn(page));
}

static bool should_defer_flush(struct mm_struct *mm, enum ttu_flags flags)
{
	bool should_defer = false;

	if (!(flags & TTU_BATCH_FLUSH))
		return false;

	if (cpumask_any_but(mm_cpumask(mm), get_cpu()) < nr_cpu_ids)
		should_defer = true;
	put_cpu();

	return should_defer;
}

static void set_tlb_ubc_flush_pending(struct mm_struct *mm, bool writable)
{
	struct tlbflush_unmap_batch *tlb_ubc = &current->tlb_ubc;

	arch_tlbbatch_add_mm(&tlb_ubc->arch, mm);
	tlb_ubc->flush_required = true;

	barrier();
	mm->tlb_flush_batched = true;

	if (writable)
		tlb_ubc->writable = true;
}

void foo_compaction(void)
{
	return;
}
EXPORT_SYMBOL(foo_compaction);

