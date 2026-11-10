#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "energy.h"
#include "tasks.h"

#define MAX_READY_TASKS 1000

// Min-heap structure representing the ready list
typedef struct {
    task_t items[MAX_READY_TASKS];
    uint16_t size;
} min_heap_t;

// Initialize the min-heap
static void heap_init(min_heap_t *heap) {
    heap->size = 0;
}

// Check if heap is empty
static bool heap_is_empty(const min_heap_t *heap) {
    return heap->size == 0;
}

// Swap two tasks in the heap
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
        // Compare deadlines: parent deadline must be <= child deadline
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

        if (left < heap->size && heap->items[left].deadline < heap->items[smallest].deadline) {
            smallest = left;
        }
        if (right < heap->size && heap->items[right].deadline < heap->items[smallest].deadline) {
            smallest = right;
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
    *min_task = heap->items[0];
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

// -------------------------------------------------------------------------
// MAIN SIMULATION LOOP (Student B - Earliest Deadline First)
// -------------------------------------------------------------------------

int main(void) {
    uint16_t energy_buffer = 0;
    uint32_t wasted_energy = 0;
    uint16_t tasks_completed = 0;
    uint16_t deadlines_missed = 0;

    min_heap_t ready_heap;
    heap_init(&ready_heap);

    uint16_t next_task_idx = 0;

    for (uint32_t t = 0; t < NUM_STEPS; t++) {

        // 1. Energy harvesting step
        uint8_t harvested = energy_in[t];
        if (energy_buffer + harvested > ENERGY_MAX) {
            wasted_energy += (energy_buffer + harvested) - ENERGY_MAX;
            energy_buffer = ENERGY_MAX;
        } else {
            energy_buffer += harvested;
        }

        // 2. Task arrival: Insert arriving tasks into min-heap
        while (next_task_idx < NUM_TASKS && tasks[next_task_idx].arrival == t) {
            if (!heap_insert(&ready_heap, tasks[next_task_idx])) {
                printf("Error: Ready list heap overflow at t=%u!\n", t);
            }
            next_task_idx++;
        }

        // 3. Deadline check: Remove all tasks whose deadline < t
        uint16_t i = 0;
        while (i < ready_heap.size) {
            if (ready_heap.items[i].deadline < t) {
                deadlines_missed++;
                heap_remove_at(&ready_heap, i);
                // Re-evaluate index i as it now holds a replacement element
            } else {
                i++;
            }
        }

        // 4. Scheduling decision (Student B: Top of Min-Heap / Earliest Deadline First)
        if (!heap_is_empty(&ready_heap)) {
            // Peek top task (earliest deadline)
            task_t top_task = ready_heap.items[0];

            // Run task if there is enough energy
            if (energy_buffer >= top_task.cost) {
                task_t executed_task;
                heap_remove_min(&ready_heap, &executed_task);

                energy_buffer -= executed_task.cost;
                tasks_completed++;
            }
        }
    }

    // Mark remaining waiting tasks after NUM_STEPS as missed
    deadlines_missed += ready_heap.size;

    // Output results
    printf("=== Student B: Min-Heap (Earliest Deadline First) ===\n");
    printf("Total Steps Simulated: %u\n", NUM_STEPS);
    printf("Tasks Completed:      %u / %u\n", tasks_completed, NUM_TASKS);
    printf("Deadlines Missed:     %u\n", deadlines_missed);
    printf("Wasted Energy:        %u units\n", wasted_energy);
    printf("Remaining Energy:     %u / %u\n", energy_buffer, ENERGY_MAX);

    return 0;
}