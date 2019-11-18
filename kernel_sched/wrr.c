#include "sched.h"

#include <linux/slab.h>
static inline struct task_struct *wrr_task_of(struct sched_wrr_entity *wrr_se)
{
	return container_of(wrr_se, struct task_struct, wrr);
}
#ifdef CONFIG_SMP
static struct task_struct *pick_pullable_task_wrr(struct rq *src_rq,
						  int this_cpu)
{
	struct task_struct *p = NULL;
	struct sched_wrr_entity *pos;
	if (list_empty(&src_rq->wrr.queue))
		return p;

	list_for_each_entry(pos, &src_rq->wrr.queue, run_list) {
		p = wrr_task_of(pos);
		if (p == src_rq->curr)
			continue;
		if (cpumask_test_cpu(this_cpu, &p->cpus_allowed))
			return p;
	}
	return NULL;
}

void idle_balance_wrr(struct rq *this_rq)
{
	int this_cpu = this_rq->cpu, cpu;
	struct task_struct *p;
	struct rq *src_rq;

	for_each_possible_cpu(cpu) {
		if (this_cpu == cpu)
			continue;

		src_rq = cpu_rq(cpu);
		double_lock_balance(this_rq, src_rq);

		if (src_rq->wrr.wrr_nr_running <= 1)
			goto skip;

		p = pick_pullable_task_wrr(src_rq, this_cpu);

		if (p) {
			if (p == src_rq->curr)
				goto skip;
			WARN_ON(!p->on_rq);

			deactivate_task(src_rq, p, 0);
			set_task_cpu(p, this_cpu);
			activate_task(this_rq, p, 0);
			double_unlock_balance(this_rq, src_rq);
			return;
		}
skip:
		double_unlock_balance(this_rq, src_rq);
	}
}

static int
select_task_rq_wrr(struct task_struct *p, int sd_flag, int flags)
{
	int cpu;
	int min_cpu = task_cpu(p);
	int min_weight = INT_MAX;

	if (p->nr_cpus_allowed == 1)
		goto out;

	/* For anything but wake ups, just return the task_cpu */
	if (sd_flag != SD_BALANCE_WAKE && sd_flag != SD_BALANCE_FORK)
		goto out;

	for_each_possible_cpu(cpu) {
		if (my_wrr_info.total_weight[cpu] < min_weight) {
			min_weight = my_wrr_info.total_weight[cpu];
			min_cpu = cpu;
		}
	}
	return min_cpu;
out:
	return min_cpu;
}
#endif

void init_wrr_rq(struct wrr_rq *wrr_rq)
{
	INIT_LIST_HEAD(&wrr_rq->queue);
	wrr_rq->wrr_nr_running = 0;

}
static void switched_to_wrr(struct rq *rq, struct task_struct *p)
{
	/* Do nothing */
}

static void set_curr_task_wrr(struct rq *rq)
{
	/* Do nothing */
}
static void put_prev_task_wrr(struct rq *rq, struct task_struct *p)
{
	/* Do nothiing */
}

static void check_preempt_curr_wrr(struct rq *rq,
				   struct task_struct *p, int flags)
{
}

static struct task_struct *pick_next_task_wrr(struct rq *rq)
{
	struct task_struct *p;
	struct wrr_rq *wrr_rq;
	struct sched_wrr_entity *next = NULL;

	wrr_rq = &rq->wrr;

	if (list_empty(&wrr_rq->queue))
		return NULL;


	next = list_entry(wrr_rq->queue.next,
			  struct sched_wrr_entity, run_list);
	p = wrr_task_of(next);
	return p;
}

static void dequeue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_wrr_entity *wrr_se = &p->wrr;
	struct wrr_rq *wrr_rq = &rq->wrr;
	int cpu;
	if (wrr_se == NULL)
		return;

	list_del_init(&wrr_se->run_list);
	wrr_rq->wrr_nr_running--;
	dec_nr_running(rq);
	cpu = cpu_of(rq);
	raw_spin_lock(&wrr_info_locks[cpu]);
	my_wrr_info.nr_running[cpu]--;
	my_wrr_info.total_weight[cpu] -= wrr_se->weight;
	raw_spin_unlock(&wrr_info_locks[cpu]);
}

static void
enqueue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_wrr_entity *wrr_se = &p->wrr;
	struct wrr_rq *wrr_rq = &rq->wrr;
	int cpu;
	if (wrr_se == NULL)
		return;
	if (flags & ENQUEUE_HEAD)
		list_add(&wrr_se->run_list, &wrr_rq->queue);
	else
		list_add_tail(&wrr_se->run_list, &wrr_rq->queue);
	wrr_rq->wrr_nr_running++;
	inc_nr_running(rq);
	cpu = cpu_of(rq);
	raw_spin_lock(&wrr_info_locks[cpu]);
	my_wrr_info.nr_running[cpu]++;
	my_wrr_info.total_weight[cpu] += wrr_se->weight;
	raw_spin_unlock(&wrr_info_locks[cpu]);
}

static void requeue_task_wrr(struct rq *rq, struct task_struct *p, int head)
{
	struct sched_wrr_entity *wrr_se = &p->wrr;
	struct wrr_rq *wrr_rq = &rq->wrr;

	if (wrr_se == NULL)
		return;

	list_move_tail(&wrr_se->run_list, &wrr_rq->queue);
}

static void yield_task_wrr(struct rq *rq)
{
	requeue_task_wrr(rq, rq->curr, 0);
}

static void task_tick_wrr(struct rq *rq, struct task_struct *p, int queued)
{
	struct sched_wrr_entity *wrr_se = &p->wrr;
	int cpu = cpu_of(rq);
	if (wrr_se == NULL)
		return;

	if (--p->wrr.time_slice)
		return;

	if (wrr_se->weight > 1) {
		wrr_se->weight--;
		raw_spin_lock(&wrr_info_locks[cpu]);
		my_wrr_info.total_weight[cpu]--;
		raw_spin_unlock(&wrr_info_locks[cpu]);
	}
	wrr_se->time_slice = wrr_se->weight * 10;

	if (wrr_se->run_list.prev != wrr_se->run_list.next) {
		requeue_task_wrr(rq, p, 0);
		set_tsk_need_resched(p);
		return;
	}
}

static bool
yield_to_task_wrr(struct rq *rq, struct task_struct *p, bool preempt)
{
	return true;
}

static void
prio_changed_wrr(struct rq *rq, struct task_struct *p, int oldprio)
{
}
/*
 * Update the current task's runtime statistics. Skip current tasks that
 * are not in our scheduling class.
 */
const struct sched_class wrr_sched_class = {
	.next			= &fair_sched_class,
	.enqueue_task		= enqueue_task_wrr,
	.dequeue_task		= dequeue_task_wrr,
	.check_preempt_curr	= check_preempt_curr_wrr,
	.pick_next_task		= pick_next_task_wrr,
	.put_prev_task		= put_prev_task_wrr,
	.yield_task		= yield_task_wrr,
	.yield_to_task		= yield_to_task_wrr,

#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_wrr,
	.prio_changed		= prio_changed_wrr,
#endif

	.set_curr_task          = set_curr_task_wrr,
	.task_tick		= task_tick_wrr,
	.switched_to		= switched_to_wrr,
};
