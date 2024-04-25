#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

#define DATA_ARRY_SIZE 100000
#define NUM_OF_W_THREADS 100
#define NUM_OF_WRITES_PER_THREAD 50

int data_arr[DATA_ARRY_SIZE] = {0};
int32_t a_head_ptr = 0;
int32_t a_virtual_head_ptr = 0;
int32_t a_tail_ptr = 0;

// H & T road map:
//    1           7
//    T           H
// |-|*|*|*|*|*|*|-|-|-|-|-|

int32_t circular_gap_size(int32_t head, int32_t tail)
{
    if (tail >= head)
    {
        return circular_gap_size(DATA_ARRY_SIZE, tail - head);
    }

    return head - tail;
}

int write_to_arry(const int *data_to_write)
{
    assert(data_to_write);

    int32_t new_virtual_head = 0;
    int32_t virtual_head_val = __atomic_load_n(&a_virtual_head_ptr, __ATOMIC_SEQ_CST);
    int32_t tail_val = __atomic_load_n(&a_tail_ptr, __ATOMIC_SEQ_CST);

    while (circular_gap_size(tail_val, virtual_head_val) > 0)
    {
        new_virtual_head = virtual_head_val + 1;

        // Verify that no other queue writing thread has updated the virtual head pointer yet
        if (__atomic_compare_exchange_n(&a_virtual_head_ptr, &virtual_head_val, new_virtual_head, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        {
            data_arr[virtual_head_val % DATA_ARRY_SIZE] = *data_to_write;

            // Wait until the virtual head points to the exact index 'back', indicating that all previous threads have successfully completed their writes.
            while (!__atomic_compare_exchange_n(&a_head_ptr, &virtual_head_val, new_virtual_head, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            {
                usleep(10000);
                // sience that in case of failure, the atomic func replaces the 'expected' param with the actual value, we'll update it again
                virtual_head_val = new_virtual_head - 1;
            }

            return 0;
        }

        // the virtual head was taken by other thread, let's try again.
        virtual_head_val = __atomic_load_n(&a_virtual_head_ptr, __ATOMIC_SEQ_CST);
        tail_val = __atomic_load_n(&a_tail_ptr, __ATOMIC_SEQ_CST);
    }

    // faied to write, probably buffer is full
    return -1;
}

int read_from_arry(int *out_data)
{
    // Y do I have to validate everything myself here?
    assert(out_data);

    uint32_t head_val = __atomic_load_n(&a_head_ptr, __ATOMIC_SEQ_CST);
    uint32_t tail_val = __atomic_load_n(&a_tail_ptr, __ATOMIC_SEQ_CST);
    uint32_t new_tail = 0;

    if (head_val != tail_val && circular_gap_size(head_val, tail_val) > 0)
    {
        *out_data = data_arr[tail_val % DATA_ARRY_SIZE];
        new_tail = tail_val + 1;

        if (__atomic_compare_exchange_n(&a_tail_ptr, &tail_val, new_tail, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        {
            return 0;
        }
        else{
            assert(false); // for now only 1 reader is supported- this has to work!
        }
    }

    usleep(1000);

    // failed to read, probably buffer is empty.
    return -1;
}

void *writer_thread(void *vargp)
{
    uint16_t loop_len = *((uint16_t *)vargp);
    for (int i = 0; i < loop_len; ++i)
    {
        while(write_to_arry(&i));
    }

    return NULL;
}

void *reader_thread(void *vargp)
{
    int32_t tot_expected_data = 0;
    int32_t tot_data = 0;
    int new_data = 0;
    int i, j;

    for (i = 0; i < NUM_OF_W_THREADS; i++)
    {
        for (j = 0; j < NUM_OF_WRITES_PER_THREAD; j++)
        {
            tot_expected_data += j;

            while(read_from_arry(&new_data));
            tot_data += new_data;
        }
    }

    if(tot_data == tot_expected_data)
    {
        printf("All data is correct! 👌\n");
    }
    else
    {
        printf("Some data is missing! 👎\n");
    }

    return NULL;
}

int main() {
    int i;
    clock_t time;
    double cpu_time_used;
    pthread_t r_thread_id;
    pthread_t w_thread_ids[NUM_OF_W_THREADS];
    uint32_t writes_per_thread = NUM_OF_WRITES_PER_THREAD;

    time = clock();

    pthread_create(&r_thread_id, NULL, reader_thread, NULL);

    for (i = 0; i < NUM_OF_W_THREADS; i++)
    {
        printf("started writing thraed #%d\n", i);
        pthread_create(&w_thread_ids[i], NULL, writer_thread, (void *)&writes_per_thread);
    }

    for (i = 0; i < NUM_OF_W_THREADS; i++)
    {
        pthread_join(w_thread_ids[i], NULL);
    }
    pthread_join(r_thread_id, NULL);

    time = clock();

    cpu_time_used = ((double)time)/CLOCKS_PER_SEC;

    printf("Time taken: %f seconds\n", cpu_time_used);

    return 0;
}
