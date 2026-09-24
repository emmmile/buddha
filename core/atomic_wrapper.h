#include <atomic>
using namespace std;

template <typename T>
struct atomic_wrapper
{
  std::atomic<T> _a;

  atomic_wrapper()
    :_a()
  {}

  atomic_wrapper(const std::atomic<T> &a)
    :_a(a.load(memory_order_relaxed))
  {}

  atomic_wrapper(const atomic_wrapper &other)
    :_a(other._a.load(memory_order_relaxed))
  {}

  atomic_wrapper &operator=(const atomic_wrapper &other)
  {
    _a.store(other._a.load(memory_order_relaxed), memory_order_relaxed);
    return *this;
  }

  T operator () () const {
    return _a.load(memory_order_relaxed);
  }

  T load () const {
    return _a.load(memory_order_relaxed);
  }

  void store ( T desired ) {
    _a.store(desired, memory_order_relaxed);
  }

  T operator++() {
    return _a.fetch_add(1, memory_order_relaxed) + 1;
  }
};
