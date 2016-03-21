#include <string.h>

#include "nrf.h"
#include "nrf_gpio.h"

#include "hardware.h"
#include "rtos.h"
#include "timer.h"

static RTOS_Task *task_list;
static RTOS_Task *free_task;
static RTOS_Task task_pool[MAX_TASKS];

static int last_counter;

void RTOS::init(void) {
  // Clear out our task management pool
  task_list = NULL;
  free_task = &task_pool[0];

  memset(&task_pool, 0, sizeof(task_pool));
  for (int i = 1; i < MAX_TASKS; i++) {
    task_pool[i-1].next = &task_pool[i];
  }

  // Setup the watchdog
  NRF_WDT->CONFIG = (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos);
  NRF_WDT->CRV = 0x10000 * 60; // 1 minute before watchdog murders your fam
  NRF_WDT->RREN = wdog_channel_mask;
  NRF_WDT->TASKS_START = 1;

  // Manage trigger set
  NVIC_EnableIRQ(SWI0_IRQn);
  NVIC_SetPriority(SWI0_IRQn, RTOS_PRIORITY);
}

void RTOS::kick(uint8_t channel) {
  NRF_WDT->RR[channel] = WDT_RR_RR_Reload;
}

void RTOS::delay(RTOS_Task* task, int delay) {
  task->target += delay;
}

RTOS_Task* RTOS::allocate(void) {
  if (free_task == NULL) {
    return NULL;
  }
  
  RTOS_Task *task = free_task;
  free_task = free_task->next;

  return task;
}

void RTOS::remove(RTOS_Task* task) {
  // Remove task from listing
  if (task->prev) {
    task->prev->next = task->next;
  } else {
    task_list = task->next;
  }
  
  if (task->next) {
    task->next->prev = task->prev;
  }
}
  
void RTOS::release(RTOS_Task* task) {
  RTOS::remove(task);

  // Add to unallocated list
  task->next = free_task;
  free_task = task;
}

static void insert(RTOS_Task* task) {
  // Set task to the first index
  task->prev = NULL;
  task->next = task_list;
  
  // Shift our task down the chain
  while (task->next && task->next->priority <= task->priority) {
    task->prev = task->next;
    task->next = task->next->next;
  }

  // Insert the task into the chain
  if (task->prev) {
    task->prev->next = task;
  } else {
    task_list = task;
  }

  if (task->next) {
    task->next->prev = task;
  }
}

void RTOS::setPriority(RTOS_Task* task, RTOS_Priority priority) {
  remove(task);
  task->priority = priority;
  insert(task);
}

void RTOS::manage(void) {
  NVIC_SetPendingIRQ(SWI0_IRQn);
}

RTOS_Task* RTOS::create(RTOS_TaskProc func, bool repeating) {
  RTOS_Task* task = allocate();

  if (task == NULL) {
    return NULL;
  }

  task->priority = RTOS_DEFAULT_PRIORITY;
  task->task = func;
  task->repeating = repeating;
  task->active = false;

  insert(task);
 
  return task;
}

void RTOS::start(RTOS_Task* task, int period, void* userdata) {
  task->period = period;
  task->target = period;
  task->userdata = userdata;
  task->active = true;
}

void RTOS::stop(RTOS_Task* task) {
  task->active = false;
}

void RTOS::EnterCritical(void) {
  NVIC_DisableIRQ(SWI0_IRQn);
}

void RTOS::LeaveCritical(void) {
  NVIC_EnableIRQ(SWI0_IRQn);
}

RTOS_Task* RTOS::schedule(RTOS_TaskProc func, int period, void* userdata, bool repeating) {
  RTOS_Task* task = create(func, repeating);
  
  if (task == NULL) {
    return NULL;
  }

  start(task, period, userdata);
  
  return task;
}

extern "C" void SWI0_IRQHandler(void) {
  int new_count = GetCounter();
  int ticks = new_count - last_counter;

  last_counter = new_count;
  
  for (RTOS_Task* task = task_list; task; task = task->next) {
    // Resume execution
    if (!task->active) {
      continue ; 
    }

    task->target -= ticks;

    // Current task has not yet fired
    if (task->target > 0) {
      continue ;
    }

    int start = GetCounter();
		task->task(task->userdata);
		int time = GetCounter() - start;
		task->time = MAX(time, task->time);

    // Either release the task slice, or reinsert it with the period
    if (task->repeating) {
			do {
				task->target += task->period;
			} while (task->target <= 0);
    } else {
      RTOS::release(task);
    }
  }

  RTOS::kick(WDOG_RTOS);
}
