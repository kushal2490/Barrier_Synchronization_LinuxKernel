#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/module.h>

static int barrier_number = 5;
ktime_t ktime;
LIST_HEAD(barrier_list);
DEFINE_SPINLOCK(global_lock);
DEFINE_SPINLOCK(barrier_id_lock);

typedef struct barrier {
	unsigned int count;
	unsigned int barrier_id;
	signed int timeout;
	unsigned int curr_count;
	unsigned int barrier_entry_count;
	unsigned int timeout_count;
	unsigned long period_ns;
	pid_t tgid;
	spinlock_t lock_barrier;
	struct mutex bar_mutex_lock;
	struct hrtimer my_hrtimer;
	struct list_head listnode;
	wait_queue_head_t my_event;

	bool TIMEOUT;
}bdata;

/* Timer callback function */
static enum hrtimer_restart myfunc(struct hrtimer *timer)
{
	struct barrier *barrierp;

	/* Get the per-device structure that contains this cdev */
	barrierp = container_of(timer, struct barrier, my_hrtimer);

	printk("\n\t[BARRIER:%d] Thread:%d TIMEOUT DETECTED -- current Count=%d\n\n", barrierp->barrier_id, current->pid, barrierp->curr_count);
	barrierp->TIMEOUT = 1;
	barrierp->curr_count = 0;

	//Wake up all waiting threads
	wake_up_all(&barrierp->my_event);
	return HRTIMER_NORESTART;
}

