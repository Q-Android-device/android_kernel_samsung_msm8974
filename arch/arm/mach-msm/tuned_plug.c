/*
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/* tuned hotplugger by fbs (heiler.bemerguy@gmail.com) */

#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/rq_stats.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/lcd_notify.h>

static struct notifier_block lcd_notif;
static struct workqueue_struct *tunedplug_wq;
static struct delayed_work tunedplug_work;

static unsigned int tunedplug_active = 1;
module_param(tunedplug_active, uint, 0644);

static unsigned long sampling_time;

#define DEF_SAMPLING msecs_to_jiffies(20)
#define MAX_SAMPLING msecs_to_jiffies(2000)


bool displayon = true;

static int down[NR_CPUS-1];
/* 123 are cpu cores. 0 will be used for flags */

static void inline down_one(void){
	unsigned int i;
	for (i = NR_CPUS-1; i > 0; i--) {
		if (cpu_online(i)) {
			if (down[i] > 300) {
				cpu_down(i);
				pr_info("tunedplug: DOWN cpu %d. sampling: %lu", i, sampling_time);
				down[i]=0;
			}
			else down[i]++;
			return;
		}
	}
}

static void inline up_one(void){
        unsigned int i;
        for (i = 1; i < NR_CPUS; i++) {
                if (!cpu_online(i)) {
			if (down[i] < -5) {
				struct cpufreq_policy policy;

				pr_info("tunedplug: UP cpu %d. sampling: %lu", i, sampling_time);

				cpu_up(i);

	                        if (cpufreq_get_policy(&policy, i) != 0)
        	                        pr_info("tunedplug: no policy for cpu %d ?", i);
				else
					policy.cur = policy.max;

				down[i]=-100;
			}
			else down[i]--;
			return;
                }
        }
}

static void tunedplug_work_fn(struct work_struct *work)
{
	unsigned int nr_cpus, i;
	struct cpufreq_policy policy;

	queue_delayed_work_on(0, tunedplug_wq, &tunedplug_work, sampling_time);

        if (!tunedplug_active)
                return;

	if (!displayon && (sampling_time < MAX_SAMPLING))
		sampling_time++;

	nr_cpus = num_online_cpus();
	down[0]=0;

	//up
	if (nr_cpus < NR_CPUS) {
		for_each_online_cpu(i) {
			if (cpufreq_get_policy(&policy, i) != 0)
				continue;
			if (i < NR_CPUS-1 && (policy.cur >= policy.max)) {
				up_one();
				down[0]=3;
				break;
			}
		}
	}

	//down
	if (down[0]!=3 && nr_cpus > 1) {
                for_each_online_cpu(i) {
                        if (!i || cpufreq_get_policy(&policy, i) != 0)
                                continue;
                        if (policy.cur <= policy.min)
				down[0] = 1;
			else if (policy.cur >= policy.max)
				down[0] = 2;
                }
		if (down[0]==1) down_one();
	}

}
static int lcd_notifier_callback(struct notifier_block *this,
				unsigned long event, void *data)
{
        switch (event)
        {
                case LCD_EVENT_OFF_END:
			displayon = false;
                        break;

                case LCD_EVENT_ON_START:
			displayon = true;
	                sampling_time = DEF_SAMPLING;
                        break;

                default:
                        break;
        }
        return 0;
}

static void initnotifier(void)
{
        lcd_notif.notifier_call = lcd_notifier_callback;
        if (lcd_register_client(&lcd_notif) != 0)
                pr_err("%s: Failed to register lcd callback\n", __func__);

}

static int __init tuned_plug_init(void)
{

	tunedplug_wq = alloc_workqueue("tunedplug", WQ_HIGHPRI, 1);
        if (!tunedplug_wq)
                return -ENOMEM;

        INIT_DELAYED_WORK(&tunedplug_work, tunedplug_work_fn);

	sampling_time = DEF_SAMPLING;

        queue_delayed_work_on(0, tunedplug_wq, &tunedplug_work, 1);

	initnotifier();

	return 0;
}

MODULE_AUTHOR("Heiler Bemerguy <heiler.bemerguy@gmail.com>");
MODULE_DESCRIPTION("'tuned_plug' - A simple cpu hotplug driver");
MODULE_LICENSE("GPL");

late_initcall(tuned_plug_init);
