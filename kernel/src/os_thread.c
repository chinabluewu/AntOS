/*------------------------------------------------------------------------
|                            FILE DESCRIPTION                            |
------------------------------------------------------------------------*/
/*------------------------------------------------------------------------
|  - File name     : os_thread.c
|  - Author        : zevorn
|  - Update date   : 2021.09.12
|  - Copyright(C)  : 2021-2021 zevorn. All rights reserved.
------------------------------------------------------------------------*/
/*------------------------------------------------------------------------
|                            COPYRIGHT NOTICE                            |
------------------------------------------------------------------------*/
/*
 * Copyright (C) 2021, zevorn (zevorn@yeah.net)

 * This file is part of Ant Real Time Operating System.

 * Ant Real Time Operating System is free software: you can redistribute
 * it and/or modify it under the terms of the Apache-2.0 License.

 * Ant Real Time Operating System is distributed in the hope that it will
 * be useful,but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Apache-2.0 License License for more details.

 * You should have received a copy of the Apache-2.0 License.Ant Real Time
 * Operating System. If not, see <http://www.apache.org/licenses/>.
**/
/*------------------------------------------------------------------------
|                                INCLUDES                                |
------------------------------------------------------------------------*/

#include "os_thread.h"
#include "os_mem.h"
#include "os_cpu.h"
#include "os_core.h"

/*------------------------------------------------------------------------
|                                  DATA                                  |
------------------------------------------------------------------------*/

/**
 * @brief The thread node that is running.
 **/
os_thread_t *g_thrd_run_node;

/**
 * @brief   Thread ready linked list queue.
 **/
struct os_thread_list g_thrd_rdylist;

/**
 * @brief   Thread sleep linked list queue.
 **/
struct os_thread_list g_thrd_slplist;

/**
 * @brief   The thread blocks the linked list queue.
 **/
struct os_thread_list g_thrd_blklist;

/**
 * @brief Idle thread stack.
 **/
os_thread_stk g_idle_stack[OS_IDLE_THREAD_STK_SIZE];

/**
 * @brie The pointer handle of the idle thread.
 **/
extern os_thread_t g_idle_thread_handle;

/*------------------------------------------------------------------------
|                              API FUNCTIONS                             |
------------------------------------------------------------------------*/

extern void os_exce_handle(os_uint8_t excCode);

/**
 * @brief  Replaces an element on the blocking list
 * @param[in]  index1  The array index of the element 1 to be replaced
 * @param[in]  index2  The index of an array with element 2
 * @return  None
 **/
void os_blklist_replace(os_uint8_t index1, os_uint8_t index2)
{
    g_thrd_blklist.node[index1] = g_thrd_blklist.node[index2];
    g_thrd_blklist.node[index2] = OS_NULL;
}

/**
 * @brief  Replaces an element on the ready list
 * @param[in]  index1  The array index of the element 1 to be replaced
 * @param[in]  index2  The index of an array with element 2
 * @return  none
 **/
void os_rdylist_replace(os_uint8_t index1, os_uint8_t index2)
{
    g_thrd_rdylist.node[index1] = g_thrd_rdylist.node[index2];
    g_thrd_rdylist.node[index2] = OS_NULL;
}

/**
 * @brief  delete an element on the ready list
 * @param[in]  thread  Pointer to the thread handle to be deleted
 * @return  none
 **/
static void os_rdylist_delete(os_thread_t *thread)
{
    os_uint8_t index;
    /** Delete linked list node. */
    for (index = 0; index < g_thrd_rdylist.num; index++)
    {
        if (thread == (os_thread_t *)g_thrd_rdylist.node[index])
        {
            os_rdylist_replace(index, --g_thrd_rdylist.num);
            return;
        }
    }
}

/**
 * @brief  delete an element on the sleep list
 * @param[in]  thread  Pointer to the thread handle to be deleted
 * @return  none
 **/
static void os_slplist_delete(os_thread_t *thread)
{
    os_uint8_t index;
    for (index = 0; index < OS_THREAD_REAL_LEN; index++)
    {
        if (thread == (os_thread_t *)g_thrd_slplist.node[index])
        {
            g_thrd_slplist.node[index] = OS_NULL;
            g_thrd_slplist.num--;
            break;
        }
    }
}

