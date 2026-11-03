#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#include "energy.h"
#include "tasks.h"

// Ready list configuration
#define MAX_READY_TASKS 100

typedef struct {
    task_t items[MAX_READY_TASKS];
    uint16_t count;
} ready_list_t;

// Ready list helper functions
static void ready_list_init(ready_list_t *rl) {
    rl->count = 0;
}

static bool ready_list_add(ready_list_t *rl, task_t task) {
    if (rl->count >= MAX_READY_TASKS) {
        return false; // Ready list is full
    }
    rl->items[rl->count++] = task;
    return true;
}

static void ready_list_remove_at(ready_list_t *rl, uint16_t index) {
    if (index >= rl->count) return;
    for (uint16_t i = index; i < rl->count - 1; i++) {
        rl->items[i] = rl->items[i + 1];
    }
    rl->count--;
}

int main(void) {
    // Seed random number generator for Week 1 baseline (random selection)
    srand((unsigned int)time(NULL));

    // Simulation State
    uint16_t energy_buffer = 0;      // Current energy level in capacitor
    uint32_t wasted_energy = 0;     // Energy lost due to full buffer
    uint16_t tasks_completed = 0;   // Successfully run tasks
    uint16_t deadlines_missed = 0;  // Tasks dropped due to expired deadlines

    ready_list_t ready_list;
    ready_list_init(&ready_list);

    uint16_t next_task_idx = 0; // Index in the global `tasks` array

    // -------------------------------------------------------------------------
    // TIME-STEP LOOP (ms per step)
    // -------------------------------------------------------------------------
    for (uint32_t t = 0; t < NUM_STEPS; t++) {

        // 1. ENERGY BUFFER UPDATE
        uint8_t harvested = energy_in[t];
        if (energy_buffer + harvested > ENERGY_MAX) {
            wasted_energy += (energy_buffer + harvested) - ENERGY_MAX;
            energy_buffer = ENERGY_MAX;
        } else {
            energy_buffer += harvested;
        }

        // 2. TASK ARRIVAL
        // Add all tasks arriving at current time step t to the ready list
        while (next_task_idx < NUM_TASKS && tasks[next_task_idx].arrival == t) {
            if (!ready_list_add(&ready_list, tasks[next_task_idx])) {
                printf("Warning: Ready list overflow at step %u for task ID %u!\n", 
                        t, tasks[next_task_idx].id);
            }
            next_task_idx++;
        }

        // 3. DEADLINE CHECK & CLEANUP
        // Remove tasks whose deadline has already passed (deadline < t)
        uint16_t i = 0;
        while (i < ready_list.count) {
            if (ready_list.items[i].deadline < t) {
                deadlines_missed++;
                ready_list_remove_at(&ready_list, i);
                // Do not increment i because the next element shifted into index i
            } else {
                i++;
            }
        }

        // 4. TASK SCHEDULING & EXECUTION (Baseline: Random Pick)
        if (ready_list.count > 0) {
            // Pick a random task from the ready list
            uint16_t picked_index = rand() % ready_list.count;
            task_t candidate = ready_list.items[picked_index];

            // Check if buffer has sufficient energy to run candidate task
            if (energy_buffer >= candidate.cost) {
                // Deduct energy cost and mark task as completed
                energy_buffer -= candidate.cost;
                tasks_completed++;

                // Remove task from ready list
                ready_list_remove_at(&ready_list, picked_index);
            }
        }
    }

    // 5. POST-SIMULATION CLEANUP
    // Count remaining tasks left waiting in the ready list after NUM_STEPS as missed
    deadlines_missed += ready_list.count;

    // 6. PRINT RESULTS
    printf("--- Simulation Results (Baseline: Random Pick) ---\n");
    printf("Total Steps Simulated: %u\n", NUM_STEPS);
    printf("Tasks Completed:      %u / %u\n", tasks_completed, NUM_TASKS);
    printf("Deadlines Missed:     %u\n", deadlines_missed);
    printf("Wasted Energy:        %u units\n", wasted_energy);
    printf("Remaining Energy:     %u / %u\n", energy_buffer, ENERGY_MAX);

    return 0;
}