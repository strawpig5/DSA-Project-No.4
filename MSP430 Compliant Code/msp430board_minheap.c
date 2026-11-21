#include <msp430.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Set to 1 to print benchmark summary to CCStudio Debug Console.
   Set to 0 when running standalone on harvested power. */
#define ENABLE_DEBUG_PRINT 1

/* Set to 0 for a single fast benchmark run (prints exact single-run metrics).
   Set to 1 to loop each scheduler 50 times (~3-5 seconds each) so you can
   capture a clear power profile in CCStudio's EnergyTrace window! */
#define ENERGYTRACE_STRESS_RUNS 0

#if ENABLE_DEBUG_PRINT
#include <stdio.h>
#endif

#include "energy.h"
#include "tasks.h"

#define MAX_READY_TASKS 128

/* MSP430F5529 Active Power at ~1.05 MHz, 3.3V is ~0.96 mW (290 uA * 3.3V) */
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

/* Student A: FCFS Circular Queue Structure */
typedef struct {
    task_t data[MAX_READY_TASKS];
    uint16_t front;
    uint16_t rear;
    uint16_t count;
} circular_queue_t;

/* Student B: EDF Min-Heap Structure */
typedef struct {
    task_t items[MAX_READY_TASKS];
    uint16_t size;
    uint32_t comparisons;
} min_heap_t;

/* Allocated in static SRAM (.bss) to prevent stack overflow */
static circular_queue_t ready_queue;
static min_heap_t ready_heap;
static sched_result_t res_queue;
static sched_result_t res_heap;

/* =========================================================================
   HARDWARE TIMER_A0 BENCHMARKING (~1.048576 MHz SMCLK / 8 = 131,072 Hz)
   ========================================================================= */
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
    /* 131,072 ticks/sec -> 1 tick = 7.62939 us
       us = (ticks * 1,000,000) / 131,072 = (ticks * 15625) >> 11 */
    res->exec_us = (res->cpu_ticks * 15625UL) >> 11;
    res->exec_ms = res->exec_us / 1000UL;

    /* Energy (uJ) = Power (uW) * Time (s) = (Active_uW * exec_us) / 1,000,000 */
    res->energy_uj = (res->exec_us / 1000UL) * MSP430F5529_ACTIVE_UW / 1000UL;
    if (res->energy_uj == 0 && res->exec_us > 0) {
        res->energy_uj = (res->exec_us * MSP430F5529_ACTIVE_UW) / 1000000UL;
    }
}

#if ENABLE_DEBUG_PRINT
/* Prints 32-bit unsigned integer using only %d (safe for printf_support=minimal) */
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

/* Prints fixed-point ratio (e.g., 245 -> "2.45x") without float printf */
static void print_ratio_x100(uint32_t numerator, uint32_t denominator) {
    uint32_t scaled;
    uint16_t whole;
    uint16_t frac;
    if (denominator == 0) {
        printf("N/A");
        return;
    }
    scaled = (numerator * 100UL) / denominator;
    whole = (uint16_t)(scaled / 100UL);
    frac  = (uint16_t)(scaled % 100UL);
    printf("%d.", (int)whole);
    if (frac < 10) printf("0");
    printf("%dx", (int)frac);
}
#endif

/* =========================================================================
   STUDENT A: CIRCULAR QUEUE OPERATIONS (FCFS)
   ========================================================================= */
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

static bool queue_dequeue(circular_queue_t *q, task_t *out_task) {
    if (queue_is_empty(q)) return false;
    if (out_task) *out_task = q->data[q->front];
    q->front = (q->front + 1) % MAX_READY_TASKS;
    q->count--;
    return true;
}

static bool queue_peek(const circular_queue_t *q, task_t *out_task) {
    if (queue_is_empty(q)) return false;
    if (out_task) *out_task = q->data[q->front];
    return true;
}

static void run_student_a_queue(void) {
    uint32_t step;
    uint32_t energy_buffer = 0;
    uint16_t next_task_idx = 0;
    task_t front_task;
    task_t remaining;

    res_queue.tasks_done = 0;
    res_queue.tasks_missed = 0;
    res_queue.wasted_energy = 0;
    res_queue.scheduling_steps = 0;
    res_queue.heap_comparisons = 0;
    res_queue.final_energy = 0;

    queue_init(&ready_queue);

    for (step = 0; step < NUM_STEPS; step++) {
        energy_buffer += energy_in[step];
        if (energy_buffer > ENERGY_MAX) {
            res_queue.wasted_energy += (energy_buffer - ENERGY_MAX);
            energy_buffer = ENERGY_MAX;
        }

        while (next_task_idx < NUM_TASKS && tasks[next_task_idx].arrival == step) {
            queue_enqueue(&ready_queue, tasks[next_task_idx]);
            next_task_idx++;
        }

        while (queue_peek(&ready_queue, &front_task)) {
            res_queue.scheduling_steps++;
            if (step > front_task.deadline) {
                queue_dequeue(&ready_queue, NULL);
                res_queue.tasks_missed++;
            } else {
                break;
            }
        }

        if (queue_peek(&ready_queue, &front_task)) {
            res_queue.scheduling_steps++;
            if (energy_buffer >= front_task.cost) {
                energy_buffer -= front_task.cost;
                queue_dequeue(&ready_queue, NULL);
                res_queue.tasks_done++;
            }
        }
    }

    while (queue_dequeue(&ready_queue, &remaining)) {
        res_queue.tasks_missed++;
    }

    res_queue.final_energy = energy_buffer;
}