/**
 * @brief  delete an element on the block list
 * @param[in]  thread  Pointer to the thread handle to be deleted
 * @return  none
 **/
static void os_blklist_delete(os_thread_t *thread)
{
    os_uint8_t index;
    /** Delete linked list node. */
    for (index = 0; index < OS_THREAD_REAL_LEN; index++)
    {
        if (thread == g_thrd_blklist.node[index])
        {
            os_blklist_replace(index, --g_thrd_blklist.num);
            break;
        }
    }
}

/**
 * @brief     Get the thread with the highest priority in the ready thread list
 * @param[in] max_prio_label  The thread label with the highest priority
 * @retval    [os_thread_t]  Update completed
 * @retval    [OS_NULL]  The update failed or the ready thread list is empty
 **/
os_thread_t os_rdylist_get_max_prio(os_uint8_t *max_prio_label)
{
    os_uint8_t index;
    os_uint8_t selected;
    os_uint8_t max_prio;

    if (g_thrd_rdylist.num == 0)
    {
        if (max_prio_label != OS_NULL)
            *max_prio_label = 0;
        return OS_NULL;
    }

    /* 以第一个就绪节点作基线，否则 priority==0 的 idle 线程永远比不上初始 max_prio=0，
     * 当 idle 不在 node[0] 时会返回错误下标。 */
    selected = 0;
    max_prio = ((os_thread_t)*g_thrd_rdylist.node[0])->priority;

    for (index = 1; index < g_thrd_rdylist.num; index++)
    {
        if (((os_thread_t)*g_thrd_rdylist.node[index])->priority > max_prio)
        {
            max_prio = ((os_thread_t)*g_thrd_rdylist.node[index])->priority;
            selected = index;
        }
    }

    if (max_prio_label != OS_NULL)
        *max_prio_label = selected;
    return g_thrd_rdylist.node[selected];
}

/**
 * @brief     Update the thread list
 * @param[in] thread handle
 * @return    none
 **/
void os_update_list(os_thread_t *thread, os_uint8_t status)
{
    os_uint8_t index = 0;

    (*thread)->status = status;
    switch (status)
    {
        case OS_THREAD_READY:
        {
            (os_thread_t *)g_thrd_rdylist.node[g_thrd_rdylist.num++] = thread;
            break;
        }
        case OS_THREAD_SLEEP:
        {
            for (; index < OS_THREAD_REAL_LEN; index++)
            {
                if (g_thrd_slplist.node[index] == OS_NULL)
                {
                    (os_thread_t *)g_thrd_slplist.node[index] = thread;
                    g_thrd_slplist.num++;
                    break;
                }
            }
            break;
        }
        case OS_THREAD_BLOCK:
        {
            (os_thread_t *)g_thrd_blklist.node[g_thrd_blklist.num++] = thread;
            break;
        }
    }
}

/**
 * @brief     Create a thread macro function statically,
 *            and return the handle address of the thread object if it succeeds
 * @param[in] Thread pointer handle, not repeatable
 * @param[in] Thread entry addres
 * @param[in] Thread entry parameters
 * @param[in] Thread priority
 * @param[in] Thread stack address
 * @param[in] Thread stack size
 * @retval [os_thread_t] The thread is initialized successfully and the address is returned
 * @retval [OS_NULL] Creation failure returns NULL
 * @note   To create a thread statically, the user needs to define a thread stack
 *         and thread control block pointer (optional),The pointer is mainly used for thread control,
 *         such as suspend and delete
 **/
