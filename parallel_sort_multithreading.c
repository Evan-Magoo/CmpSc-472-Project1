#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/resource.h>

// Array Size
#define ARRAY_SIZE 131072

// Global Variables
int array[ARRAY_SIZE];
int chunk_size;
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

// Thread routine for assigning local chunks and sorting them
void* chunk_sorting(void* arg) {
    int thread_id = *(int *)arg;
    int start = thread_id * chunk_size;
    int end = 0;

    // Last thread gets last elements
    if (thread_id == NUM_THREADS - 1) {
        end = ARRAY_SIZE - 1;
    } else {
        end = start + chunk_size - 1;
    }

    printf("\tThread %d: Sorting %d to %d\n", thread_id, start, end);
    fflush(stdout);

    // Copy local chunk into temporary array
    int local_size = end - start + 1;
    int *local_array = malloc(local_size * sizeof(int));
    memcpy(local_array, &array[start], local_size * sizeof(int));

    // Sort local chunk
    quickSort(local_array, 0, local_size - 1);

    // Copy sorted chunk back into main array
    memcpy(&array[start], local_array, local_size * sizeof(int));

    free(local_array);
    pthread_exit(NULL);
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

    int thread_count[] = {1, 2, 4, 8};  // Thread counts
    double performance[4];              // For storing execution times
    long memory_usage[4];               // For storing memory usage

    struct timespec c_start, c_end;
    long mem_before, mem_after;

    // Loop through the different thread counts
    for (int t = 0; t <4; t++) {
        NUM_THREADS = thread_count[t];
        chunk_size = ARRAY_SIZE / NUM_THREADS;

        // Label thread configurations
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

        // Print sample of unsorted array
        printf("    - Before sorting (first 20 elements):\n\t");
        for (int i = 0; i < 20; i++) {
            printf("%d ", array[i]);
        }
        printf("\n\n");

        // Record memory and time before sorting
        mem_before = get_memory_usage();
        clock_gettime(CLOCK_MONOTONIC, &c_start);

        // ---- Map Phase ------------------------------------------------------
        // Each thread sorts one chunk of array
        pthread_t threads[NUM_THREADS];
        int thread_ids[NUM_THREADS];

        printf("    - Sorting:\n");
        for (int i = 0; i < NUM_THREADS; i++) {
            thread_ids[i] = i;
            pthread_create(&threads[i], NULL, chunk_sorting, &thread_ids[i]);
        }

        // Wait for all threads to complete
        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_join(threads[i], NULL);
        }
        printf("\n\t - All threads finished -\n");


        // ---- Reduce Phase ---------------------------------------------------
        // Merge sorted chunks iteratively into single sorted array
        int step = chunk_size;
        while (step < ARRAY_SIZE) {
            for (int i = 0; i < ARRAY_SIZE; i += 2 * step) {
                int low = i;
                int mid = i + step - 1;
                int high = 0;
                if ((i + 2 * step - 1) < ARRAY_SIZE) {
                    high = i + 2 * step - 1;
                } else {
                    high = ARRAY_SIZE - 1;
                }
                merge(array, low, mid, high);
            }
            step *= 2; // Merge larger sections each pass
        }

        // Record memory and time after sorting
        mem_after = get_memory_usage();
        clock_gettime(CLOCK_MONOTONIC, &c_end);

        // Print sample after sorting
        printf("\n    - After sorting (first 20 elements):\n\t");
        for (int i = 0; i < 20; i++) {
            printf("%d ", array[i]);
        }
        printf("\n\n");

        // Calculate Execution time
        double execution_time = (c_end.tv_sec - c_start.tv_sec) + (c_end.tv_nsec - c_start.tv_nsec) / 1e9;
        printf("    - Execution Time: %f sec", execution_time);
        performance[t] = execution_time;

        // Calculate memory usage
        long mem_usage = mem_after - mem_before;
        printf("\n\n    - Memory Before: %ld KB", mem_before);
        printf("\n    - Memory After: %ld KB", mem_after);
        printf("\n\n    - Memory Delta: %ld KB", mem_usage);
        memory_usage[t] = mem_usage;

        printf("\n------------------------------------------------------------------------------------------------------------------------\n");
    }

    // Display performance summary for all thread configs
    printf("\nPerformance Summary:\n");
    printf("Threads\t   Time (s)\tMem Delta (KB)\n");
    for (int t = 0; t < 4; t++) {
        printf("%d\t   %.6f\t%ld\n", thread_count[t], performance[t], memory_usage[t]);
    }

    return 0;
}
