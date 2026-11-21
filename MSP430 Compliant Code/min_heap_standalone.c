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
    uint32_t heap_comparisons;
    uint32_t final_energy;
    uint32_t cpu_ticks;
    uint32_t exec_us;
    uint32_t exec_ms;
    uint32_t energy_uj;
} sched_result_t;

typedef struct {
    task_t items[MAX_READY_TASKS];
    uint16_t size;
    uint32_t comparisons;
} min_heap_t;

static min_heap_t ready_heap;
static sched_result_t res_heap;
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

static void heap_init(min_heap_t *h) {
    h->size = 0;
    h->comparisons = 0;
}

static bool heap_is_empty(const min_heap_t *h) {
    return h->size == 0;
}

static void heap_swap(task_t *a, task_t *b) {
    task_t tmp = *a;
    *a = *b;
    *b = tmp;
}

static void sift_up(min_heap_t *h, uint16_t idx) {
    while (idx > 0) {
        uint16_t p = (idx - 1) / 2;
        h->comparisons++;
        if (h->items[idx].deadline < h->items[p].deadline) {
            heap_swap(&h->items[idx], &h->items[p]);
            idx = p;
        } else {
            break;
        }
    }
}

static void sift_down(min_heap_t *h, uint16_t idx) {
    while (1) {
        uint16_t l = 2 * idx + 1;
        uint16_t r = 2 * idx + 2;
        uint16_t sm = idx;

        if (l < h->size) {
            h->comparisons++;
            if (h->items[l].deadline < h->items[sm].deadline) {
                sm = l;
            }
        }
        if (r < h->size) {
            h->comparisons++;
            if (h->items[r].deadline < h->items[sm].deadline) {
                sm = r;
            }
        }
        if (sm != idx) {
            heap_swap(&h->items[idx], &h->items[sm]);
            idx = sm;
        } else {
            break;
        }
    }
}

static bool heap_insert(min_heap_t *h, task_t t) {
    if (h->size >= MAX_READY_TASKS) return false;
    h->items[h->size] = t;
    sift_up(h, h->size);
    h->size++;
    return true;
}

static bool heap_remove_min(min_heap_t *h, task_t *min_t) {
    if (heap_is_empty(h)) return false;
    if (min_t) *min_t = h->items[0];
    h->items[0] = h->items[h->size - 1];
    h->size--;
    if (h->size > 0) sift_down(h, 0);
    return true;
}

static void heap_remove_at(min_heap_t *h, uint16_t idx) {
    if (idx >= h->size) return;
    h->items[idx] = h->items[h->size - 1];
    h->size--;
    if (idx < h->size) {
        sift_up(h, idx);
        sift_down(h, idx);
    }
}

