#include "relay.h"
#include "hal/gpio.h"
#include "hal/printf_selector.h"
#include "hal/tasks.h"
#include <stddef.h>

extern uint16_t relay_pulse_ms;
extern uint16_t pulse_delay_ms;

#define RELAY_PULSE_QUEUE_SIZE    16

typedef struct {
    relay_t *relay;
    uint8_t  on;
} pulse_request_t;

static pulse_request_t pulse_queue[RELAY_PULSE_QUEUE_SIZE];
static uint8_t         queue_head  = 0;
static uint8_t         queue_tail  = 0;
static uint8_t         queue_count = 0;

static uint8_t pulse_allowed = 1;

static hal_task_t pulse_end_task   = { 0 };
static hal_task_t pulse_delay_task = { 0 };

static void try_start_next_pulse(void);

static uint8_t pulse_queue_push(relay_t *relay, uint8_t on) {
    if (queue_count >= RELAY_PULSE_QUEUE_SIZE) {
        printf("pulse_queue_push: pulse queue full\r\n");
        return 0;
    }

    pulse_queue[queue_tail].relay = relay;
    pulse_queue[queue_tail].on    = on;

    queue_tail = (queue_tail + 1) % RELAY_PULSE_QUEUE_SIZE;
    queue_count++;

    return 1;
}

static pulse_request_t pulse_queue_pop(void) {
    pulse_request_t request = pulse_queue[queue_head];

    queue_head = (queue_head + 1) % RELAY_PULSE_QUEUE_SIZE;
    queue_count--;

    return request;
}

static void pulse_delay_expired(void *arg) {
    pulse_allowed = 1;
    try_start_next_pulse();
}

static void pulse_end(void *arg) {
    relay_t *relay = (relay_t *)arg;

    hal_gpio_write(relay->pin, !relay->on_high);
    hal_gpio_write(relay->off_pin, !relay->on_high);

    try_start_next_pulse();
}

static void try_start_next_pulse(void) {
    pulse_request_t request;
    relay_t *       relay;
    hal_gpio_pin_t  pin;

    if (!pulse_allowed || queue_count == 0)
        return;

    request = pulse_queue_pop();
    relay   = request.relay;

    pin = request.on ? relay->pin : relay->off_pin;

    hal_gpio_write(pin, relay->on_high);

    pulse_end_task.arg = relay;
    hal_tasks_schedule(&pulse_end_task, relay_pulse_ms);

    if (pulse_delay_ms) {
        uint16_t delay = (pulse_delay_ms != (uint16_t)-1) ? pulse_delay_ms : relay_pulse_ms;
        pulse_allowed = 0;
        hal_tasks_schedule(&pulse_delay_task, delay);
    }
}

void relay_init(relay_t *relay) {
    // Turn off all pins
    hal_gpio_write(relay->pin, !relay->on_high);
    if (relay->is_latching) {
        hal_gpio_write(relay->off_pin, !relay->on_high);

        if (pulse_end_task.handler == NULL) {
            pulse_end_task.handler = pulse_end;
            hal_tasks_init(&pulse_end_task);
        }

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
        if (pulse_queue_push(relay, 1))
            try_start_next_pulse();
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
        if (pulse_queue_push(relay, 0))
            try_start_next_pulse();
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
