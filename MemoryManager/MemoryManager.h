#include <iostream>
#include <vector>
#include <functional>
#include <algorithm>
#include <fstream>
#include <string>
#include <cmath>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>

using namespace std;

int bestFit(int sizeInWords, void* list);
int worstFit(int sizeInWords, void* list);

class MemoryManager {
    private:
        vector<char> memory;
        vector<pair<unsigned, unsigned>> holes;
        vector<pair<unsigned, unsigned>> partitions;
        vector<int> bitmap;
        unsigned wordSize;
        size_t memSize;
        function<int(int, void*)> allocator;

    public:
        // Constructor - Sets native word size (in bytes, for alignment) and default allocator for finding a memory hole.
        MemoryManager(unsigned wordSize, function<int(int, void*)> allocator);
        // Destructor - Releases all memory allocated by this object without leaking memory.
        ~MemoryManager();

        // --------------------------------------------Class Functions-------------------------------------------- //

        // Instantiates block of requested size, no larger than 65536 words; cleans up previous block if applicable.
        void initialize(size_t sizeInWords);

        // Releases memory block acquired during initialization, if any. This should only include memory created for
        // long term use not those that returned such as getList() or getBitmap() as whatever is calling those should
        // delete it instead.
        void shutdown();

        // Allocates a memory using the allocator function. If no memory is available or size is invalid, returns nullptr.
        void* allocate(size_t sizeInBytes);

        // Frees the memory block within the memory manager so that it can be reused.
        void free(void* address);

        // Changes the allocation algorithm to identifying the memory hole to use for allocation.
        void setAllocator(std::function<int(int, void*)> allocator);

        // Uses standard POSIX calls to write hole list to filename as text, returning -1 on error and 0 if successful.
        // Format: "[START, LENGTH] - [START, LENGTH] …", e.g., "[0, 10] - [12, 2] - [20, 6]".
        int dumpMemoryMap(char* filename);

        // Returns an array of information (in decimal) about holes for use by the allocator function (little-Endian).
        // Offset and length are in words. If no memory has been allocated, the function should return a NULL pointer.
        // Format: [(# holes), (hole 0 offset), (hole 0 length), (hole 1 offset), (hole 1 length), ...] 
        // Example: [3, 0, 10, 12, 2, 20, 6]
        void* getList();
        
        // Returns a bit-stream of bits in terms of an array representing whether words are used (1) or free (0). The
        // first two bytes are the size of the bitmap (little-Endian); the rest is the bitmap, word-wise.
        void* getBitmap();

        // Returns the word size used for alignment.
        unsigned getWordSize();

        // Returns the byte-wise memory address of the beginning of the memory block.
        void* getMemoryStart();

        // Returns the byte limit of the current memory block.
        unsigned getMemoryLimit();
};