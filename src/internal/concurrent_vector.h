#pragma once

#include <omp.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <numeric>
#include <vector>

#include "../reducer.h"
#include "hash/hash_map.h"

namespace blaze {
namespace internal {
template <class V>
class ConcurrentVector {
 public:
  ConcurrentVector();

  ~ConcurrentVector();

  void resize(const size_t n, const V& value = V());

  void set(
      const size_t key,
      const V& value,
      const std::function<void(V&, const V&)>& reducer = Reducer<V>::overwrite);

  void async_set(
      const size_t key,
      const V& value,
      const std::function<void(V&, const V&)>& reducer = Reducer<V>::overwrite);

  void sync(const std::function<void(V&, const V&)>& reducer = Reducer<V>::overwrite);

  ConcurrentVector<V>& operator-=(const ConcurrentVector<V>& rhs);

  void for_each_serial(const std::function<void(const size_t i, const V& value)>& handler) const;

  void for_each(const std::function<void(const size_t key, V& value)>& handler);

  std::vector<V> top_k(const size_t k, const std::function<bool(const V&, const V&)>& compare);

 private:
  size_t n;

  size_t n_segments;

  size_t n_segments_filter;

  size_t n_segments_shift;

  std::vector<std::vector<V>> segments;

  std::hash<size_t> hasher;

  std::vector<hash::HashMap<size_t, V, std::hash<size_t>>> thread_caches;

  std::vector<omp_lock_t> segment_locks;

  std::vector<V> get_vector_top_k(
      std::vector<V>& target,
      const size_t k,
      const std::function<bool(const V&, const V&)>& compare);

