#include <linux/init.h>      // included for __init and __exit macros
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <linux/printk.h>

struct kthread_pool {
	int top_count;				//TODO: add spin_lock - need to protect lists and counters
	int running_count;
	int pool_size;				// TODO:Modify with debugfs or module param
	struct kmem_cache *pool_slab;
	struct task_struct *refil;
	struct list_head kthread_pool;
	struct list_head kthread_running;
	void (*pool_task)(void *data);		// TODO:just as easily can move to pool_elem
};

struct pool_elem {
	struct list_head list;
	struct task_struct *task;
	struct kthread_pool *pool;

	union {
		uint64_t _unspec[4];		// TODO:can be variable size, just need to tellcache_init
	};
};

static int pipe_loop_task(void *data)
{
	struct pool_elem *elem = data;
	struct kthread_pool *pool = elem->pool;

	while (!kthread_should_stop()) {
		pr_err("%s running\n", current->comm);
		if (pool->pool_task)
			pool->pool_task(data);

		pr_err("%s sleep\n", current->comm);
		set_current_state(TASK_INTERRUPTIBLE);
		if (!kthread_should_stop()) {
			schedule();
		}
		__set_current_state(TASK_RUNNING);
	// TODO:
	//	consider adding some state? user might try freeing this struct, make sure its not running
	//	also consider frreing yourself if you are here...
	}
	pr_err("%s end\n", current->comm);
	return 0;
}

static int empty_task()
{
	return 0;
}

static int (*threadfn)(void *data) = empty_task;

static inline void refill_pool(struct kthread_pool *cbn_pool, int count)
{
	count = (count) ? count : cbn_pool->pool_size - cbn_pool->running_count;

	while (count--) {
		struct pool_elem *elem = kmem_cache_alloc(cbn_pool->pool_slab, GFP_KERNEL);
		struct task_struct *k
			= kthread_create(threadfn, elem->_unspec, "pool-thread-%d", cbn_pool->top_count);
		if (unlikely(!k)) {
			pr_err("failed to create kthread %d\n", cbn_pool->top_count);
			kmem_cache_free(cbn_pool->pool_slab, elem);
			return;
		}
		INIT_LIST_HEAD(&elem->list);
		elem->task = k;
		list_add(&elem->list, &cbn_pool->kthread_pool);
		pr_err("pool thread %d allocated %llx\n", cbn_pool->top_count, rdtsc());
		++cbn_pool->top_count;
	}
}

#define LOOP 1000

static inline u64 barr(void)
{
	unsigned int low, high;

	asm volatile ("cpuid\n\t"
		"rdtsc\n\t"
		"mov %%edx, %0\n\t"
		"mov %%eax, %1\n\t": "=r" (high), "=r" (low)::
		"%rax", "%rbx", "%rcx", "%rdx");
	return low|(((u64)high) << 32);
}

static inline u64 rdtscp(void)                                                                                                                                        
{
	unsigned int low, high;

	asm volatile("rdtscp" : "=a" (low), "=d" (high));

	return low | ((u64)high) << 32;
}

static void test(void)
{
	int count = LOOP;
	u64 max = 0;
	rdtscp();
	while (count--) {
		struct task_struct *k;
		u64 s, e;
		s = barr();
		k = kthread_run(threadfn, NULL, "pool-thread-%d", count);
		e = rdtscp();
		max = ((e-s) > max) ? (e-s) : max;
		if (unlikely(!k)) {
			pr_err("failed to create kthread %d\n", count);
			return;
		}
	}
	pr_info("loop of %d [%ld] max %lld\n", LOOP, sizeof(struct task_struct)/1024, max);
}

static int __init cbn_kthread_pool_init(void)
{
	pr_err("starting server_task\n");

	//check for failure?
	test();
	return 0;
}

static void __exit cbn_kthread_pool_clean(void)
{
	pr_err("stopping server_task\n");
}

module_init(cbn_kthread_pool_init);
module_exit(cbn_kthread_pool_clean);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Markuze Alex");
MODULE_DESCRIPTION("CBN Kthread Pool");