/* syscall to initialize a barrier and return barrier_id */
asmlinkage long sys_barrier_init(unsigned int count, unsigned int *barrier_id, signed int timeout)
{
	bdata *barrierp = kmalloc(sizeof(bdata), GFP_KERNEL);
	if(!barrierp) {
		printk("Bad kmalloc: barrier allocation\n");
	}
	memset(barrierp, 0, sizeof(bdata));

	// printk("sys_barrier_init Entered\n");

	barrierp->count = count;
	barrierp->curr_count = 0;
	barrierp->barrier_entry_count = 0;
	barrierp->tgid = current->tgid;
	barrierp->TIMEOUT = 0;
	barrierp->timeout_count = 0;
	barrierp->timeout = timeout;

	spin_lock_init(&barrierp->lock_barrier);
	mutex_init(&barrierp->bar_mutex_lock);

	spin_lock(&barrier_id_lock);
	barrierp->barrier_id = barrier_number++;
	*barrier_id = barrierp->barrier_id;
	spin_unlock(&barrier_id_lock);

	hrtimer_init(&(barrierp->my_hrtimer), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
 	barrierp->my_hrtimer.function = &myfunc;
	
	//Init process wait queue
	init_waitqueue_head(&barrierp->my_event);
	
	//Init global barrier list to store multiple barriers
	INIT_LIST_HEAD(&barrierp->listnode);
	spin_lock(&global_lock);
 	list_add(&barrierp->listnode, &barrier_list);
	spin_unlock(&global_lock);

	printk("Barrier %d initialized in process:%d with count:%d\n", barrierp->barrier_id, barrierp->tgid, barrierp->count);

	return 0;
}

/* Returns barrier reference based on a barried_id */
bdata* search_list(unsigned int search_id) {
	bdata *barrier_this = NULL;

	spin_lock(&global_lock);
	list_for_each_entry(barrier_this, &barrier_list, listnode)
	{
		if(barrier_this->barrier_id == search_id && barrier_this->tgid == current->tgid)
		{
			return barrier_this;
		}
	}
	spin_unlock(&global_lock);
	return (void *)-EINVAL;
}

/* syscall to wait on barrier */
asmlinkage long sys_barrier_wait(unsigned int barrier_id1)
{
	bdata *barrier_this;

	barrier_this = search_list(barrier_id1);
	if(barrier_this == (void*)-EINVAL)
	{
		return -EINVAL;
	}
	// printk("sys_barrier_wait Entered\n");
	mutex_lock_interruptible(&(barrier_this->bar_mutex_lock));
	barrier_this->barrier_entry_count += 1;

	if(barrier_this->barrier_entry_count < barrier_this->count)
	{
		mutex_unlock(&(barrier_this->bar_mutex_lock));
	}

	if(barrier_this->TIMEOUT == 1)  {
		goto timeout;
	}
	spin_lock(&barrier_this->lock_barrier);

	//count to keep track of number of threads entered
	barrier_this->curr_count +=1;
	if(barrier_this->curr_count < barrier_this->count) {

		spin_unlock(&barrier_this->lock_barrier);

		//First thread of a thread group arrives at the barrier and starts the timer for timeout period
		if( (barrier_this->timeout > 0) && barrier_this->curr_count==1 )
		{
			barrier_this->period_ns = barrier_this->timeout;
			ktime = ktime_set( 0, barrier_this->period_ns );
			printk("\n\t\t--------[BARRIER:%d] Thread : %d Starting TIMER ------------\n", barrier_this->barrier_id, current->pid);
			hrtimer_start((&barrier_this->my_hrtimer), ktime, HRTIMER_MODE_REL);
		}

		//threads wait on the barrier until timeout
		printk("\t\t[BARRIER:%d] EntryCount = %d -- Thread:%d --SLEEPS\n", barrier_this->barrier_id, barrier_this->curr_count, current->pid);
		wait_event_interruptible(barrier_this->my_event, barrier_this->curr_count==0);
		printk("\t[BARRIER:%d] EntryCount = %d -- Thread:%d WAKES\n", barrier_this->barrier_id, barrier_this->curr_count, current->pid);

		if(barrier_this->TIMEOUT == 1) {
			goto timeout;
		}
		spin_lock(&barrier_this->lock_barrier);
	}
	//last thread of a thread group wakes up all threads from the wait queue
	if(barrier_this->curr_count == barrier_this->count)
	{
		hrtimer_cancel(&barrier_this->my_hrtimer);

		barrier_this->curr_count = 0;
		wake_up_all(&barrier_this->my_event);
		printk("\n\t[BARRIER:%d] EntryCount = %d -- Thread:%d WAKING UP ALL >>>>>>>>>>>>>>>\n\n", barrier_this->barrier_id, barrier_this->count, current->pid);
	}

	barrier_this->barrier_entry_count -= 1;
	if(barrier_this->barrier_entry_count == 0)
	{
		mutex_unlock(&(barrier_this->bar_mutex_lock));
	}

	spin_unlock(&barrier_this->lock_barrier);
	
	return 0;
//threads jump here on a timeout
timeout:
		barrier_this->timeout_count +=1;
		printk("\n\t[BARRIER:%d] Thread:%d timed out\n\n", barrier_this->barrier_id, current->pid);
		if(barrier_this->timeout_count >= barrier_this->count)
		{
			mutex_unlock(&(barrier_this->bar_mutex_lock));
		}
	return -ETIME;
}

/* syscall to reset the barrier after a timeout caused a broken barrier */
asmlinkage long sys_barrier_reset(unsigned int barrier_id)
{
	bdata *barrier_this;
	// printk("sys_barrier_reset Entered\n");
	barrier_this = search_list(barrier_id);
	if(barrier_this == (void*)-EINVAL)
	{	
		return -EINVAL;
	}
	//Barrier Reset flags  and current count for the barrier
	barrier_this->TIMEOUT = 0;
	barrier_this->curr_count = 0;
	barrier_this->barrier_entry_count = 0;

	return 0;
}

/* syscall to delete the barriers from the global barrier list */
asmlinkage long sys_barrier_destroy(unsigned int barrier_id)
{
	bdata *barrier_this;

	// printk("sys_barrier_destroy Entered\n");
	barrier_this = search_list(barrier_id);
	if(barrier_this == (void*)-EINVAL)
	{	
		// printk("Could not find [BARRIER:%d]\n", barrier_this->barrier_id);
		return -EINVAL;
	}

	if(list_empty(&barrier_list))
	{
		printk("\t\t*****destroyed all barriers*****\n");
		return -ENOENT;
	}

	printk("[Barrier:%d] destroyed in Process:%d\n", barrier_this->barrier_id, barrier_this->tgid);
	list_del_init(&barrier_this->listnode);

	return 0;
}

EXPORT_SYMBOL_GPL(sys_barrier_destroy);
EXPORT_SYMBOL_GPL(sys_barrier_init);
EXPORT_SYMBOL_GPL(sys_barrier_wait);

MODULE_LICENSE("GPL v2");