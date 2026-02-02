#include "relay.h"
#include "hal/gpio.h"
#include "hal/printf_selector.h"
#include "hal/tasks.h"
#include <stddef.h>

#ifndef RELAY_PULSE_MS
#define RELAY_PULSE_MS    50
#endif

#ifndef PULSE_WAIT_END_MS
#define PULSE_WAIT_END_MS    50
#endif

#ifndef PULSE_DELAY_MS
#define PULSE_DELAY_MS    500
#endif

extern uint8_t allow_simultaneous_latching_pulses;

static relay_t *pulse_relay = NULL;

static uint8_t    pulse_start_allowed = 1;
static hal_task_t pulse_delay_task;
static uint8_t    pulse_delay_task_init = 0;

static void relay_start_latching_pulse(relay_t *relay);
static void relay_end_latching_pulse(relay_t *relay);

static void pulse_delay_expired(void *arg) {
    pulse_start_allowed = 1;
}

static void relay_end_latching_pulse(relay_t *relay) {
    hal_gpio_write(relay->pin, !relay->on_high);
    hal_gpio_write(relay->off_pin, !relay->on_high);
    if (pulse_relay == relay) {
        // If this relay had a pending pulse, mark it as cleared
        pulse_relay = NULL;
    }
}

static void relay_start_latching_pulse(relay_t *relay) {
    hal_gpio_pin_t pin = relay->on ? relay->pin : relay->off_pin;

    if ((pulse_relay == NULL || allow_simultaneous_latching_pulses) && pulse_start_allowed) {
        // Start new pulse
        hal_gpio_write(pin, relay->on_high);
        pulse_relay = relay;
        relay->latching_task.handler = (task_handler_t)relay_end_latching_pulse;
        hal_tasks_schedule(&relay->latching_task, RELAY_PULSE_MS);

        if (!allow_simultaneous_latching_pulses) {
            pulse_start_allowed      = 0;
            pulse_delay_task.handler = pulse_delay_expired;
            hal_tasks_schedule(&pulse_delay_task, PULSE_DELAY_MS);
        }
    } else {
        printf("relay_start_latching_pulse: another pulse is active\r\n");
        relay->latching_task.handler = (task_handler_t)relay_start_latching_pulse;
        hal_tasks_schedule(&relay->latching_task, PULSE_WAIT_END_MS);
    }
}

void relay_init(relay_t *relay) {
    if (!pulse_delay_task_init) {
        hal_tasks_init(&pulse_delay_task);
        pulse_delay_task_init = 1;
    }

    relay->latching_task.arg = relay;
    hal_tasks_init(&relay->latching_task);

    // Turn off all pins
    hal_gpio_write(relay->pin, !relay->on_high);
    if (relay->is_latching) {
        hal_gpio_write(relay->off_pin, !relay->on_high);
    }
}

void relay_on(relay_t *relay) {
    if (relay == NULL) {
        return;
    }
    printf("relay_on\r\n");

    relay->on = 1;
    if (!relay->is_latching) {
        // Normal relay: drive continuously
        hal_gpio_write(relay->pin, relay->on_high);
    } else {
        // Bi-stable relay
        relay_end_latching_pulse(relay);
        hal_tasks_unschedule(&relay->latching_task);
        relay_start_latching_pulse(relay);
    }

    if (relay->on_change != NULL) {
        relay->on_change(relay->callback_param, 1);
    }
}

void relay_off(relay_t *relay) {
    if (relay == NULL) {
        return;
    }
    printf("relay_off\r\n");

    relay->on = 0;
    if (!relay->is_latching) {
        // Normal relay:  drive continuously
        hal_gpio_write(relay->pin, !relay->on_high);
    } else {
        // Bi-stable relay
        relay_end_latching_pulse(relay);
        hal_tasks_unschedule(&relay->latching_task);
        relay_start_latching_pulse(relay);
    }

    if (relay->on_change != NULL) {
        relay->on_change(relay->callback_param, 0);
    }
}

void relay_toggle(relay_t *relay) {
    if (relay == NULL) {
        return;
    }
    printf("relay_toggle\r\n");

    if (relay->on) {
        relay_off(relay);
    } else {
        relay_on(relay);
    }
}
