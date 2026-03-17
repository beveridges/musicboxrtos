#ifndef BUTTON_ISR_H
#define BUTTON_ISR_H

#include <FreeRTOS.h>
#include <task.h>

void button_isr_attach(TaskHandle_t midi_task);

#endif
