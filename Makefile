all:
	g++ -std=c++17 -o CommandLineTest CommandLineTest.cpp -L ./MemoryManager -lMemoryManager
clean:
	rm CommandLineTest 
	rm -f testComplexBestFit.txt 
	rm -f testNewAllocator.txt 
	rm -f testRepeatedShutdown.txt 
	rm -f testSimpleBestFit.txt 
	rm -f testSimpleFirstFit.txt