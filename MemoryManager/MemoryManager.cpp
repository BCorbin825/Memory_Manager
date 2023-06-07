#include "MemoryManager.h"

// Constructor - Sets native word size (in bytes, for alignment) and default allocator for finding a memory hole.
MemoryManager::MemoryManager(unsigned wordSize, function<int(int, void*)> allocator) {
    this->wordSize = wordSize;
    this->allocator = allocator;
}
// Destructor - Releases all memory allocated by this object without leaking memory.
MemoryManager::~MemoryManager() {
    shutdown();
}

// Instantiates block of requested size, no larger than 65536 words; cleans up previous block if applicable.
void MemoryManager::initialize(size_t sizeInWords) {
    // Clean up previous block
    shutdown(); 

    // Set memory to appropriate size
    memSize = (sizeInWords > 65535) ? 65535 : sizeInWords;
    memory.resize(memSize*wordSize);
    holes.push_back(make_pair(0, memSize));

    // Bitmap initialization
    int bitmapSize = 0;
    if (memSize % 8 == 0) { bitmapSize = memSize; }
    else { bitmapSize = memSize + 8 - (memSize % 8); } // Add extra 0s for bitmap return simplification
    bitmap.resize(bitmapSize);
}

// Releases memory block acquired during initialization, if any. This should only include memory created for
// long term use not those that returned such as getList() or getBitmap() as whatever is calling those should
// delete it instead.
void MemoryManager::shutdown() {
    memory.clear();
    holes.clear();
    partitions.clear();
    bitmap.clear();
}

// Allocates a memory partition using the allocator function. If no memory is available or size is invalid, returns nullptr.
void* MemoryManager::allocate(size_t sizeInBytes) {
    // Check for invalid allocation
    if (sizeInBytes > memSize*wordSize || sizeInBytes < 0 || memory.size() == 0) { return nullptr; }

    unsigned words = ceil(sizeInBytes/wordSize);

    // Get holes array and find allocation offset
    uint16_t* list = (uint16_t*)getList();
    int holeSelected = allocator(words, list);
    delete [] list; 
    if (holeSelected == -1) { return nullptr; } // No hole can fit allocation

    // Find index of hole in holes container
    unsigned index = 0;
    for (unsigned i = 0; i < holes.size(); i++) {
        if (holes[i].first == holeSelected) {
            index = i;
            break;
        }
    }

    // If allocation fits perfectly in hole just erase the hole
    if (holes[index].second == words) { 
        holes.erase(holes.begin() + index);
    }
    else { // Otherwise create a smaller hole with new word offset
        holes[index].first = holeSelected + words;
        holes[index].second = holes[index].second - words;
    }

    // Keep track of memory partitions
    partitions.push_back(make_pair(holeSelected, words));
    sort(partitions.begin(), partitions.end());

    // Fill bitmap with ones where words were allocated
    auto start = bitmap.begin() + holeSelected;
    fill(start, start + words, 1);

    return &memory[holeSelected*wordSize];
}

// Frees the memory partition within the memory manager so that it can be reused.
void MemoryManager::free(void* address) {
    // Find word offset in memory of the address we are freeing
    unsigned wordOffset = 0;
    for (unsigned i = 0; i < memory.size(); i += wordSize) {
        if (&memory[i] == address) {
            wordOffset = i/wordSize;
        }
    }
    
    // Remove partition from partitions container
    unsigned length = 0;
    for (unsigned i = 0; i < partitions.size(); i++) {
        if (partitions[i].first == wordOffset) {
            length = partitions[i].second;
            partitions.erase(partitions.begin() + i);
            break;
        }
    }

    // Create new hole for freed memory
    pair<int, int> combine = make_pair(wordOffset, length); 

    // Combine holes if possible
    int erase[2] = {-1, -1};
    for (unsigned i = 0; i < holes.size(); i++) {
        // If there is a hole that ends right before the freed memory, combine the holes
        if (holes[i].first + holes[i].second == wordOffset) {
            combine = make_pair(holes[i].first, holes[i].second + length);
            erase[0] = i;
        }
        // If there is a hole is right after the freed memory, combine the holes
        if (holes[i].first == combine.first + combine.second) {
            combine = make_pair(combine.first, combine.second + holes[i].second);
            erase[1] = i;
            break;
        }
    }
    
    // Erase pairs that have been combined
    if (erase[0] != -1) { holes.erase(holes.begin() + erase[0]); }
    if (erase[1] != -1) { holes.erase(holes.begin() + erase[1]); }

    // Add new hole to holes container and sort
    holes.push_back(combine);
    sort(holes.begin(), holes.end());

    // Fill bitmap with zeros where words were freed
    auto start = bitmap.begin() + wordOffset;
    fill(start, start + length, 0);
}

