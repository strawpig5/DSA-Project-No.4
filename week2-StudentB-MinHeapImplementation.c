#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "energy.h"
#include "tasks.h"

#define MAX_READY_TASKS 1000

typedef struct {
    uint16_t tasks_done;
    uint16_t tasks_missed;
    uint32_t wasted_energy;
    uint32_t scheduling_steps; // Total comparisons (heap + deadline scan + energy check)
    uint32_t heap_comparisons; // Breakdown: pure heap sift comparisons
    uint32_t final_energy;
} sched_result_t;

// Min-heap structure representing the ready list
typedef struct {
    task_t items[MAX_READY_TASKS];
    uint16_t size;
    uint32_t comparisons;
} min_heap_t;

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

// -------------------------------------------------------------------------
// HEAP OPERATIONS (Student B)
// -------------------------------------------------------------------------

// Restore heap property moving upward (min-heap ordered by deadline)
static void sift_up(min_heap_t *heap, uint16_t index) {
    while (index > 0) {
        uint16_t parent = (index - 1) / 2;
        heap->comparisons++; // Heap comparison: child vs parent deadline
        if (heap->items[index].deadline < heap->items[parent].deadline) {
            heap_swap(&heap->items[index], &heap->items[parent]);
            index = parent;
        } else {
            break;
        }
    }
}

// Restore heap property moving downward
static void sift_down(min_heap_t *heap, uint16_t index) {
    while (1) {
        uint16_t left = 2 * index + 1;
        uint16_t right = 2 * index + 2;
        uint16_t smallest = index;

        if (left < heap->size) {
            heap->comparisons++; // Heap comparison: left child vs smallest deadline
            if (heap->items[left].deadline < heap->items[smallest].deadline) {
                smallest = left;
            }
        }
        if (right < heap->size) {
            heap->comparisons++; // Heap comparison: right child vs smallest deadline
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

// Insert a task into the min-heap
static bool heap_insert(min_heap_t *heap, task_t task) {
    if (heap->size >= MAX_READY_TASKS) {
        return false; // Heap overflow
    }
    heap->items[heap->size] = task;
    sift_up(heap, heap->size);
    heap->size++;
    return true;
}

// Remove and return the task with the smallest deadline (top of the heap)
static bool heap_remove_min(min_heap_t *heap, task_t *min_task) {
    if (heap_is_empty(heap)) {
        return false;
    }
    if (min_task) {
        *min_task = heap->items[0];
    }
    heap->items[0] = heap->items[heap->size - 1];
    heap->size--;
    if (heap->size > 0) {
        sift_down(heap, 0);
    }
    return true;
}

// Remove an expired task at a specific index without breaking heap property
static void heap_remove_at(min_heap_t *heap, uint16_t index) {
    if (index >= heap->size) return;
    heap->items[index] = heap->items[heap->size - 1];
    heap->size--;
    if (index < heap->size) {
        // Re-balance the heap: try sifting up first, then sifting down
        sift_up(heap, index);
        sift_down(heap, index);
    }
}

// ==========================================
// SIMULATION ENGINE WITH STEP TRACING
// ==========================================

sched_result_t run_student_b_heap_with_trace(uint32_t max_steps_to_trace) {
    sched_result_t res = {0, 0, 0, 0, 0, 0};
    min_heap_t ready_heap;
    heap_init(&ready_heap);

    uint32_t energy_buffer = 0;
    uint16_t next_task_idx = 0;

    for (uint32_t step = 0; step < NUM_STEPS; step++) {
        bool should_print = (step < max_steps_to_trace);

        if (should_print) {
            printf("[Step %5u] Energy trace input: +%u | Buffer: %u",
                   step, energy_in[step], energy_buffer);
        }

        // 1. Harvest Energy & Cap Buffer
        energy_buffer += energy_in[step];
        if (energy_buffer > ENERGY_MAX) {
            uint32_t overflow = energy_buffer - ENERGY_MAX;
            res.wasted_energy += overflow;
            energy_buffer = ENERGY_MAX;
            if (should_print && overflow > 0) {
                printf(" (Wasted: %u)", overflow);
            }
        }
        if (should_print) printf("\n");

        // 2. Add arriving tasks to min-heap
        while (next_task_idx < NUM_TASKS && tasks[next_task_idx].arrival == step) {
            task_t new_task = tasks[next_task_idx];
            if (heap_insert(&ready_heap, new_task)) {
                if (should_print) {
                    printf("  -> Event: Task ID %u arrived! (Cost: %u, Deadline: %u) [Heap size: %u]\n",
                           new_task.id, new_task.cost, new_task.deadline, ready_heap.size);
                }
            } else {
                printf("  -> Error: Ready list heap overflow at step %u!\n", step);
            }
            next_task_idx++;
        }

        // 3. Remove expired tasks (matches Student A's deadline comparison count)
        uint16_t i = 0;
        while (i < ready_heap.size) {
            res.scheduling_steps++; // Comparison: step > deadline (or deadline < step)
            if (ready_heap.items[i].deadline < step) {
                if (should_print) {
                    printf("  -> Event: Task ID %u MISSED deadline (%u)! Removed from heap.\n",
                           ready_heap.items[i].id, ready_heap.items[i].deadline);
                }
                res.tasks_missed++;
                heap_remove_at(&ready_heap, i);
            } else {
                i++;
            }
        }

        // 4. Run task at top of heap if enough energy is available
        if (!heap_is_empty(&ready_heap)) {
            task_t top_task = ready_heap.items[0];
            res.scheduling_steps++; // Comparison: energy_buffer >= top_task.cost

            if (energy_buffer >= top_task.cost) {
                task_t executed_task;
                heap_remove_min(&ready_heap, &executed_task);
                energy_buffer -= executed_task.cost;
                res.tasks_done++;
                if (should_print) {
                    printf("  -> Event: EXECUTED Task ID %u! Used %u energy. Remaining Buffer: %u\n",
                           executed_task.id, executed_task.cost, energy_buffer);
                }
            } else {
                if (should_print) {
                    printf("  -> Waiting: Task ID %u at top needs %u energy, but buffer has %u.\n",
                           top_task.id, top_task.cost, energy_buffer);
                }
            }
        }
    }

    // Mark remaining waiting tasks after NUM_STEPS as missed
    res.tasks_missed += ready_heap.size;
    ready_heap.size = 0;

    // Combine heap sift comparisons with the scheduler's deadline & energy comparisons
    res.heap_comparisons = ready_heap.comparisons;
    res.scheduling_steps += ready_heap.comparisons;
    res.final_energy = energy_buffer;

    return res;
}

int main(void) {
    // Pass how many steps you want to print step-by-step (e.g., first 500 steps)
    uint32_t steps_to_print = 500;

    printf("====================================================\n");
    printf("   Student B: Min-Heap EDF (Step-by-Step Trace)     \n");
    printf("====================================================\n");
    printf("Showing detailed trace for first %u steps...\n\n", steps_to_print);

    sched_result_t res_b = run_student_b_heap_with_trace(steps_to_print);

    printf("\n--- Final Simulation Summary ---\n");
    printf("Tasks Completed:   %u\n", res_b.tasks_done);
    printf("Tasks Missed:      %u\n", res_b.tasks_missed);
    printf("Wasted Energy:     %u units\n", res_b.wasted_energy);
    printf("Remaining Energy:  %u units\n", res_b.final_energy);
    printf("Heap Comparisons:  %u comparisons\n", res_b.heap_comparisons);
    printf("Scheduling Steps:  %u comparisons\n\n", res_b.scheduling_steps);

    return 0;
}