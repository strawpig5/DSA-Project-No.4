#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "tasks.h"
#include "energy.h"

#define MAX_READY_QUEUE 1000

typedef struct {
    uint16_t tasks_done;
    uint16_t tasks_missed;
    uint32_t wasted_energy;
    uint32_t scheduling_steps;
    uint32_t final_energy;
} sched_result_t;

typedef struct {
    task_t data[MAX_READY_QUEUE];
    uint16_t front;
    uint16_t rear;
    uint16_t count;
} circular_queue_t;

void queue_init(circular_queue_t *q) {
    q->front = 0;
    q->rear = 0;
    q->count = 0;
}

bool queue_is_empty(const circular_queue_t *q) {
    return q->count == 0;
}

bool queue_is_full(const circular_queue_t *q) {
    return q->count >= MAX_READY_QUEUE;
}

bool queue_enqueue(circular_queue_t *q, task_t task) {
    if (queue_is_full(q)) return false;
    q->data[q->rear] = task;
    q->rear = (q->rear + 1) % MAX_READY_QUEUE;
    q->count++;
    return true;
}

bool queue_dequeue(circular_queue_t *q, task_t *out_task) {
    if (queue_is_empty(q)) return false;
    if (out_task) *out_task = q->data[q->front];
    q->front = (q->front + 1) % MAX_READY_QUEUE;
    q->count--;
    return true;
}

bool queue_peek(const circular_queue_t *q, task_t *out_task) {
    if (queue_is_empty(q)) return false;
    if (out_task) *out_task = q->data[q->front];
    return true;
}

// ==========================================
// SIMULATION ENGINE WITH STEP TRACING
// ==========================================

sched_result_t run_student_a_queue_with_trace(uint32_t max_steps_to_trace) {
    sched_result_t res = {0, 0, 0, 0, 0};
    circular_queue_t ready_q;
    queue_init(&ready_q);

    uint32_t energy_buffer = 0;
    uint16_t next_task_idx = 0;

    for (uint32_t step = 0; step < NUM_STEPS; step++) {
        bool activity_in_step = false;

        // Trace header (only prints if within the max_steps limit)
        if (step < max_steps_to_trace) {
            printf("[Step %5u] Energy trace input: +%u | Buffer: %u", 
                    step, energy_in[step], energy_buffer);
        }

        // 1. Harvest Energy & Cap Buffer
        energy_buffer += energy_in[step];
        if (energy_buffer > ENERGY_MAX) {
            uint32_t overflow = energy_buffer - ENERGY_MAX;
            res.wasted_energy += overflow;
            energy_buffer = ENERGY_MAX;
            if (step < max_steps_to_trace && overflow > 0) {
                printf(" (Wasted: %u)", overflow);
            }
        }
        if (step < max_steps_to_trace) printf("\n");

        // 2. Add arriving tasks to ready queue
        while (next_task_idx < NUM_TASKS && tasks[next_task_idx].arrival == step) {
            queue_enqueue(&ready_q, tasks[next_task_idx]);
            if (step < max_steps_to_trace) {
                printf("  -> Event: Task ID %u arrived! (Cost: %u, Deadline: %u) [Queue size: %u]\n",
                        tasks[next_task_idx].id, tasks[next_task_idx].cost, 
                        tasks[next_task_idx].deadline, ready_q.count);
            }
            next_task_idx++;
        }

        // 3. Remove expired tasks from front of queue
        task_t front_task;
        while (queue_peek(&ready_q, &front_task)) {
            res.scheduling_steps++;
            if (step > front_task.deadline) {
                queue_dequeue(&ready_q, NULL);
                res.tasks_missed++;
                if (step < max_steps_to_trace) {
                    printf("  -> Event: Task ID %u MISSED deadline (%u)! Discarded from front.\n", 
                            front_task.id, front_task.deadline);
                }
            } else {
                break; 
            }
        }

        // 4. Run task at front if enough energy is available
        if (queue_peek(&ready_q, &front_task)) {
            res.scheduling_steps++;
            if (energy_buffer >= front_task.cost) {
                energy_buffer -= front_task.cost;
                queue_dequeue(&ready_q, NULL);
                res.tasks_done++;
                if (step < max_steps_to_trace) {
                    printf("  -> Event: EXECUTED Task ID %u! Used %u energy. Remaining Buffer: %u\n", 
                            front_task.id, front_task.cost, energy_buffer);
                }
            } else {
                if (step < max_steps_to_trace) {
                    printf("  -> Waiting: Task ID %u at front needs %u energy, but buffer has %u.\n", 
                            front_task.id, front_task.cost, energy_buffer);
                }
            }
        }
    }

    task_t remaining;
    while (queue_dequeue(&ready_q, &remaining)) {
        res.tasks_missed++;
    }

    res.final_energy = energy_buffer;
    return res;
}

int main(void) {
    // Pass how many steps you want to print step-by-step (e.g., first 500 steps)
    uint32_t steps_to_print = 500;

    printf("====================================================\n");
    printf("   Student A: FCFS Circular Queue (Step-by-Step Trace) \n");
    printf("====================================================\n");
    printf("Showing detailed trace for first %u steps...\n\n", steps_to_print);

    sched_result_t res_a = run_student_a_queue_with_trace(steps_to_print);

    printf("\n--- Final Simulation Summary ---\n");
    printf("Tasks Completed:   %u\n", res_a.tasks_done);
    printf("Tasks Missed:      %u\n", res_a.tasks_missed);
    printf("Wasted Energy:     %u units\n", res_a.wasted_energy);
    printf("Remaining Energy:  %u units\n", res_a.final_energy);
    printf("Scheduling Steps:  %u comparisons\n\n", res_a.scheduling_steps);

    return 0;
}
