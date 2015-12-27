/*
 * State Notifier Driver
 *
 * Copyright (c) 2013-2015, Pranav Vashi <neobuddy89@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/state_notifier.h>
#include <linux/notifier.h>
#include <linux/export.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/delay.h>

#define DEFAULT_SUSPEND_DEFER_TIME	10
#define STATE_NOTIFIER			"state_notifier"

/*
 * debug = 1 will print all
 */
static unsigned int debug = 1;
module_param_named(debug_mask, debug, uint, 0644);

static bool state_suspended;
module_param_named(state_suspended, state_suspended, bool, 0444);

#define dprintk(msg...)		\
do {				\
	if (debug)		\
		pr_info(msg);	\
} while (0)

#ifdef CONFIG_MACH_JF
static unsigned int suspend_defer_time = 1;
#else
static unsigned int suspend_defer_time = DEFAULT_SUSPEND_DEFER_TIME;
#endif
module_param_named(suspend_defer_time, suspend_defer_time, uint, 0664);
static struct delayed_work suspend_work;
static struct workqueue_struct *susp_wq;
struct work_struct resume_work;
static bool suspend_in_progress;

static BLOCKING_NOTIFIER_HEAD(state_notifier_list);

/**
 *	state_register_client - register a client notifier
 *	@nb: notifier block to callback on events
 */
int state_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&state_notifier_list, nb);
}
EXPORT_SYMBOL(state_register_client);

/**
 *	state_unregister_client - unregister a client notifier
 *	@nb: notifier block to callback on events
 */
int state_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&state_notifier_list, nb);
}
EXPORT_SYMBOL(state_unregister_client);

/**
 *	state_notifier_call_chain - notify clients on state_events
 *	@val: Value passed unmodified to notifier function
 *	@v: pointer passed unmodified to notifier function
 *
 */
int state_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&state_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(state_notifier_call_chain);

static void _suspend_work(struct work_struct *work)
{
	state_notifier_call_chain(STATE_NOTIFIER_SUSPEND, NULL);
	state_suspended = true;
	suspend_in_progress = false;
	dprintk("%s: suspend completed.\n", STATE_NOTIFIER);
}

static void _resume_work(struct work_struct *work)
{
	state_notifier_call_chain(STATE_NOTIFIER_ACTIVE, NULL);
	msleep_interruptible(50);
	state_suspended = false;
	dprintk("%s: resume completed.\n", STATE_NOTIFIER);
}

void state_suspend(void)
{
	if (state_suspended || suspend_in_progress)
		return;

	dprintk("%s: suspend called.\n", STATE_NOTIFIER);
	suspend_in_progress = true;

	queue_delayed_work_on(0, susp_wq, &suspend_work, 
#ifdef CONFIG_MACH_JF
		msecs_to_jiffies(suspend_defer_time * 100));
#else
		msecs_to_jiffies(suspend_defer_time * 1000));
#endif
}

void state_resume(void)
{
	dprintk("%s: resume called.\n", STATE_NOTIFIER);
	cancel_delayed_work_sync(&suspend_work);
	suspend_in_progress = false;

	if (state_suspended)
		queue_work_on(0, susp_wq, &resume_work);
}

static int __init state_notifier_init(void)
{
	susp_wq = create_singlethread_workqueue("state_susp_wq");

	if (!susp_wq) {
		pr_err("State Notifier failed to allocate suspend workqueue\n");
		return 0;
	}

	INIT_DELAYED_WORK(&suspend_work, _suspend_work);
	INIT_WORK(&resume_work, _resume_work);

	return 0;
}

subsys_initcall(state_notifier_init);

MODULE_AUTHOR("Pranav Vashi <neobuddy89@gmail.com>");
MODULE_DESCRIPTION("State Notifier Driver");
MODULE_LICENSE("GPLv2");
