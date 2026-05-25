/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-02-25     CYFS         first version
 */
#include <stdio.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "gd32vw55x_platform.h"
#include "wrapper_os.h"
#include "wifi_management.h"
#include "wifi_init.h"
#include "wifi_netlink.h"
#include "user_setting.h"
#include "util.h"

#ifdef RT_USING_WIFI
#define START_TASK_STACK_SIZE       512
#define START_TASK_PRIO             4

static volatile int gd32_wifi_ready_flag;

int gd32vw55x_wifi_ready(void)
{
    return gd32_wifi_ready_flag;
}

static void application_init(void)
{
    int ret;

    gd32_wifi_ready_flag = 0;
    util_init();

    user_setting_init();

    ret = wifi_init();
    if (ret)
    {
        rt_kprintf("wifi init failed: %d\r\n", ret);
        return;
    }

    ret = wifi_wait_ready();
    if (ret)
    {
        rt_kprintf("wifi wait ready failed: %d\r\n", ret);
    } else {
        wifi_netlink_dbg_open();
        /* rt_kprintf("wifi init success\r\n"); */
    }

    /* set wifi work mode */
    rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_STATION);
    rt_wlan_set_mode(RT_WLAN_DEVICE_AP_NAME, RT_WLAN_AP);
#ifdef RT_USING_NETDEV
    rt_thread_mdelay(1000);
    extern int wifi_netdev_register(void);
    wifi_netdev_register();
#endif /* RT_USING_NETDEV */
    gd32_wifi_ready_flag = 1;
    rt_kprintf("wifi init success\r\n");
}

static void start_task(void *param)
{
    (void)param;

    rt_kprintf("wifi start task running\r\n");
    application_init();

    while (1)
    {
        rt_thread_mdelay(1000);
    }
}

#endif /* RT_USING_WIFI */

int wifi_app_init(void)
{

#ifdef RT_USING_WIFI
    rt_thread_t thread;

    rt_kprintf("wifi app init start\r\n");
    thread = rt_thread_create("start_task", start_task, RT_NULL,
                              START_TASK_STACK_SIZE * sizeof(rt_uint32_t),
                              START_TASK_PRIO, 20);
    if (thread == RT_NULL)
    {
        rt_kprintf("Create start task failed\r\n");
        return -RT_ERROR;
    }
    rt_thread_startup(thread);
#endif /* RT_USING_WIFI */

    return RT_EOK;
}

INIT_APP_EXPORT(wifi_app_init);
