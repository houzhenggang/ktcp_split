#include <linux/init.h>      // included for __init and __exit macros
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include "cbn_common.h"
#include "pool.h"

#define cbn_list_del(x) {pr_err("list_del(%d:%s): %p {%p, %p}", __LINE__, current->comm, x, (x)->next, (x)->prev); list_del((x));}
#define cbn_list_add(x, h) {pr_err("list_add(%d:%s): %p {%p, %p} h %p {%p, %p}", 	\
					__LINE__, current->comm,			\
					x, (x)->next, (x)->prev,			\
					h, (h)->next, (h)->prev);			\
					list_add((x), (h));}

static void kthread_pool_reuse(struct kthread_pool *cbn_pool, struct pool_elem *elem)
{
	list_del(&elem->list);
	list_add(&elem->list, &cbn_pool->kthread_pool);
	--cbn_pool->refil_needed;
}

static int pipe_loop_task(void *data)
{
	struct pool_elem *elem = data;
	struct kthread_pool *pool = elem->pool;

	while (!kthread_should_stop()) {
		pr_info("running %s\n", current->comm);
		if (elem->pool_task)
			elem->pool_task(elem->data);
		else
			pr_err("ERROR %s: no pool task\n", __FUNCTION__);

		set_current_state(TASK_INTERRUPTIBLE);
		if (!kthread_should_stop()) {
			pr_info("%s out to reuse\n", current->comm);
			kthread_pool_reuse(pool, elem);
			schedule();
		}
		__set_current_state(TASK_RUNNING);
	// TODO:
	//	consider adding some state? user might try freeing this struct, make sure its not running
	//	also consider frreing yourself if you are here...
	}
	pr_warn("%s out no reuse\n", current->comm);
	return 0;
}

static int (*threadfn)(void *data) = pipe_loop_task;

static inline void refill_pool(struct kthread_pool *cbn_pool, int count)
{
	count = (count) ? count : cbn_pool->refil_needed;

	while (count--) {
		struct pool_elem *elem = kmem_cache_alloc(cbn_pool->pool_slab, GFP_KERNEL);
		struct task_struct *k
			= kthread_create(threadfn, elem, "pool-thread-%d", cbn_pool->top_count);
		if (unlikely(!k)) {
			pr_err("failed to create kthread %d\n", cbn_pool->top_count);
			kmem_cache_free(cbn_pool->pool_slab, elem);
			return;
		}
		INIT_LIST_HEAD(&elem->list);
		elem->task = k;
		elem->pool = cbn_pool;
		list_add(&elem->list, &cbn_pool->kthread_pool);
		//pr_info("pool thread %d [%p] allocated %llx\n", cbn_pool->top_count, elem, rdtsc());
		--cbn_pool->refil_needed;
		++cbn_pool->top_count;
	}
}

static int refil_thread(void *data)
{
	struct kthread_pool *cbn_pool = data;

	while (!kthread_should_stop()) {
		refill_pool(cbn_pool, 0);

		set_current_state(TASK_INTERRUPTIBLE);
		if (!kthread_should_stop())
			schedule();
		__set_current_state(TASK_RUNNING);
	}
	pr_warn("%s going out\n", __FUNCTION__);
	return 0;
}

static struct pool_elem *kthread_pool_alloc(struct kthread_pool *cbn_pool)
{
	struct pool_elem *elem = NULL;

	while (unlikely(list_empty(&cbn_pool->kthread_pool))) {
		pr_warn("pool is empty refill is to slow\n");
		refill_pool(cbn_pool, 1);
	}

	elem = list_first_entry(&cbn_pool->kthread_pool, struct pool_elem, list);
	list_del(&elem->list);
	++cbn_pool->refil_needed;
	//pr_err("allocated %p\n", elem);
	wake_up_process(cbn_pool->refil);
	return elem;
}

struct pool_elem *kthread_pool_run(struct kthread_pool *cbn_pool, int (*func)(void *), void *data)
{
	struct pool_elem *elem = kthread_pool_alloc(cbn_pool);
	if (unlikely(!elem)) {
		pr_err("Failed to alloc elem\n");
		return ERR_PTR(-ENOMEM);
	}
	elem->pool_task = func;
	elem->data = data;
	list_add(&elem->list, &cbn_pool->kthread_running);
	TRACE_PRINT("staring %s\n", elem->task->comm);
	wake_up_process(elem->task);
	return elem;
}

int __init cbn_kthread_pool_init(struct kthread_pool *cbn_pool)
{
	pr_info("starting: %s\n", __FUNCTION__);
	INIT_LIST_HEAD(&cbn_pool->kthread_pool);
	INIT_LIST_HEAD(&cbn_pool->kthread_running);
	cbn_pool->pool_slab = kmem_cache_create("pool-thread-cache",
						sizeof(struct pool_elem), 0, 0, NULL);

	cbn_pool->refil_needed = cbn_pool->pool_size;
	cbn_pool->refil = kthread_run(refil_thread, cbn_pool, "pool-cache-refill");

	set_user_nice(cbn_pool->refil, MAX_NICE);
	return 0;
}

void __exit cbn_kthread_pool_clean(struct kthread_pool *cbn_pool)
{
	struct list_head *itr, *tmp;
	pr_info("stopping: %s\n", __FUNCTION__);

	kthread_stop(cbn_pool->refil);

	list_for_each_safe(itr, tmp, &cbn_pool->kthread_pool) {
		struct pool_elem *task = container_of(itr, struct pool_elem, list);
		list_del(itr);
		kthread_stop(task->task);
		kmem_cache_free(cbn_pool->pool_slab, task);
	}

	list_for_each_safe(itr, tmp, &cbn_pool->kthread_running) {
		struct pool_elem *task = container_of(itr, struct pool_elem, list);
		list_del(itr);
		kthread_stop(task->task);
		kmem_cache_free(cbn_pool->pool_slab, task);
	}
	kmem_cache_destroy(cbn_pool->pool_slab);
	pr_info("stopping: elements freed");
}