/* =========================================================================
   STUDENT B: MIN-HEAP OPERATIONS (EDF)
   ========================================================================= */
static void heap_init(min_heap_t *heap) {
    heap->size = 0;
    heap->comparisons = 0;
}

static bool heap_is_empty(const min_heap_t *heap) {
    return heap->size == 0;
}

static void heap_swap(task_t *a, task_t *b) {
    task_t temp = *a;
    *a = *b;
    *b = temp;
}

static void sift_up(min_heap_t *heap, uint16_t index) {
    while (index > 0) {
        uint16_t parent = (index - 1) / 2;
        heap->comparisons++;
        if (heap->items[index].deadline < heap->items[parent].deadline) {
            heap_swap(&heap->items[index], &heap->items[parent]);
            index = parent;
        } else {
            break;
        }
    }
}

static void sift_down(min_heap_t *heap, uint16_t index) {
    while (1) {
        uint16_t left = 2 * index + 1;
        uint16_t right = 2 * index + 2;
        uint16_t smallest = index;

        if (left < heap->size) {
            heap->comparisons++;
            if (heap->items[left].deadline < heap->items[smallest].deadline) {
                smallest = left;
            }
        }
        if (right < heap->size) {
            heap->comparisons++;
            if (heap->items[right].deadline < heap->items[smallest].deadline) {
                smallest = right;
            }
        }

        if (smallest != index) {
            heap_swap(&heap->items[index], &heap->items[smallest]);
            index = smallest;
        } else {
            break;
        }
    }
}

static bool heap_insert(min_heap_t *heap, task_t task) {
    if (heap->size >= MAX_READY_TASKS) return false;
    heap->items[heap->size] = task;
    sift_up(heap, heap->size);
    heap->size++;
    return true;
}

static bool heap_remove_min(min_heap_t *heap, task_t *min_task) {
    if (heap_is_empty(heap)) return false;
    if (min_task) *min_task = heap->items[0];
    heap->items[0] = heap->items[heap->size - 1];
    heap->size--;
    if (heap->size > 0) sift_down(heap, 0);
    return true;
}

static void heap_remove_at(min_heap_t *heap, uint16_t index) {
    if (index >= heap->size) return;
    heap->items[index] = heap->items[heap->size - 1];
    heap->size--;
    if (index < heap->size) {
        sift_up(heap, index);
        sift_down(heap, index);
    }
}

static void run_student_b_heap(void) {
    uint32_t step;
    uint32_t energy_buffer = 0;
    uint16_t next_task_idx = 0;
    uint16_t i;

    res_heap.tasks_done = 0;
    res_heap.tasks_missed = 0;
    res_heap.wasted_energy = 0;
    res_heap.scheduling_steps = 0;
    res_heap.heap_comparisons = 0;
    res_heap.final_energy = 0;

    heap_init(&ready_heap);

    for (step = 0; step < NUM_STEPS; step++) {
        energy_buffer += energy_in[step];
        if (energy_buffer > ENERGY_MAX) {
            res_heap.wasted_energy += (energy_buffer - ENERGY_MAX);
            energy_buffer = ENERGY_MAX;
        }

        while (next_task_idx < NUM_TASKS && tasks[next_task_idx].arrival == step) {
            heap_insert(&ready_heap, tasks[next_task_idx]);
            next_task_idx++;
        }

        i = 0;
        while (i < ready_heap.size) {
            res_heap.scheduling_steps++;
            if (ready_heap.items[i].deadline < step) {
                res_heap.tasks_missed++;
                heap_remove_at(&ready_heap, i);
            } else {
                i++;
            }
        }

        if (!heap_is_empty(&ready_heap)) {
            task_t top_task = ready_heap.items[0];
            res_heap.scheduling_steps++;

            if (energy_buffer >= top_task.cost) {
                task_t executed_task;
                heap_remove_min(&ready_heap, &executed_task);
                energy_buffer -= executed_task.cost;
                res_heap.tasks_done++;
            }
        }
    }

    res_heap.tasks_missed += ready_heap.size;
    ready_heap.size = 0;

    res_heap.heap_comparisons = ready_heap.comparisons;
    res_heap.scheduling_steps += ready_heap.comparisons;
    res_heap.final_energy = energy_buffer;
}