// Changes the allocation algorithm to identifying the memory hole to use for allocation.
void MemoryManager::setAllocator(std::function<int(int, void*)> allocator) {
    this->allocator = allocator;
}

// Uses standard POSIX calls to write hole list to filename as text, returning -1 on error and 0 if successful.
// Format: "[START, LENGTH] - [START, LENGTH] …", e.g., "[0, 10] - [12, 2] - [20, 6]".
int MemoryManager::dumpMemoryMap(char* filename) {
    if (memory.size() == 0) { return -1; }

    // Open/create a new file
    int fd = open(filename, O_CREAT | O_RDWR, S_IRWXU);
    if (fd == -1) { return -1; }

    string out;
    char buf[128];
    if (holes.size() == 0) {
        out = "[0, 0]";
        strcpy(buf, out.c_str());
        write(fd, buf, strlen(buf));
        close(fd);
        return 0;
    }

    // Write holes list to output file with specified format
    for (int i = 0; i < holes.size()-1; i++) {
        out = "[" + to_string(holes[i].first) + ", " + to_string(holes[i].second) + "] - ";
        strcpy(buf, out.c_str());
        write(fd, buf, strlen(buf));
    }
    out = "[" + to_string(holes[holes.size()-1].first) + ", " + to_string(holes[holes.size()-1].second) + "]";
    strcpy(buf, out.c_str());
    write(fd, buf, strlen(buf));

    close(fd);
    return 0;
}

// Returns an array of information (in decimal) about holes for use by the allocator function (little-Endian).
// Offset and length are in words. If no memory has been allocated, the function should return a NULL pointer.
// Format: [(# holes), (hole 0 offset), (hole 0 length), (hole 1 offset), (hole 1 length), ...] 
// Example: [3, 0, 10, 12, 2, 20, 6]
void* MemoryManager::getList() {
    if (memory.size() == 0) { return nullptr; }

    int listSize = (holes.size()*2) + 1;
    uint16_t* holesArray = new uint16_t[listSize];
    holesArray[0] = holes.size();
    
    for (unsigned i = 0; i < holes.size(); i++) {
        holesArray[(i*2)+1] = holes[i].first;
        holesArray[(i*2)+2] = holes[i].second;
    }
    return holesArray;
}

// Returns a bit-stream of bits in terms of an array representing whether words are used (1) or free (0). The
// first two bytes are the size of the bitmap (little-Endian); the rest is the bitmap, word-wise.
void* MemoryManager::getBitmap() {
    if (memory.size() == 0) { return nullptr; }
    
    uint16_t size = bitmap.size()/8;
    uint8_t* bits = new uint8_t[size + 2];

    // Store size of bitmap (Little-Endian)
    bits[0] = (uint8_t)(size >> 0);
    bits[1] = (uint8_t)(size >> 8);

    unsigned byte = 0;
    for (unsigned i = 0; i < bitmap.size(); i += 8) {
        for (int j = 7; j >= 0; j--) { // Combine words into bytes representation 
            byte |= bitmap[i+j] << j; // Reverses order for Little-Endian
        }
        bits[(i/8)+2] = (uint8_t)byte;
        byte = 0;
    }
    return bits;
}

// Returns the word size used for alignment.
unsigned MemoryManager::getWordSize() {
    return wordSize;
}

// Returns the byte-wise memory address of the beginning of the memory block.
void* MemoryManager::getMemoryStart() {
    return &memory[0];
}

// Returns the byte limit of the current memory block.
unsigned MemoryManager::getMemoryLimit() {
    return memory.size();
}

// --------------Memory allocation algorithms-------------- //

// Find hole that can best fit partition being allocated
int bestFit(int sizeInWords, void* list) {
    uint16_t* holes = (uint16_t*)list;
    uint16_t holeListlength = *holes;

    int bestFitHole[2] = {-1, 65536}; 
    for (int i = 1; i < (holeListlength * 2); i += 2) { 
        // If current hole is smaller than bestFitHole, update bestFitHole
        if (holes[i+1] < bestFitHole[1] && holes[i+1] >= sizeInWords) { 
            bestFitHole[0] = holes[i];
            bestFitHole[1] = holes[i+1];
        }
    }
    return bestFitHole[0];
}

// Find largest hole in partition
int worstFit(int sizeInWords, void* list) {
    uint16_t* holes = (uint16_t*)(list);
    uint16_t holeListlength = *holes;

    int worstFitHole[2] = {-1, -1}; 
    for (uint16_t i = 1; i < (holeListlength * 2); i += 2) {
        // If current hole is bigger than worst so far, update worst with current
        if (holes[i+1] > worstFitHole[1] && holes[i+1] >= sizeInWords) { 
            worstFitHole[0] = holes[i];
            worstFitHole[1] = holes[i+1];
        }
    }
    return worstFitHole[0];
}