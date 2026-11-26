#include "relay.h"
#include "hal/gpio.h"
#include "hal/printf_selector.h"
#include "hal/tasks.h"
#include <stddef.h>

extern uint16_t relay_pulse_ms;
extern uint16_t pulse_delay_ms;

static hal_task_t pulse_delay_task = { 0 };
static uint8_t    pulse_allowed    = 1;

static void relay_start_latching_pulse(relay_t *relay);
static void relay_end_latching_pulse(relay_t *relay);

static void pulse_delay_expired(void *arg) {
    pulse_allowed = 1;
}

static void relay_end_latching_pulse(relay_t *relay) {
    hal_gpio_write(relay->pin, !relay->on_high);
    hal_gpio_write(relay->off_pin, !relay->on_high);
}

static void relay_start_latching_pulse(relay_t *relay) {
    hal_gpio_pin_t pin   = relay->on ? relay->pin : relay->off_pin;
    uint16_t       delay = (pulse_delay_ms != (uint16_t)-1) ? pulse_delay_ms : relay_pulse_ms;

    if (pulse_allowed) {
        // Start new pulse
        hal_gpio_write(pin, relay->on_high);
        relay->latching_task.handler = (task_handler_t)relay_end_latching_pulse;
        hal_tasks_schedule(&relay->latching_task, relay_pulse_ms);

        if (pulse_delay_ms) {
            pulse_allowed = 0;
            hal_tasks_schedule(&pulse_delay_task, delay);
        }
    } else {
        printf("relay_start_latching_pulse: another pulse is active\r\n");
        relay->latching_task.handler = (task_handler_t)relay_start_latching_pulse;
        hal_tasks_schedule(&relay->latching_task, delay);
    }
}

void relay_init(relay_t *relay) {
    relay->latching_task.arg = relay;
    hal_tasks_init(&relay->latching_task);

    // Turn off all pins
    hal_gpio_write(relay->pin, !relay->on_high);
    if (relay->is_latching) {
        hal_gpio_write(relay->off_pin, !relay->on_high);

        if (pulse_delay_task.handler == NULL) {
            pulse_delay_task.handler = pulse_delay_expired;
            hal_tasks_init(&pulse_delay_task);
        }
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