  std::vector<V> merge_vector_top_k(
      const std::vector<V>& a,
      const std::vector<V>& b,
      const size_t k,
      const std::function<bool(const V&, const V&)>& compare);
};

template <class V>
ConcurrentVector<V>::ConcurrentVector() {
  const size_t n_threads = omp_get_max_threads();
  n_segments = 4;
  while (n_segments < n_threads) n_segments <<= 1;
  n_segments <<= 2;
  n_segments_shift = __builtin_ctzll(n_segments);
  n_segments_filter = n_segments - 1;
  segments.resize(n_segments);
  segment_locks.resize(n_segments);
  for (auto& lock : segment_locks) omp_init_lock(&lock);
  thread_caches.resize(n_threads);
}

template <class V>
ConcurrentVector<V>::~ConcurrentVector() {
  for (auto& lock : segment_locks) omp_destroy_lock(&lock);
}

template <class V>
void ConcurrentVector<V>::resize(const size_t n, const V& value) {
  this->n = n;
  const size_t segment_size = (n >> n_segments_shift) + 1;
  for (size_t segment_id = 0; segment_id < n_segments; segment_id++) {
    segments[segment_id].resize(segment_size, value);
  }
}

template <class V>
void ConcurrentVector<V>::set(
    const size_t key, const V& value, const std::function<void(V&, const V&)>& reducer) {
  const size_t segment_id = key & n_segments_filter;
  const size_t elem_id = key >> n_segments_shift;
  auto& lock = segment_locks[segment_id];
  omp_set_lock(&lock);
  reducer(segments[segment_id][elem_id], value);
  omp_unset_lock(&lock);
}

template <class V>
void ConcurrentVector<V>::async_set(
    const size_t key, const V& value, const std::function<void(V&, const V&)>& reducer) {
  const size_t segment_id = key & n_segments_filter;
  const size_t elem_id = key >> n_segments_shift;
  auto& lock = segment_locks[segment_id];
  if (omp_test_lock(&lock)) {
    reducer(segments[segment_id][elem_id], value);
    omp_unset_lock(&lock);
  } else {
    const int thread_id = omp_get_thread_num();
    thread_caches[thread_id].set(key, hasher(key), value, reducer);
  }
}

template <class V>
void ConcurrentVector<V>::sync(const std::function<void(V&, const V&)>& reducer) {
#pragma omp parallel
  {
    const int thread_id = omp_get_thread_num();
    const auto& handler = [&](const size_t key, const size_t, const V& value) {
      const size_t segment_id = key & (n_segments - 1);
      const size_t elem_id = key >> n_segments_shift;
      auto& lock = segment_locks[segment_id];
      omp_set_lock(&lock);
      reducer(segments[segment_id][elem_id], value);
      omp_unset_lock(&lock);
    };
    thread_caches.at(thread_id).for_each(handler);
    thread_caches.at(thread_id).clear();
  }
}

template <class V>
void ConcurrentVector<V>::for_each(const std::function<void(const size_t key, V& value)>& handler) {
#pragma omp parallel for schedule(static, 1)
  for (size_t segment_id = 0; segment_id < n_segments; segment_id++) {
    auto& segment = segments[segment_id];
    size_t segment_size = segments[segment_id].size();
    for (size_t i = 0; i < segment_size; i++) {
      size_t key = (i << n_segments_shift) + segment_id;
      if (key >= n) continue;
      handler(key, segment[i]);
    }
  }
}

template <class V>
void ConcurrentVector<V>::for_each_serial(
    const std::function<void(const size_t key, const V& value)>& handler) const {
  size_t key = 0;
  size_t segment_id = 0;
  size_t elem_id = 0;
  while (key < n) {
    handler(key, segments[segment_id][elem_id]);
    key++;
    segment_id++;
    if (segment_id == n_segments) {
      segment_id = 0;
      elem_id++;
    }
  }
}

template <class V>
ConcurrentVector<V>& ConcurrentVector<V>::operator-=(const ConcurrentVector<V>& rhs) {
  if (n != rhs.n) throw std::runtime_error("Subtraction of two vector with different size");

#pragma omp parallel for schedule(static, 1)
  for (size_t segment_id = 0; segment_id < n_segments; segment_id++) {
    size_t segment_size = segments[segment_id].size();
    for (size_t i = 0; i < segment_size; i++) {
      segments[segment_id][i] -= rhs.segments[segment_id][i];
    }
  }

  return *this;
}

template <class V>
std::vector<V> ConcurrentVector<V>::top_k(
    const size_t k, const std::function<bool(const V&, const V&)>& compare) {
  std::vector<std::vector<V>> segment_top_ks(n_segments);

  const size_t segment_n_base = n >> n_segments_shift;
#pragma omp parallel for
  for (size_t segment_id = 0; segment_id < n_segments; segment_id++) {
    size_t segment_n = segment_n_base;
    if ((n & n_segments_filter) > segment_id) segment_n++;
    std::vector<V> segment_copy = segments[segment_id];
    segment_copy.resize(segment_n);
    segment_top_ks[segment_id] = get_vector_top_k(segment_copy, k, compare);
  }

  size_t step = 1;
  while (step < n_segments) {
    size_t i_end = n_segments - step;
    size_t i_step = step << 1;
#pragma omp parallel for schedule(static, 1)
    for (size_t i = 0; i < i_end; i += i_step) {
      segment_top_ks[i] =
          merge_vector_top_k(segment_top_ks[i], segment_top_ks[i + step], k, compare);
    }
    step <<= 1;
  }

  std::vector<V> result = segment_top_ks[0];
  return result;
}

template <class V>
std::vector<V> ConcurrentVector<V>::get_vector_top_k(
    std::vector<V>& target,
    const size_t k,
    const std::function<bool(const V&, const V&)>& compare) {
  if (target.size() <= k) {
    std::sort(target.begin(), target.end(), compare);
    return target;
  }

  std::nth_element(target.begin(), target.begin() + k, target.end(), compare);
  std::vector<V> result;

  V threshold = target[k - 1];
  for (size_t i = 0; i < target.size(); i++) {
    if (!compare(threshold, target[i])) {
      result.push_back(target[i]);
    }
  }

  std::sort(result.begin(), result.end(), compare);
  result.resize(k);
  return result;
}

template <class V>
std::vector<V> ConcurrentVector<V>::merge_vector_top_k(
    const std::vector<V>& a,
    const std::vector<V>& b,
    const size_t k,
    const std::function<bool(const V&, const V&)>& compare) {
  std::vector<V> result;
  size_t pos = 0;
  size_t na = a.size();
  size_t nb = b.size();
  size_t pos_a = 0;
  size_t pos_b = 0;
  while (pos < k && pos_a < na && pos_b < nb) {
    if (compare(a[pos_a], b[pos_b])) {
      result.push_back(a[pos_a]);
      pos_a++;
    } else {
      result.push_back(b[pos_b]);
      pos_b++;
    }
    pos++;
  }
  while (pos < k && pos_a < na) {
    result.push_back(a[pos_a]);
    pos_a++;
    pos++;
  }
  while (pos < k && pos_b < nb) {
    result.push_back(b[pos_b]);
    pos_b++;
    pos++;
  }
  return result;
}

}  // namespace internal
}  // namespace blaze