os_thread_t os_thread_static_create(os_thread_t *pthread,
                                    void (*entry)(void *param),
                                    void *param,
                                    os_uint8_t priority,
                                    os_thread_stk_t stk_addr,
                                    os_uint8_t stk_size)
{
    os_uint8_t index = os_object_get_thrd_num();
    os_object_t *list = os_object_get_thrd_list();

    /* 线程数量上限保护：OS_THREAD_REAL_LEN 已经包含 idle（和软件定时器）槽位。 */
    if (index >= OS_THREAD_REAL_LEN)
        goto erro;

    /* 优先级唯一性检查：idle 线程占用 priority=0，用户线程必须各不相同。 */
    while (index)
    {
        if ((*((os_thread_t *)list[--index]))->priority == priority)
            goto erro;
    }

    OS_ENTER_CRITICAL(); /* RTOS enters the critical area of the system */

    *pthread = os_malloc(pthread, sizeof(struct os_thread));
    if (*pthread == OS_NULL)
    {
        /* malloc 失败时必须先退出临界区，否则会出现 lock 计数泄漏。 */
        OS_EXIT_CRITICAL();
        goto erro;
    }

    os_object_thread_init((os_object_t *)pthread, OS_Object_Class_Thread);

    (*pthread)->entry = entry;
    (*pthread)->stk_addr = stk_addr;
    (*pthread)->stk_size = stk_size;
    (*pthread)->priority = priority;
    (*pthread)->status = OS_THREAD_READY;

    /** 栈顶第 0 字节存放后续要拷贝到内核栈的字节数（不含自身）。
     * 总帧大小 = 2(入口PC低/高) + 寄存器帧。常量定义在 os_cpu.h。 */
    *(stk_addr++) = OS_CTX_FRAME_SIZE;

    /** 入口地址低 8 位在前，高 8 位在后；调度恢复后 RET 弹出该 PC。 */
    *stk_addr++ = (os_uint16_t)entry;
    *stk_addr = (os_uint16_t)entry >> 8;

    /** 寄存器帧（按 PUSH 顺序）：ACC, B, DPH, DPL, PSW, AR0, AR4, AR5, AR6, AR7, AR1, AR2, AR3。
     *  其中 AR1/AR2/AR3 用于 Keil C51 generic 指针传参（R1=低字节，R2=高字节，R3=内存类型），
     *  其余 (OS_CTX_REG_COUNT - 3) 个寄存器清零。 */
    for (index = 0; index < (OS_CTX_REG_COUNT - 3); index++)
    {
        *(++stk_addr) = 0;
    }

    /** 注意：必须用 pre-increment，否则会把 AR1 写到 AR7 槽位、AR3 留为未初始化。 */
    *(++stk_addr) = (os_uint32_t)param;       /*!< AR1 = R1 (低) */
    *(++stk_addr) = (os_uint32_t)param >> 8;  /*!< AR2 = R2 (高) */
    *(++stk_addr) = (os_uint32_t)param >> 16; /*!< AR3 = R3 (内存类型) */

    (os_thread_t *)g_thrd_rdylist.node[g_thrd_rdylist.num++] = pthread;

    OS_EXIT_CRITICAL();
    if (g_thrd_run_node != OS_NULL)
        os_thread_schedule(OS_THREAD_READY);
    return *pthread;

erro:
    return OS_NULL;
}

/**
 * @brief Create a thread, and return the handle address of the thread object if it succeeds
 * @param[in] thread   Thread pointer handle, not repeatable
 * @param[in] entry    Thread entry address
 * @param[in] param    Thread entry parameters
 * @param[in] priority Thread priority
 * @param[in] stk_addr Thread stack address
 * @param[in] stk_size Thread stack size
 * @retval [os_thread_t] The thread is initialized successfully and the address is returned
 * @retval [OS_NULL]     The thread initialization failed
 * @note To dynamically create threads, users need to define a thread stack pointer
 *       and thread control block pointer, The pointer is mainly used for thread control,
 *       such as suspend and delete
 **/
os_thread_t os_thread_create(os_thread_t *pthread,
                             void (*entry)(void *param),
                             void *param,
                             os_uint8_t priority,
                             os_thread_stk_t *stk_addr,
                             os_uint8_t stk_size)
{
    *stk_addr = (os_uint8_t *)os_malloc(stk_addr, stk_size);
    return os_thread_static_create(pthread, entry, param, priority, *stk_addr, stk_size);
}

/**
 * @brief When a thread is hibernated, the hibernated thread voluntarily surrenders
 *        the right to use the CPU
 * @param[in] ticks The number of system beats
 * @retval [OS_OK]  The thread is sleep successfully
 * @retval [OS_NOK] Thread sleep failure
 **/
os_err_t os_thread_sleep(os_uint16_t ticks)
{
    /* RTOS enters the critical area of the system. */
    (*g_thrd_run_node)->slpctr = ticks;
    os_thread_schedule(OS_THREAD_SLEEP);
    return OS_OK;
}