static void run_student_b_heap(uint32_t max_steps_to_trace) {
    uint32_t step;
    uint32_t energy_buffer = 0;
    uint16_t next_idx = 0;
    uint16_t i;

    res_heap.tasks_done = 0;
    res_heap.tasks_missed = 0;
    res_heap.wasted_energy = 0;
    res_heap.scheduling_steps = 0;
    res_heap.heap_comparisons = 0;
    res_heap.final_energy = 0;
    heap_init(&ready_heap);

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
            res_heap.wasted_energy += overflow;
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

        /* 2. Add arriving tasks to min-heap */
        while (next_idx < NUM_TASKS && tasks[next_idx].arrival == step) {
            task_t new_task = tasks[next_idx];
            if (heap_insert(&ready_heap, new_task)) {
#if ENABLE_DEBUG_PRINT
                if (should_print) {
                    printf("  -> Event: Task ID %d arrived! (Cost: %d, Deadline: %d) [Heap size: %d]\n",
                           (int)new_task.id, (int)new_task.cost,
                           (int)new_task.deadline, (int)ready_heap.size);
                }
#endif
            } else {
#if ENABLE_DEBUG_PRINT
                printf("  -> Error: Ready list heap overflow at step ");
                print_u32(step);
                printf("!\n");
#endif
            }
            next_idx++;
        }

        /* 3. Remove expired tasks */
        i = 0;
        while (i < ready_heap.size) {
            res_heap.scheduling_steps++;
            if (ready_heap.items[i].deadline < step) {
#if ENABLE_DEBUG_PRINT
                if (should_print) {
                    printf("  -> Event: Task ID %d MISSED deadline (%d)! Removed from heap.\n",
                           (int)ready_heap.items[i].id, (int)ready_heap.items[i].deadline);
                }
#endif
                res_heap.tasks_missed++;
                heap_remove_at(&ready_heap, i);
                P1OUT ^= BIT0;
            } else {
                i++;
            }
        }

        /* 4. Run task at top of heap if enough energy is available */
        if (!heap_is_empty(&ready_heap)) {
            task_t top_t = ready_heap.items[0];
            res_heap.scheduling_steps++;
            if (energy_buffer >= top_t.cost) {
                task_t exec_t;
                heap_remove_min(&ready_heap, &exec_t);
                energy_buffer -= exec_t.cost;
                res_heap.tasks_done++;
                P4OUT ^= BIT7;
#if ENABLE_DEBUG_PRINT
                if (should_print) {
                    printf("  -> Event: EXECUTED Task ID %d! Used %d energy. Remaining Buffer: ",
                           (int)exec_t.id, (int)exec_t.cost);
                    print_u32(energy_buffer);
                    printf("\n");
                }
#endif
            } else {
#if ENABLE_DEBUG_PRINT
                if (should_print) {
                    printf("  -> Waiting: Task ID %d at top needs %d energy, but buffer has ",
                           (int)top_t.id, (int)top_t.cost);
                    print_u32(energy_buffer);
                    printf(".\n");
                }
#endif
            }
        }
    }

    res_heap.tasks_missed += ready_heap.size;
    ready_heap.size = 0;
    res_heap.heap_comparisons = ready_heap.comparisons;
    res_heap.scheduling_steps += ready_heap.comparisons;
    res_heap.final_energy = energy_buffer;
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
    printf("   Student B: Min-Heap EDF (MSP430F5529LP)                  \n");
    printf("============================================================\n");
    printf("Showing detailed trace for first ");
    print_u32(steps_to_print);
    printf(" steps...\n\n");
#endif

    /* Pass 1: Detailed trace for first 25 steps */
    run_student_b_heap(steps_to_print);

    /* Pass 2: Untimed pure CPU benchmark (0 printed steps) for accurate timing */
    timer_start();
    run_student_b_heap(0);
    res_heap.cpu_ticks = timer_stop_ticks();
    compute_hardware_metrics(&res_heap);

#if ENABLE_DEBUG_PRINT
    printf("\n============================================================\n");
    printf("   Final Simulation & Hardware Benchmark Summary (Min-Heap) \n");
    printf("============================================================\n");
    printf("Tasks Completed:          %d\n", (int)res_heap.tasks_done);
    printf("Tasks Missed:             %d\n", (int)res_heap.tasks_missed);
    printf("Wasted Energy (Sim):      "); print_u32(res_heap.wasted_energy);    printf(" units\n");
    printf("Remaining Energy (Sim):   "); print_u32(res_heap.final_energy);     printf(" units\n");
    printf("Heap Sift Comparisons:    "); print_u32(res_heap.heap_comparisons); printf(" comparisons\n");
    printf("Total Scheduling Steps:   "); print_u32(res_heap.scheduling_steps); printf(" comparisons\n");
    printf("Timer_A0 Hardware Ticks:  "); print_u32(res_heap.cpu_ticks);        printf(" ticks\n");
    printf("Execution Time (us):      "); print_u32(res_heap.exec_us);          printf(" us\n");
    printf("Execution Time (ms):      "); print_u32(res_heap.exec_ms);          printf(" ms\n");
    printf("Est. Active Power:        0.96 mW\n");
    printf("Est. CPU Energy Burned:   "); print_u32(res_heap.energy_uj);        printf(" uJ\n");
    printf("============================================================\n\n");
#endif

    P1OUT &= ~BIT0;
    P4OUT |= BIT7;

    __bis_SR_register(LPM4_bits);
    return 0;
}
