#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>

// Array Size
#define ARRAY_SIZE 131072

// Global Variables
int *array;
int chunk_size;
int NUM_PROCESSES;
long *shared_mem_usage;

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

// Recursieve QuickSort Implementation
void quickSort(int *array, int low, int high) {
    if (low < high) {
        int pivot = array[low];
        int i = low;
        int j = high;

        // Partitioning
        while (i < j) {
            // Find first element larger than pivot
            while (array[i] <= pivot && i <= high - 1) {
                i++;
            }
            // Find first element smaller than pivot
            while (array[j] > pivot && j >= low + 1) {
                j--;
            }
            if (i < j) {
                // Swap elements
                int temp = array[i];
                array[i] = array[j];
                array[j] = temp;
            }
        }

        // Place pivot in correct position
        int temp = array[low];
        array[low] = array[j];
        array[j] = temp;

        // Recursive calls
        quickSort(array, low, j - 1);
        quickSort(array, j + 1, high);
    }
}

// Merge two sorted subarrays into single sorted array
void merge(int *array, int low, int mid, int high) {
    int n1 = mid - low + 1;
    int n2 = high - mid;

    // Create temp arrays
    int *left = malloc(n1 * sizeof(int));
    int *right = malloc(n2 * sizeof(int));

    // Copy data to temp arrays
    for (int i = 0; i < n1; i++) {
        left[i] = array[low + i];
    }
    for (int j = 0; j < n2; j++) {
        right[j] = array[mid + 1 + j];
    }

    // Merge until one array runs out
    int i = 0;
    int j = 0;
    int k = low;
    while (i < n1 && j < n2) {
        if (left[i] <= right[j]) {
            array[k++] = left[i++];
        } else {
            array[k++] = right[j++];
        }
    }

    // Copy remaining elements
    while (i < n1) {
        array[k++] = left[i++];
    }
    while (j < n2) {
        array[k++] = right[j++];
    }

    free(left);
    free(right);
}

// Main Method
int main(void) {
    printf("------------------------------------------------------------------------------------------------------------------------\n");
    int process_count[] = {1, 2, 4, 8}; // Worker configs
    double performance[4];              // For storing execution times
    long memory_usage[4];               // For storing memory usage

    struct timespec c_start, c_end;

    // Loop through the different process counts
    for (int p = 0; p < 4; p++) {
        NUM_PROCESSES = process_count[p];
        chunk_size = ARRAY_SIZE / NUM_PROCESSES;

        // Label process configurations
        if (NUM_PROCESSES == 1){
            printf(" - %d PROCESS:\n", NUM_PROCESSES);
        } else {
            printf(" - %d PROCESSES:\n", NUM_PROCESSES);
        }

        // Set up shared memory
        array = mmap(NULL, ARRAY_SIZE * sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        shared_mem_usage = mmap(NULL, 8 * sizeof(long), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

        // Generate & Fill Array
        srand(42);
        for (int i = 0; i < ARRAY_SIZE; i++) {
            array[i] = rand() % 100;
        }

        // Print sample of unsorted array
        printf("    - Before sorting (first 20 elements):\n\t");
        for (int i = 0; i < 20; i++) {
            printf("%d ", array[i]);
        }
        printf("\n\n");

        // Record time before sorting
        clock_gettime(CLOCK_MONOTONIC, &c_start);

        // ---- Map Phase ------------------------------------------------------
        // Each procress sorts one chunk of array
        printf("    - Sorting:\n");
        pid_t pids[NUM_PROCESSES];
        for (int i = 0; i < NUM_PROCESSES; i++) {
            pids[i] = fork();
            if (pids[i] == 0) {
                int start = i * chunk_size;
                int end = 0;
                if (i == NUM_PROCESSES - 1){
                    end = ARRAY_SIZE - 1;
                } else {
                    end = start + chunk_size - 1;
                }

                printf("\tProcess %d (PID=%d): sorting %d to %d\n", i, getpid(), start, end);
                fflush(stdout);

                int local_size = end - start + 1;
                int *local_array = malloc(local_size * sizeof(int));
                memcpy(local_array, &array[start], local_size * sizeof(int));
                quickSort(local_array, 0, local_size - 1);
                memcpy(&array[start], local_array, local_size * sizeof(int));\
                free(local_array);

                shared_mem_usage[i] = get_memory_usage();
                _exit(0);

            }
        }

        // Wait for all processes to complete
        for (int i = 0; i < NUM_PROCESSES; i++) {
            waitpid(pids[i], NULL, 0);
        }
        printf("\n\t - All processess finished -\n");

        // Get total memory usage of processes
        long total_memory = 0;
        for (int i = 0; i < NUM_PROCESSES; i++) {
            total_memory += shared_mem_usage[i];
        }
        printf("\n    - Total memory used by children: %ld KB\n", total_memory);
        memory_usage[p] = total_memory;

        // ---- Reduce Phase ---------------------------------------------------
        // Merge sorted chunks iteratively into a single sorted array
        int step = chunk_size;
        while (step < ARRAY_SIZE) {
            for (int i = 0; i < ARRAY_SIZE; i += 2 * step) {
                int low = i;
                int mid = i + step - 1;
                int high = (i + 2 * step - 1 < ARRAY_SIZE) ? (i + 2 * step - 1) : (ARRAY_SIZE - 1);
                merge(array, low, mid, high);
            }
            step *= 2; // Merge larger sections each pass
        }

        // Record time after sorting
        clock_gettime(CLOCK_MONOTONIC, &c_end);

        // Print sample after sorting
        printf("\n    - After sorting (first 20 elements):\n\t");
        for (int i = 0; i < 20; i++) {
            printf("%d ", array[i]);
        }
        printf("\n\n");

        // Calculate execution time
        double execution_time = (c_end.tv_sec - c_start.tv_sec) + (c_end.tv_nsec - c_start.tv_nsec) / 1e9;
        printf("    - Execution Time: %.6f sec", execution_time);
        performance[p] = execution_time;

        printf("\n------------------------------------------------------------------------------------------------------------------------\n");
    }

    printf("\nPerformance Summary:\n");
    printf("Processes  Time (s)\tMem Usage (KB)\n");
    for (int p = 0; p < 4; p++) {
        printf("%d\t   %.6f\t%ld\n", process_count[p], performance[p], memory_usage[p]);
    }

    return 0;
}
