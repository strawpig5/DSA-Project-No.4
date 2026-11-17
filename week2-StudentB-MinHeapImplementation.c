// Set how many time steps to print in the trace (set to 0 to print only final results)
#define MAX_PRINT_STEPS 10

int main(void) {
    uint16_t energy_buffer = 0;
    uint32_t wasted_energy = 0;
    uint16_t tasks_completed = 0;
    uint16_t deadlines_missed = 0;

    min_heap_t ready_heap;
    heap_init(&ready_heap);

    uint16_t next_task_idx = 0;

    if (MAX_PRINT_STEPS > 0) {
        printf("--- SIMULATION TRACE START (Showing first %u steps) ---\n", MAX_PRINT_STEPS);
    }

    for (uint32_t t = 0; t < NUM_STEPS; t++) {
        bool should_print = (t < MAX_PRINT_STEPS);

        if (should_print) {
            printf("\n[ Time Step t = %u ]\n", t);
        } else if (t == MAX_PRINT_STEPS && MAX_PRINT_STEPS > 0) {
            printf("\n... [Trace truncated after %u steps; running remaining steps silently] ...\n", MAX_PRINT_STEPS);
        }

        // 1. Energy harvesting step
        uint8_t harvested = energy_in[t];
        if (energy_buffer + harvested > ENERGY_MAX) {
            uint16_t waste = (energy_buffer + harvested) - ENERGY_MAX;
            wasted_energy += waste;
            energy_buffer = ENERGY_MAX;
            if (should_print) {
                printf("  Energy: +%u harvested -> Cap at %u (Wasted: %u, Total Wasted: %u)\n", 
                        harvested, ENERGY_MAX, waste, wasted_energy);
            }
        } else {
            energy_buffer += harvested;
            if (should_print) {
                printf("  Energy: +%u harvested -> Buffer: %u/%u\n", 
                        harvested, energy_buffer, ENERGY_MAX);
            }
        }

        // 2. Task arrival: Insert arriving tasks into min-heap
        while (next_task_idx < NUM_TASKS && tasks[next_task_idx].arrival == t) {
            task_t new_task = tasks[next_task_idx];
            if (heap_insert(&ready_heap, new_task)) {
                if (should_print) {
                    printf("  Arrival: Task (ID/Cost: %u, Deadline: %u) added to ready heap.\n", 
                            new_task.cost, new_task.deadline);
                }
            } else {
                // Always print errors even if trace is truncated
                printf("  Error: Ready list heap overflow at t=%u!\n", t);
            }
            next_task_idx++;
        }

        // 3. Deadline check: Remove all tasks whose deadline < t
        uint16_t i = 0;
        while (i < ready_heap.size) {
            if (ready_heap.items[i].deadline < t) {
                if (should_print) {
                    printf("  Expired: Task (Cost: %u, Deadline: %u) missed deadline at t=%u! Removing.\n", 
                            ready_heap.items[i].cost, ready_heap.items[i].deadline, t);
                }
                deadlines_missed++;
                heap_remove_at(&ready_heap, i);
            } else {
                i++;
            }
        }

        // 4. Scheduling decision
        if (!heap_is_empty(&ready_heap)) {
            task_t top_task = ready_heap.items[0];
            if (should_print) {
                printf("  Scheduler Check: Top Task (Cost: %u, Deadline: %u) | Ready Queue Size: %u\n", 
                        top_task.cost, top_task.deadline, ready_heap.size);
            }

            if (energy_buffer >= top_task.cost) {
                task_t executed_task;
                heap_remove_min(&ready_heap, &executed_task);

                energy_buffer -= executed_task.cost;
                tasks_completed++;
                if (should_print) {
                    printf("  [EXECUTED]: Task (Cost: %u, Deadline: %u) finished! Remaining Energy: %u\n", 
                            executed_task.cost, executed_task.deadline, energy_buffer);
                }
            } else {
                if (should_print) {
                    printf("  [BLOCKED]: Insufficient energy (%u < %u). Waiting...\n", 
                            energy_buffer, top_task.cost);
                }
            }
        } else {
            if (should_print) {
                printf("  Scheduler Check: Ready queue is empty. Idle step.\n");
            }
        }
    }

    // Mark remaining waiting tasks after NUM_STEPS as missed
    if (ready_heap.size > 0) {
        if (MAX_PRINT_STEPS > 0) {
            printf("\n[ End of Simulation Cleanup ]\n");
            printf("  %u task(s) remaining in ready heap at t=%u marked as missed.\n", 
                    ready_heap.size, NUM_STEPS);
        }
        deadlines_missed += ready_heap.size;
    }

    printf("\n=== Student B: Min-Heap (Earliest Deadline First) Results ===\n");
    printf("Total Steps Simulated: %u\n", NUM_STEPS);
    printf("Tasks Completed:       %u / %u\n", tasks_completed, NUM_TASKS);
    printf("Deadlines Missed:      %u\n", deadlines_missed);
    printf("Wasted Energy:         %u units\n", wasted_energy);
    printf("Remaining Energy:      %u / %u\n", energy_buffer, ENERGY_MAX);
    printf("Heap Comparisons:      %" PRIu64 "\n", ready_heap.comparisons);

    return 0;
}