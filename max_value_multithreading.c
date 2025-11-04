#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <string.h>

// Array Size
#define ARRAY_SIZE 131072

// Global Variables
int array[ARRAY_SIZE];
int chunk_size;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int global_max;
int NUM_THREADS;

// Returns current memory usage by program
long get_memory_usage() {
    FILE* fp = fopen("/proc/self/status", "r");
    if (fp == NULL) {
        perror("Error opening /proc/self/status");
        return 0;
    }
    char line[256];
    long vm_rss = -1;

    // Scan file until VmRSS field is found
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
void* find_local_max(void* arg) {
    int id = *(int *)arg;
    free(arg);

    // Determine start and end for thread chunk
    int start = id * chunk_size;
    int end = 0;
    if (id == NUM_THREADS - 1) {
        end = ARRAY_SIZE - 1;
    } else {
        end = start + chunk_size - 1;
    }

    printf("\tThread %d: Finding local max in %d to %d\n", id, start, end);
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
    pthread_mutex_lock(&mutex);
    if (local_max > global_max) {
        global_max = local_max;
    }
    pthread_mutex_unlock(&mutex);

    pthread_exit(NULL);
}

// Main Method
int main(void) {
    printf("------------------------------------------------------------------------------------------------------------------------\n");
    int thread_count[] = {1, 2, 4, 8};  // Thread configs
    double performance[4];              // For storing execution times
    long memory_usage[4];               // For storing memory usage

    struct timespec c_start, c_end;

    // Loop through the different thread counts
    for (int t = 0; t < 4; t++) {
        NUM_THREADS = thread_count[t];
        chunk_size = ARRAY_SIZE / NUM_THREADS;
        if (NUM_THREADS == 1){
            printf(" - %d THREAD:\n", NUM_THREADS);
        } else {
            printf(" - %d THREADS:\n", NUM_THREADS);
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
        // Creates threads to process chunks
        printf("    - Finding Global Max:\n");
        global_max = array[0];
        pthread_t threads[NUM_THREADS];
        for (int i = 0; i < NUM_THREADS; i++) {
            int *arg = malloc(sizeof(int));
            *arg = i;
            pthread_create(&threads[i], NULL, find_local_max, arg);
        }

        // Wait for all threads to complete
        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_join(threads[i], NULL);
        }
        printf("\n\t - All threads finished -\n");

        // Output Resutl
        printf("\n    - Global Max: %d\n", global_max);

        // Record memory and time after execution
        clock_gettime(CLOCK_MONOTONIC, &c_end);
        long mem_after = get_memory_usage();

        // Calculate exeuction time
        double execution_time = (c_end.tv_sec - c_start.tv_sec) + (c_end.tv_nsec - c_start.tv_nsec) / 1e9;
        printf("\n    - Execution Time: %f sec", execution_time);
        performance[t] = execution_time;

        // Caluclate memory usage
        memory_usage[t] = mem_after - mem_before;
        printf("\n\n    - Memory Before: %ld KB\n", mem_before);
        printf("    - Memory After: %ld KB\n", mem_after);
        printf("    - Memory Delta: %ld KB", mem_after - mem_before);

        printf("\n------------------------------------------------------------------------------------------------------------------------\n");
    }
    pthread_mutex_destroy(&mutex); // Destory mutex for all threads

    // Print performance summary for all threads
    printf("\nPerformance Summary:\n");
    printf("Thread\t   Time (s)\tMem Delta (KB)\n");
    for (int t = 0; t < 4; t++) {
        printf("%d\t   %.6f\t%ld\n", thread_count[t], performance[t], memory_usage[t]);
    }

    return 0;
}
