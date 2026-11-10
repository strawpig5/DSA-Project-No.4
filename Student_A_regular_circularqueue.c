#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// Include dataset headers directly without modifying or copying them
#include "tasks.h"
#include "energy.h"

#define MAX_READY_QUEUE 1000

// Result tracking metrics
typedef struct {
    uint16_t tasks_done;
    uint16_t tasks_missed;
    uint32_t wasted_energy;
    uint32_t scheduling_steps; // Operational cost tracking
    uint32_t final_energy;
} sched_result_t;

// Student A: Circular Queue Structure
typedef struct {
    task_t data[MAX_READY_QUEUE];
    uint16_t front;
    uint16_t rear;
    uint16_t count;
} circular_queue_t;

// ==========================================
// CIRCULAR QUEUE OPERATIONS (Student A)
// ==========================================

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
// SIMULATION ENGINE (Student A)
// ==========================================

sched_result_t run_student_a_queue(void) {
    sched_result_t res = {0, 0, 0, 0, 0};
    circular_queue_t ready_q;
    queue_init(&ready_q);

    uint32_t energy_buffer = 0;
    uint16_t next_task_idx = 0;

    for (uint32_t step = 0; step < NUM_STEPS; step++) {
        // 1. Harvest Energy & Cap Buffer
        energy_buffer += energy_in[step];
        if (energy_buffer > ENERGY_MAX) {
            res.wasted_energy += (energy_buffer - ENERGY_MAX);
            energy_buffer = ENERGY_MAX;
        }

        // 2. Add arriving tasks to ready queue
        while (next_task_idx < NUM_TASKS && tasks[next_task_idx].arrival == step) {
            queue_enqueue(&ready_q, tasks[next_task_idx]);
            next_task_idx++;
        }

        // 3. Remove expired tasks from front of queue
        task_t front_task;
        while (queue_peek(&ready_q, &front_task)) {
            res.scheduling_steps++;
            if (step > front_task.deadline) {
                queue_dequeue(&ready_q, NULL);
                res.tasks_missed++;
            } else {
                break; // Head task is still valid
            }
        }

        // 4. Run task at front if enough energy is available
        if (queue_peek(&ready_q, &front_task)) {
            res.scheduling_steps++;
            if (energy_buffer >= front_task.cost) {
                energy_buffer -= front_task.cost;
                queue_dequeue(&ready_q, NULL);
                res.tasks_done++;
            }
        }
    }

    // Mark any remaining tasks as missed at simulation end
    task_t remaining;
    while (queue_dequeue(&ready_q, &remaining)) {
        res.tasks_missed++;
    }

    res.final_energy = energy_buffer;
    return res;
}

// ==========================================
// MAIN FUNCTION & DISPLAY
// ==========================================

int main(void) {
    printf("=========================================\n");
    printf("   Student A: FCFS Circular Queue        \n");
    printf("=========================================\n");
    printf("Loaded dataset: %d steps, %d tasks\n\n", NUM_STEPS, NUM_TASKS);

    sched_result_t res_a = run_student_a_queue();

    printf("--- Results ---\n");
    printf("Tasks Completed:   %u\n", res_a.tasks_done);
    printf("Tasks Missed:      %u\n", res_a.tasks_missed);
    printf("Wasted Energy:     %u units\n", res_a.wasted_energy);
    printf("Remaining Energy:  %u units\n", res_a.final_energy);
    printf("Scheduling Steps:  %u comparisons\n\n", res_a.scheduling_steps);

    return 0;
}
