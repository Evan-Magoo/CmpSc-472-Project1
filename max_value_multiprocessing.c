#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <time.h>

// Array Size
#define ARRAY_SIZE 131072

// Global Variables
int *array;
int chunk_size;
int *global_max;
sem_t *mutex;
long *child_mem;
int NUM_PROCESSES;

// Returns current memory usage by program
long get_memory_usage() {
    FILE* fp = fopen("/proc/self/status", "r");
    if (fp == NULL) {
        perror("Error opening /proc/self/status");
        return 0;
    }
    char line[256];
    long vm_rss = -1;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%ld", &vm_rss);
            break;
        }
    }

    fclose(fp);
    return vm_rss;
}

// Computes maximum value within assigned chunk
void find_local_max(int id) {

    // Determine start and end for thread chunk
    int start = id * chunk_size;
    int end = 0;
    if (id == NUM_PROCESSES - 1) {
                end = ARRAY_SIZE - 1;
            } else {
                end = start + chunk_size - 1;
            }

    printf("\tProcess %d (PID=%d): sorting %d to %d\n", id, getpid(), start, end);
    fflush(stdout);

    // Compute local max
    int local_max = array[start];
    for (int i = start + 1; i <= end; i++) {
        if (array[i] > local_max) {
            local_max = array[i];
        }
    }

    // ---- Reduce Phase -------------------------------------------------------
    // Updates global max protected with mutex
    sem_wait(mutex);
    if (local_max > *global_max) {
        *global_max = local_max;
    }
    sem_post(mutex);

    // Collect child process memory usage
    child_mem[id] = get_memory_usage();

    _exit(0);
}

// Main Method
int main(void) {
    // Shared Memory
    array = mmap(NULL, ARRAY_SIZE * sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    global_max = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    mutex = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    child_mem = mmap(NULL, 8 * sizeof(long), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    printf("------------------------------------------------------------------------------------------------------------------------\n");
    int process_count[] = {1, 2, 4, 8}; // Thread configs
    double performance[4];              // For storing execution times
    long memory_usage[4];               // For storing memory Usage

    struct timespec c_start, c_end;

    // Loop through the different process counts
    for (int p = 0; p < 4; p++) {
        *global_max = array[0];
        NUM_PROCESSES = process_count[p];
        chunk_size = ARRAY_SIZE / NUM_PROCESSES;
        if (NUM_PROCESSES == 1){
            printf(" - %d PROCESS:\n", NUM_PROCESSES);
        } else {
            printf(" - %d PROCESSES:\n", NUM_PROCESSES);
        }

        // Generate & Fill Array
        srand(42);
        for (int i = 0; i < ARRAY_SIZE; i++) {
            array[i] = rand() % 100;
        }

        // Print Array
        printf("    - Array (first 20 elements):\n\t");
        for (int i = 0; i < 20; i++) {
            printf("%d ", array[i]);
        }
        printf("\n\n");

        // Record memory and time before execution
        long mem_before = get_memory_usage();
        clock_gettime(CLOCK_MONOTONIC, &c_start);

        // ---- Map Phase ------------------------------------------------------
        // Create processes to process chunks
        printf("    - Finding Global Max:\n");
        *global_max = array[0];
        sem_init(mutex, 1, 1);
        pid_t pids[NUM_PROCESSES];
        for (int i = 0; i < NUM_PROCESSES; i++) {
            pids[i] = fork();
            if(pids[i] == 0) {
                find_local_max(i);
                _exit(0);
            }
        }

        // Wait for all child process to finish
        for (int i = 0; i < NUM_PROCESSES; i++) {
            waitpid(pids[i], NULL, 0);
        }
        printf("\n\t - All processess finished -\n");

        // Output Result
        printf("\n    - Global Max: %d\n", *global_max);

        // Record memory and time after execution
        long mem_after = get_memory_usage();
        clock_gettime(CLOCK_MONOTONIC, &c_end);

        // Calculate total memory usage
        long total_child_mem = 0;
        for (int i = 0; i < NUM_PROCESSES; i++) {
            total_child_mem += child_mem[i];
        }
        memory_usage[p] = total_child_mem;
        printf("\n    - Total memory used by children: %ld KB\n", total_child_mem);

        // Calculate parent memory usage
        double execution_time = (c_end.tv_sec - c_start.tv_sec) + (c_end.tv_nsec - c_start.tv_nsec) / 1e9;
        printf("\n    - Execution Time: %f sec", execution_time);
        printf("\n\n    - Memory Before: %ld KB\n", mem_before);
        printf("    - Memory After: %ld KB\n", mem_after);
        printf("    - Memory Delta: %ld KB", mem_after - mem_before);

        printf("\n------------------------------------------------------------------------------------------------------------------------\n");
        performance[p] = execution_time;

    }

    // Print performance data for each process count
    printf("\nPerformance Summary:\n");
    printf("Processes  Time (s)\tMem Usage (KB)\n");
    for (int p = 0; p < 4; p++) {
        printf("%d\t   %.6f\t%ld\n", process_count[p], performance[p], memory_usage[p]);
    }

    // Destory semaphore and release memory
    sem_destroy(mutex);
    munmap(array, ARRAY_SIZE * sizeof(int));
    munmap(global_max, sizeof(int));
    munmap(mutex, sizeof(sem_t));
    munmap(child_mem, 8 * sizeof(long));
    return 0;
}
