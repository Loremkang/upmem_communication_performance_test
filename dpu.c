#include <stdio.h>
#include <defs.h>
#include <mram.h>
#include <perfcounter.h>

__host int64_t DPU_ID;
const int SIZE = 60 << 20; // 60 MB
const int BUFFERSIZE = SIZE / sizeof(uint64_t);
// __mram uint64_t haha[1 << 20];
// __mram uint64_t buffer[BUFFERSIZE];

void fill() {
    __mram_ptr uint64_t* buffer = (__mram_ptr uint64_t*) DPU_MRAM_HEAP_POINTER;
    // printf("%x\n", DPU_MRAM_HEAP_POINTER);
    // printf("Hello World!\n");
    // uint64_t offset = (DPU_ID << 32);
    // for (int i = 0; i < BUFFERSIZE; i ++) {
    //     buffer[i] = offset + (uint64_t)(buffer + i);
    //     // haha[i] = buffer[i];
    // }
    // for (int i = 0; i < 2; i ++) {
    //     printf("%x %x\n", buffer + i, buffer + i);
    // }
    // printf("id=%lld min=%16llx max=%16llx\n", DPU_ID, buffer[0], buffer[BUFFERSIZE - 1]);
}

// __mram uint64_t val[1 << 20];

int main() {
    fill();
    // int k = 1 << 20;
    // for (int i = 0; i < k; i ++) {
    //     val[i] = me();
    // }
    // printf("Hello World!\n");
    return 0;
}
