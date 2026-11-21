#include <msp430.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Set to 1 to print step-by-step trace & final benchmark summary to CIO console */
#define ENABLE_DEBUG_PRINT 1

#if ENABLE_DEBUG_PRINT
#include <stdio.h>
#endif

#include "energy.h"
#include "tasks.h"

#define MAX_READY_TASKS 128
#define MSP430F5529_ACTIVE_UW 957UL

typedef struct {
    uint16_t tasks_done;
    uint16_t tasks_missed;
    uint32_t wasted_energy;
    uint32_t scheduling_steps;
    uint32_t final_energy;
    uint32_t cpu_ticks;
    uint32_t exec_us;
    uint32_t exec_ms;
    uint32_t energy_uj;
} sched_result_t;

typedef struct {
    task_t data[MAX_READY_TASKS];
    uint16_t front;
    uint16_t rear;
    uint16_t count;
} circular_queue_t;

static circular_queue_t ready_queue;
static sched_result_t res_queue;
static volatile uint32_t timer_overflows = 0;

#pragma vector = TIMER0_A1_VECTOR
__interrupt void TIMER0_A1_ISR(void) {
    if (TA0IV == TA0IV_TAIFG) {
        timer_overflows++;
    }
}

static void timer_start(void) {
    timer_overflows = 0;
    TA0CTL = TASSEL__SMCLK | ID__8 | MC__CONTINUOUS | TACLR | TAIE;
    __enable_interrupt();
}

static uint32_t timer_stop_ticks(void) {
    TA0CTL = MC__STOP;
    return (timer_overflows << 16) | (uint32_t)TA0R;
}

static void compute_hardware_metrics(sched_result_t *res) {
    res->exec_us = (res->cpu_ticks * 15625UL) >> 11;
    res->exec_ms = res->exec_us / 1000UL;
    res->energy_uj = (res->exec_us / 1000UL) * MSP430F5529_ACTIVE_UW / 1000UL;
    if (res->energy_uj == 0 && res->exec_us > 0) {
        res->energy_uj = (res->exec_us * MSP430F5529_ACTIVE_UW) / 1000000UL;
    }
}

#if ENABLE_DEBUG_PRINT
static void print_u32(uint32_t val) {
    if (val >= 10000UL) {
        uint16_t high = (uint16_t)(val / 10000UL);
        uint16_t low  = (uint16_t)(val % 10000UL);
        printf("%d", (int)high);
        if (low < 1000) printf("0");
        if (low < 100)  printf("0");
        if (low < 10)   printf("0");
        printf("%d", (int)low);
    } else {
        printf("%d", (int)val);
    }
}
#endif

static void queue_init(circular_queue_t *q) {
    q->front = 0;
    q->rear = 0;
    q->count = 0;
}

static bool queue_is_empty(const circular_queue_t *q) {
    return q->count == 0;
}

static bool queue_is_full(const circular_queue_t *q) {
    return q->count >= MAX_READY_TASKS;
}

static bool queue_enqueue(circular_queue_t *q, task_t task) {
    if (queue_is_full(q)) return false;
    q->data[q->rear] = task;
    q->rear = (q->rear + 1) % MAX_READY_TASKS;
    q->count++;
    return true;
}

static bool queue_dequeue(circular_queue_t *q, task_t *out) {
    if (queue_is_empty(q)) return false;
    if (out) *out = q->data[q->front];
    q->front = (q->front + 1) % MAX_READY_TASKS;
    q->count--;
    return true;
}

static bool queue_peek(const circular_queue_t *q, task_t *out) {
    if (queue_is_empty(q)) return false;
    if (out) *out = q->data[q->front];
    return true;
}