/* =========================================================================
   MAIN: HARDWARE BENCHMARK (TIME, SPEEDUP, POWER & ENERGY)
   ========================================================================= */
int main(void) {
    uint16_t iter;
    uint16_t runs = (ENERGYTRACE_STRESS_RUNS) ? 50 : 1;

    WDTCTL = WDTPW | WDTHOLD;

    /* Configure F5529LP Red LED (P1.0) and Green LED (P4.7) */
    P1DIR |= BIT0;
    P1OUT &= ~BIT0;
    P4DIR |= BIT7;
    P4OUT &= ~BIT7;

#if ENABLE_DEBUG_PRINT
    printf("==================================================================\n");
    printf("   MSP-EXP430F5529LP Hardware Benchmark: FCFS Queue vs EDF Heap   \n");
    printf("==================================================================\n");
#endif

    /* 1. Run & Time Student A: Circular Queue (Red LED ON during execution) */
    P1OUT |= BIT0;
    timer_start();
    for (iter = 0; iter < runs; iter++) {
        run_student_a_queue();
    }
    res_queue.cpu_ticks = timer_stop_ticks() / runs;
    P1OUT &= ~BIT0;
    compute_hardware_metrics(&res_queue);

    /* Brief pause (~250ms) to separate EnergyTrace peaks */
    __delay_cycles(262144);

    /* 2. Run & Time Student B: Min-Heap EDF (Green LED ON during execution) */
    P4OUT |= BIT7;
    timer_start();
    for (iter = 0; iter < runs; iter++) {
        run_student_b_heap();
    }
    res_heap.cpu_ticks = timer_stop_ticks() / runs;
    P4OUT &= ~BIT7;
    compute_hardware_metrics(&res_heap);

#if ENABLE_DEBUG_PRINT
    printf("METRIC                   | CIRCULAR QUEUE (A) | MIN-HEAP EDF (B)\n");
    printf("-------------------------+--------------------+-------------------\n");
    printf("Tasks Completed          | %d                | %d\n", (int)res_queue.tasks_done, (int)res_heap.tasks_done);
    printf("Tasks Missed             | %d                 | %d\n", (int)res_queue.tasks_missed, (int)res_heap.tasks_missed);
    printf("Wasted Energy (Sim)      | "); print_u32(res_queue.wasted_energy);    printf("               | "); print_u32(res_heap.wasted_energy);    printf("\n");
    printf("Remaining Energy (Sim)   | "); print_u32(res_queue.final_energy);     printf("                | "); print_u32(res_heap.final_energy);     printf("\n");
    printf("Heap Sift Comparisons    | 0                  | "); print_u32(res_heap.heap_comparisons); printf("\n");
    printf("Total Scheduling Steps   | "); print_u32(res_queue.scheduling_steps); printf("              | "); print_u32(res_heap.scheduling_steps); printf("\n");
    printf("Timer_A0 Hardware Ticks  | "); print_u32(res_queue.cpu_ticks);        printf("              | "); print_u32(res_heap.cpu_ticks);        printf("\n");
    printf("Execution Time (us)      | "); print_u32(res_queue.exec_us);          printf(" us          | "); print_u32(res_heap.exec_us);          printf(" us\n");
    printf("Execution Time (ms)      | "); print_u32(res_queue.exec_ms);          printf(" ms             | "); print_u32(res_heap.exec_ms);          printf(" ms\n");
    printf("Est. Active Power        | 0.96 mW            | 0.96 mW\n");
    printf("Est. CPU Energy Burned   | "); print_u32(res_queue.energy_uj);        printf(" uJ             | "); print_u32(res_heap.energy_uj);        printf(" uJ\n");
    printf("==================================================================\n\n");

    printf("--- Hardware Speedup & Efficiency Analysis ---\n");
    printf("* Queue Speedup over Heap: ");
    print_ratio_x100(res_heap.cpu_ticks, res_queue.cpu_ticks);
    printf(" faster (O(1) FCFS vs O(log n) EDF)\n");

    printf("* Heap Speedup over Queue: ");
    print_ratio_x100(res_queue.cpu_ticks, res_heap.cpu_ticks);
    printf(" relative speed\n");

    printf("* Deadline Reliability:    +%d more tasks completed before deadline!\n\n",
           (int)(res_heap.tasks_done - res_queue.tasks_done));
#endif

    /* Solid Green LED ON to signal clean completion */
    P1OUT &= ~BIT0;
    P4OUT |= BIT7;

    __bis_SR_register(LPM4_bits);
    return 0;
}
