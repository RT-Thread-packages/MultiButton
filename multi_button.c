/*
 * @File:    multi_button.c
 * @Author:  Zibin Zheng
 * @Date:    2018-01-23 20:36:01
 *
 * @LICENSE: Copyright (c) 2016 Zibin Zheng <znbin@qq.com>
 *           All rights reserved.
 *
 * @NOTE:    The original author of MultiButton is Zibin Zheng.
 *           Please contact the original author for authorization
 *           before use.I(liu2guang) only adapt the library to
 *           rt-thread and fix some bugs. I(liu2guang) am not
 *           responsible for the authorization of the library.
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-01-23     liu2guang    Adapter rtthread and Fix the bug.
 */
#include "multi_button.h"

#define EVENT_CB(ev) if(handle->cb[ev]) handle->cb[ev]((void*)handle)
#define PRESS_REPEAT_MAX_NUM  15 /*!< The maximum value of the repeat counter */

static button_t head_handle = NULL;

/**
  * @brief  Initializes the button struct handle.
  * @param  handle: the button handle struct.
  * @param  pin_level: read the pin of the connected button level.
  * @param  active_level: pin pressed level.
  * @retval None
  */
void button_init(button_t handle, uint8_t(*pin_level)(void), uint8_t active_level)
{
    memset(handle, 0, sizeof(struct button));
    handle->event = (uint8_t)NONE_PRESS;
    handle->hal_button_Level = pin_level;
    handle->button_level = !active_level;
    handle->active_level = active_level;
    handle->short_ticks = SHORT_TICKS;
    handle->long_ticks = LONG_TICKS;
}

/**
  * @brief  Attach the button event callback function.
  * @param  handle: the button handle struct.
  * @param  event: trigger event type.
  * @param  cb: callback function.
  * @retval None
  */
void button_attach(button_t handle, PressEvent event, BtnCallback cb)
{
    handle->cb[event] = cb;
}

/**
  * @brief  Attach the button adjust ticks
  * @param  handle: the button handle strcut.
  * @param  ticks: judge short ticks(unit:ms)
  * @retval None
  */
void button_set_short_ticks(button_t handle, uint16_t ticks)
{
    handle->short_ticks = ticks / TICKS_INTERVAL;
}

/**
  * @brief  Attach the button adjust long ticks
  * @param  handle: the button handle strcut.
  * @param  ticks: judge long ticks(unit:ms)
  * @retval None
  */
void button_set_long_ticks(button_t handle, uint16_t ticks)
{
    handle->long_ticks = ticks / TICKS_INTERVAL;
}

/**
  * @brief  Inquire the button event happen.
  * @param  handle: the button handle struct.
  * @retval button event.
  */
PressEvent get_button_event(button_t handle)
{
    return (PressEvent)(handle->event);
}

/**
  * @brief  button driver core function, driver state machine.
  * @param  handle: the button handle struct.
  * @retval None
  */
static void button_handler(button_t handle)
{
    uint8_t read_gpio_level = handle->hal_button_Level();

    //ticks counter working..
    if((handle->state) > 0)
    {
        handle->ticks++;
    }

    /*------------button debounce handle---------------*/
    if(read_gpio_level != handle->button_level)
    {
        //not equal to prev one
        //continue read 3 times same new level change
        if(++(handle->debounce_cnt) >= DEBOUNCE_TICKS)
        {
            handle->button_level = read_gpio_level;
            handle->debounce_cnt = 0;
        }
    }
    else
    {
        // level not change ,counter reset.
        handle->debounce_cnt = 0;
    }

    /*-----------------State machine-------------------*/
    switch (handle->state)
    {
    case 0:
        if(handle->button_level == handle->active_level)
        {
            handle->event = (uint8_t)PRESS_DOWN;
            EVENT_CB(PRESS_DOWN);
            handle->ticks  = 0;
            handle->repeat = 1;
            handle->state  = 1;
        }
        else
        {
            handle->event = (uint8_t)NONE_PRESS;
        }
        break;

    case 1:
        if(handle->button_level != handle->active_level)
        {
            handle->event = (uint8_t)PRESS_UP;
            EVENT_CB(PRESS_UP);
            handle->ticks = 0;
            handle->state = 2;
        }
        else if(handle->ticks > handle->long_ticks)
        {
            handle->event = (uint8_t)LONG_PRESS_START;
            EVENT_CB(LONG_PRESS_START);
            handle->state = 5;
        }
        break;

    case 2:
        if(handle->button_level == handle->active_level)
        {
            handle->event = (uint8_t)PRESS_DOWN;
            EVENT_CB(PRESS_DOWN);
            if(handle->repeat != PRESS_REPEAT_MAX_NUM)
            {
                handle->repeat++;
            }
            EVENT_CB(PRESS_REPEAT);
            handle->ticks = 0;
            handle->state = 3;
        }
        else if(handle->ticks > handle->short_ticks)
        {
            if(handle->repeat == 1)
            {
                handle->event = (uint8_t)SINGLE_CLICK;
                EVENT_CB(SINGLE_CLICK);
            }
            else if(handle->repeat == 2)
            {
                handle->event = (uint8_t)DOUBLE_CLICK;
                EVENT_CB(DOUBLE_CLICK);
            }
            handle->state = 0;
        }
        break;

    case 3:
        if(handle->button_level != handle->active_level)
        {
            handle->event = (uint8_t)PRESS_UP;
            EVENT_CB(PRESS_UP);

            if(handle->ticks < handle->short_ticks)
            {
                handle->ticks = 0;
                handle->state = 2;
            }
            else
            {
                handle->state = 0;
            }
        }
        else if(handle->ticks > handle->short_ticks) // SHORT_TICKS < press down hold time < LONG_TICKS
        {
            handle->state = 1;
        }
        break;

    case 5:
        if(handle->button_level == handle->active_level)
        {
            handle->event = (uint8_t)LONG_PRESS_HOLD;
            if (handle->ticks % LONG_HOLD_CYC == 0)
            {
                EVENT_CB(LONG_PRESS_HOLD);
            }
        }
        else
        {
            handle->event = (uint8_t)PRESS_UP;
            EVENT_CB(PRESS_UP);

            handle->state = 0;
        }
        break;

    default:
        handle->state = 0; /* reset */
        break;
    }
}

/**
  * @brief  Start the button work, add the handle into work list.
  * @param  handle: target handle struct.
  * @retval 0: succeed. -1: already exist.
  */
int button_start(button_t handle)
{
    button_t target = head_handle;

    while(target)
    {
        if(target == handle)
        {
            return -1;  //already exist.
        }

        target = target->next;
    }

    handle->next = head_handle;
    head_handle = handle;

    return 0;
}

/**
  * @brief  Stop the button work, remove the handle off work list.
  * @param  handle: target handle struct.
  * @retval None
  */
void button_stop(button_t handle)
{
    button_t* curr;

    for(curr = &head_handle; *curr;)
    {
        button_t entry = *curr;

        if (entry == handle)
        {
            *curr = entry->next;
            return;
        }
        else
        {
            curr = &entry->next;
        }
    }
}

/**
  * @brief  background ticks, timer repeat invoking interval 5ms.
  * @param  None.
  * @retval None
  */
void button_ticks(void)
{
    button_t target;

    for(target = head_handle; target != NULL; target = target->next)
    {
        button_handler(target);
    }
}