static void run_student_a_queue(uint32_t max_steps_to_trace) {
    uint32_t step;
    uint32_t energy_buffer = 0;
    uint16_t next_idx = 0;
    task_t front_task, rem;

    res_queue.tasks_done = 0;
    res_queue.tasks_missed = 0;
    res_queue.wasted_energy = 0;
    res_queue.scheduling_steps = 0;
    res_queue.final_energy = 0;
    queue_init(&ready_queue);

    for (step = 0; step < NUM_STEPS; step++) {
#if ENABLE_DEBUG_PRINT
        bool should_print = (step < max_steps_to_trace);
        if (should_print) {
            printf("[Step ");
            print_u32(step);
            printf("] Energy trace input: +%d | Buffer: ", (int)energy_in[step]);
            print_u32(energy_buffer);
        }
#endif

        /* 1. Harvest Energy & Cap Buffer */
        energy_buffer += energy_in[step];
        if (energy_buffer > ENERGY_MAX) {
            uint32_t overflow = energy_buffer - ENERGY_MAX;
            res_queue.wasted_energy += overflow;
            energy_buffer = ENERGY_MAX;
#if ENABLE_DEBUG_PRINT
            if (should_print && overflow > 0) {
                printf(" (Wasted: ");
                print_u32(overflow);
                printf(")");
            }
#endif
        }
#if ENABLE_DEBUG_PRINT
        if (should_print) {
            printf("\n");
        }
#endif

        /* 2. Add arriving tasks to ready queue */
        while (next_idx < NUM_TASKS && tasks[next_idx].arrival == step) {
            if (queue_enqueue(&ready_queue, tasks[next_idx])) {
#if ENABLE_DEBUG_PRINT
                if (should_print) {
                    printf("  -> Event: Task ID %d arrived! (Cost: %d, Deadline: %d) [Queue size: %d]\n",
                           (int)tasks[next_idx].id, (int)tasks[next_idx].cost,
                           (int)tasks[next_idx].deadline, (int)ready_queue.count);
                }
#endif
            } else {
#if ENABLE_DEBUG_PRINT
                printf("  -> Error: Ready queue overflow at step ");
                print_u32(step);
                printf("!\n");
#endif
            }
            next_idx++;
        }

        /* 3. Remove expired tasks from front of queue */
        while (queue_peek(&ready_queue, &front_task)) {
            res_queue.scheduling_steps++;
            if (step > front_task.deadline) {
                queue_dequeue(&ready_queue, NULL);
                res_queue.tasks_missed++;
                P1OUT ^= BIT0;
#if ENABLE_DEBUG_PRINT
                if (should_print) {
                    printf("  -> Event: Task ID %d MISSED deadline (%d)! Discarded from front.\n",
                           (int)front_task.id, (int)front_task.deadline);
                }
#endif
            } else {
                break;
            }
        }

        /* 4. Run task at front if enough energy is available */
        if (queue_peek(&ready_queue, &front_task)) {
            res_queue.scheduling_steps++;
            if (energy_buffer >= front_task.cost) {
                energy_buffer -= front_task.cost;
                queue_dequeue(&ready_queue, NULL);
                res_queue.tasks_done++;
                P4OUT ^= BIT7;
#if ENABLE_DEBUG_PRINT
                if (should_print) {
                    printf("  -> Event: EXECUTED Task ID %d! Used %d energy. Remaining Buffer: ",
                           (int)front_task.id, (int)front_task.cost);
                    print_u32(energy_buffer);
                    printf("\n");
                }
#endif
            } else {
#if ENABLE_DEBUG_PRINT
                if (should_print) {
                    printf("  -> Waiting: Task ID %d at front needs %d energy, but buffer has ",
                           (int)front_task.id, (int)front_task.cost);
                    print_u32(energy_buffer);
                    printf(".\n");
                }
#endif
            }
        }
    }

    while (queue_dequeue(&ready_queue, &rem)) {
        res_queue.tasks_missed++;
    }
    res_queue.final_energy = energy_buffer;
}

int main(void) {
    uint32_t steps_to_print = 25;

    WDTCTL = WDTPW | WDTHOLD;

    P1DIR |= BIT0;
    P1OUT &= ~BIT0;
    P4DIR |= BIT7;
    P4OUT &= ~BIT7;

#if ENABLE_DEBUG_PRINT
    printf("============================================================\n");
    printf("   Student A: FCFS Circular Queue (MSP430F5529LP)           \n");
    printf("============================================================\n");
    printf("Showing detailed trace for first ");
    print_u32(steps_to_print);
    printf(" steps...\n\n");
#endif

    /* Pass 1: Detailed trace for first 25 steps */
    run_student_a_queue(steps_to_print);

    /* Pass 2: Untimed pure CPU benchmark (0 printed steps) for accurate timing */
    timer_start();
    run_student_a_queue(0);
    res_queue.cpu_ticks = timer_stop_ticks();
    compute_hardware_metrics(&res_queue);

#if ENABLE_DEBUG_PRINT
    printf("\n============================================================\n");
    printf("   Final Simulation & Hardware Benchmark Summary (Queue)    \n");
    printf("============================================================\n");
    printf("Tasks Completed:          %d\n", (int)res_queue.tasks_done);
    printf("Tasks Missed:             %d\n", (int)res_queue.tasks_missed);
    printf("Wasted Energy (Sim):      "); print_u32(res_queue.wasted_energy);    printf(" units\n");
    printf("Remaining Energy (Sim):   "); print_u32(res_queue.final_energy);     printf(" units\n");
    printf("Total Scheduling Steps:   "); print_u32(res_queue.scheduling_steps); printf(" comparisons\n");
    printf("Timer_A0 Hardware Ticks:  "); print_u32(res_queue.cpu_ticks);        printf(" ticks\n");
    printf("Execution Time (us):      "); print_u32(res_queue.exec_us);          printf(" us\n");
    printf("Execution Time (ms):      "); print_u32(res_queue.exec_ms);          printf(" ms\n");
    printf("Est. Active Power:        0.96 mW\n");
    printf("Est. CPU Energy Burned:   "); print_u32(res_queue.energy_uj);        printf(" uJ\n");
    printf("============================================================\n\n");
#endif

    P1OUT &= ~BIT0;
    P4OUT |= BIT7;

    __bis_SR_register(LPM4_bits);
    return 0;
}