/**
 * @brief The thread gives up the right to use the CPU and enters the ready state.
 * @param[in] none
 * @retval [OS_OK]  Thread yields successfully.
 * @retval [OS_NOK] Thread yield failure.
 * @note If the priority of the operated thread is the highest in the ready queue,
 *       it will be scheduled again.
 **/
os_err_t os_thread_yield(void)
{
    os_thread_schedule(OS_THREAD_READY);
    return OS_OK;
}

/**
 * @brief Suspend a process. This process can be itself or other threads
 * @param[in] thread Thread to be suspended
 * @retval [OS_OK]  The thread is suspended successfully
 * @retval [OS_NOK] The thread suspend failed
 * @note If you suspend itself, it will trigger a task schedule
 **/
os_err_t os_thread_suspend(os_thread_t *thread)
{
    OS_ENTER_CRITICAL();
    switch ((*thread)->status)
    {
        case OS_THREAD_READY:
            os_rdylist_delete(thread);
            break;
        case OS_THREAD_SLEEP:
            os_slplist_delete(thread);
            break;
        case OS_THREAD_RUNNING:
            OS_EXIT_CRITICAL();
            os_thread_schedule(OS_THREAD_BLOCK);
            return OS_OK;
        default:
            OS_EXIT_CRITICAL();
            return OS_NOK;
    }
    os_update_list(thread, OS_THREAD_BLOCK);
    OS_EXIT_CRITICAL();
    return OS_OK;
}

/**
 * @brief Resume a process. This process can be itself or other threads
 * @param[in] thread  Thread to be resumed
 * @retval [OS_OK]  The thread is resumeed successfully
 * @retval [OS_NOK] The thread resume failed
 **/
os_err_t os_thread_resume(os_thread_t *thread)
{
    os_uint8_t index;

    if (thread == OS_NULL)
        return OS_NOK;

    OS_ENTER_CRITICAL();
    for (index = 0; index < g_thrd_blklist.num; index++)
    {
        if ((os_thread_t *)g_thrd_blklist.node[index] == thread)
        {
            /* 先从 block list 移除，再加入 ready list；否则中间窗口内
             * 同一线程会同时挂在两条链表上。 */
            os_blklist_replace(index, --g_thrd_blklist.num);
            os_update_list(thread, OS_THREAD_READY);
            OS_EXIT_CRITICAL();
            return OS_OK;
        }
    }
    OS_EXIT_CRITICAL();
    return OS_NOK;
}

/**
 * @brief Delete a process. This process can be itself or other threads
 * @param[in] thread   Thread pointer handle
 * @param[in] stk_addr Thread stack pointer
 * @retval [OS_OK]  The thread is deleted successfully
 * @retval [OS_NOK] The thread delete failed
 * @note  If you delete itself, it will trigger a task schedule
 **/
os_err_t os_thread_delete(os_thread_t *thread, os_thread_stk_t *stk_addr)
{
    os_uint8_t index = 0;

    if ((*g_thrd_run_node)->entry == g_idle_thread_handle)
        goto erro;

    OS_ENTER_CRITICAL();
    os_object_thread_delete((void *)thread);
    switch ((*thread)->status)
    {
        case OS_THREAD_READY:
            os_rdylist_delete(thread);
            break;
        case OS_THREAD_SLEEP:
            os_slplist_delete(thread);
            break;
        case OS_THREAD_BLOCK:
            os_blklist_delete(thread);
            break;
        case OS_THREAD_RUNNING:
            break;
        default:
            OS_EXIT_CRITICAL();
            return OS_NOK;
    }
    os_mem_send_rec_addr(thread);
    if (stk_addr != OS_NULL)
        os_mem_send_rec_addr(stk_addr);
    OS_EXIT_CRITICAL();

    if (g_thrd_run_node == thread)
        os_thread_schedule(OS_THREAD_BLOCK);
    return OS_OK;

erro:
    return OS_NOK;
}

/*------------------------------------------------------------------------
|                    END OF FLIE.  (C) COPYRIGHT zevorn                  |
------------------------------------------------------------------------*/