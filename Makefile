CXXFLAGS := -Wall -Werror -O3 -std=c++11
LDFLAGS := -ltbb -fopenmp

BIN = parSort.out

all: $(BIN)

parSort.out: parSort.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< $(LDFLAGS)

clean:
	rm -f *~ *.out *.o
