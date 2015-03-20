#include <iostream>
#include <vector>
#include <random>
#include <limits>
#include <cassert>
#include <algorithm>
#include <parallel/algorithm>
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

// UTILITY SORTS
void serialInsertionSort(vector<int>::iterator l, vector<int>::iterator r) {
  for(auto i = l; i != r; ++i) {
    auto key = *i;
    auto j = i - 1;

    for(; j != l - 1 && *j > key; --j)
      *(j + 1) = *j;
    *(j + 1) = key;
  }
}

// SERIAL SORTS
void serialQuickSort(vector<int>::iterator l, vector<int>::iterator r) {
  auto size = std::distance(l, r);

  // Base case
  if (size < 2)
    return;

  if (INTROSPECTIVE && size <= 10){
    serialInsertionSort(l, r);
    return;
  }

  // Partition with center pivot
  --r;
  auto pivot = l + size/2;
  auto pivotVal = *pivot;
  swap(*pivot, *r);
  pivot = std::partition(l, r, [pivotVal](int i){return i < pivotVal;});
  swap(*pivot, *r);

  // Div and con
  serialQuickSort(l, pivot);
  serialQuickSort(pivot+1, r+1);
}

void serialMergeSortHelper(vector<int>::iterator l, vector<int>::iterator r, vector<int>::iterator buf) {
  auto size = std::distance(l, r);

  // Base case
  if (size < 2)
    return;

  if (INTROSPECTIVE && size <= 10){
    serialInsertionSort(l, r);
    return;
  }

  // Recurse
  auto mid = l + size/2;
  serialMergeSortHelper(l, mid, buf);
  serialMergeSortHelper(mid, r, std::next(buf, size/2));

  // Merge
  std::merge(l, mid, mid, r, buf);
  std::copy(buf, std::next(buf, size), l);
}

void serialMergeSort(vector<int>::iterator l, vector<int>::iterator r) {
  auto buf = vector<int>(std::distance(l, r) - 1);
  serialMergeSortHelper(l, r, buf.begin());
}

void serialGnuSort(vector<int>::iterator l, vector<int>::iterator r) {
  std::sort(l, r);
}

// PARALLEL SORTS
void parallelQuickSort(vector<int>::iterator l, vector<int>::iterator r) {
  auto size = std::distance(l, r);

  // Base case
  if (size < 2)
    return;

  if (size < CUTOFFQS){
    serialQuickSort(l, r);
    return;
  }

  // Partition with center pivot
  --r;
  auto pivot = l + size/2;
  auto pivotVal = *pivot;
  swap(*pivot, *r);
  pivot = std::partition(l, r, [pivotVal](int i){return i < pivotVal;});
  swap(*pivot, *r);


  // Div and con
  tbb::parallel_invoke([=]{parallelQuickSort(l, pivot);},
                       [=]{parallelQuickSort(pivot+1, r+1);});
}

void parallelMerge(vector<int>::iterator l1, vector<int>::iterator r1,
                   vector<int>::iterator l2, vector<int>::iterator r2,
                   vector<int>::iterator out) {
  // Parallel merge is a necessary evil
  auto size1 = std::distance(l1, r1);
  auto size2 = std::distance(l2, r2);
  auto size = size1 + size2;
  if (size < CUTOFFMSMERGE) {
    merge(l1, r1, l2, r2, out);
    return;
  }

  vector<int>::iterator mid1, mid2;
  if (size1 < size2) {
    mid2 = l2+size2/2;
    mid1 = std::upper_bound(l1, r1, *mid2);
  } else {
    mid1 = l1+size1/2;
    mid2 = std::lower_bound(l2, r2, *mid1);
  }
  tbb::parallel_invoke([=]{parallelMerge(l1, mid1, l2, mid2, out);},
                       [=]{parallelMerge(mid1, r1, mid2, r2, std::next(out, (mid1-l1) + (mid2-l2)));});
}

void parallelMergeSortHelper(vector<int>::iterator l, vector<int>::iterator r, vector<int>::iterator buf) {
  auto size = std::distance(l, r);

  // Base case
  if (size < 2)
    return;

  if (size < CUTOFFMSSORT){
    // No point in allocating the buffer again...
    serialMergeSortHelper(l, r, buf);
    return;
  }

  // Recurse
  auto mid = l + size/2;
  tbb::parallel_invoke([=]{parallelMergeSortHelper(l, mid, buf);},
                       [=]{parallelMergeSortHelper(mid, r, std::next(buf, size/2));});

  // Merge
  //std::merge(l, mid, mid, r, buf);
  parallelMerge(l, mid, mid, r, buf);
  std::copy(buf, std::next(buf, size), l);
}

void parallelMergeSort(vector<int>::iterator l, vector<int>::iterator r) {
  auto buf = vector<int>(std::distance(l, r) - 1);
  parallelMergeSortHelper(l, r, buf.begin());
}

void parallelGnuSort(vector<int>::iterator l, vector<int>::iterator r) {
  __gnu_parallel::sort(l, r);
}

void parallelIntelSort(vector<int>::iterator l, vector<int>::iterator r) {
  tbb::parallel_sort(l, r);
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
    SortFunc{::serialGnuSort, "Serial GNU Introsort"},
    SortFunc{::serialQuickSort, "Serial Quicksort"},
    SortFunc{::serialMergeSort, "Serial Mergesort"},
    SortFunc{::parallelGnuSort, "Parallel GNU Sort"},
    SortFunc{::parallelIntelSort, "Parallel Intel Sort"},
    SortFunc{::parallelQuickSort, "Parallel Quicksort"},
    SortFunc{::parallelMergeSort, "Parallel Mergesort"}
  };

  for (long i = STARTSIZE; i < MAXSIZE; i*=2){
    cout << "Running tests on size " << readableSize(i*sizeof(int)) << endl;
    for (auto& sortFunc : sortFunctions) {
      sortFunc.times.push_back(runTest(sortFunc, i));
    }
  }
  return 0;
}
