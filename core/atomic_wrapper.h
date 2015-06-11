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
    :_a(a.load())
  {}

  atomic_wrapper(const atomic_wrapper &other)
    :_a(other._a.load())
  {}

  atomic_wrapper &operator=(const atomic_wrapper &other)
  {
    _a.store(other._a.load());
  }

  T operator () () const {
    return _a();
  }

  T load () const {
    return _a.load();
  }

  void store ( T desired ) {
    _a.store(desired);
  }

  T operator++() {
    return ++_a;
  }
};