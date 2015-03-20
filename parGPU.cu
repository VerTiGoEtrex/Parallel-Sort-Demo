#include <iostream>
#include <vector>
#include <random>
#include <limits>
#include <cassert>
#include <algorithm>
#include <functional>
#include <string>
#include <array>
#include <tbb/parallel_for.h>
#include <tbb/parallel_invoke.h>
#include <tbb/parallel_sort.h>

extern "C" {
#include <sys/time.h>
}

using std::cout;
using std::endl;
using std::vector;
using std::function;
using std::string;
using std::swap;

// PARAMS
static const long MAXSIZE = pow(2,33) / sizeof(int); // 4GB (can't do 8, since mergesort needs copies of the list)
//static const long MAXSIZE = pow(2,24) / sizeof(int); // 4GB (can't do 8, since mergesort needs copies of the list)a
static const long STARTSIZE = pow(2, 20) / sizeof(int); // 1MB
static const int CUTOFFQS = 500;
static const int CUTOFFMSSORT = 500;
static const int CUTOFFMSMERGE = 2000;
static const bool INTROSPECTIVE = true;

// STRUCTS
struct SortFunc {
  function<void(vector<int>::iterator, vector<int>::iterator)> func;
  string name;
  vector<double> times;

  SortFunc(function<void(vector<int>::iterator, vector<int>::iterator)> f, string n) {
    func = f;
    name = n;
  }
};


// UTIL
string readableSize(long size) {
  int i = 0;
  const char* units[] = {"B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
  while (size > 1024) {
    size /= 1024;
    i++;
  }
  return std::to_string(size) + " " + units[i];
}

double getWallTime() {
  timeval timeofday;
  gettimeofday( &timeofday, NULL );
  return timeofday.tv_sec + timeofday.tv_usec / 1000000.0;
}

vector<int> createRandomList(long n) {
  assert(n>=0);
  auto gen = std::default_random_engine();
  auto dist =  std::uniform_int_distribution<int>(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
  vector<int> randomList(n);
  std::generate(randomList.begin(), randomList.end(), [=]()mutable{ return dist(gen);});
  assert(randomList.size() == (unsigned long) n);
  return randomList; //Optimizer will do named return type move
}

void parallelGnuSort(vector<int>::iterator l, vector<int>::iterator r) {
  __gnu_parallel::sort(l, r);
}

void parallelCudaSort(vector<int>::iterator l, vector<int>::iterator r) {
  
}


double runTest(SortFunc &sortFunc, long i){
  assert(i >= 0);
  auto listToSort = createRandomList(i);
  auto startTime = getWallTime();
  cout << " [-] SORT: " << sortFunc.name << "\t " << std::flush;
  sortFunc.func(listToSort.begin(), listToSort.end());
  auto totalTime = getWallTime() - startTime;
  //assert(is_sorted(listToSort.begin(), listToSort.end()));
  printf("TIME: %.3f seconds\n", totalTime);
  return totalTime;
}

int main(int argc, char *argv[]) {
  std::array<SortFunc, 7> sortFunctions = {
    SortFunc{::parallelGnuSort, "Parallel GNU Sort"},
    SortFunc{::parallelCudaSort, "Parallel CUDA Sort"}
  };

  for (long i = STARTSIZE; i < MAXSIZE; i*=2){
    cout << "Running tests on size " << readableSize(i*sizeof(int)) << endl;
    for (auto& sortFunc : sortFunctions) {
      sortFunc.times.push_back(runTest(sortFunc, i));
    }
  }
  return 0;
}
