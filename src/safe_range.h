// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef NINJA_SAFE_RANGE_H_
#define NINJA_SAFE_RANGE_H_

#include <cstddef>
#include <iterator>
#include <type_traits>

template <typename Container>
class IndexRange {
 public:
  using size_type = typename Container::size_type;

  explicit IndexRange(Container& container)
      : container_(container), size_(container.size()) {}

  class iterator {
   public:
    using difference_type = std::ptrdiff_t;
    using value_type = typename Container::value_type;
    using reference =
        typename std::conditional<std::is_const<Container>::value,
                                  typename Container::const_reference,
                                  typename Container::reference>::type;
    using iterator_category = std::input_iterator_tag;

    iterator(Container& container, size_type index)
        : container_(container), index_(index) {}

    /// The element access is performed through operator[].
    reference operator*() const { return container_[index_]; }

    iterator& operator++() {
      ++index_;
      return *this;
    }

    bool operator!=(const iterator& other) const {
      return index_ != other.index_;
    }
    bool operator==(const iterator& other) const {
      return index_ == other.index_;
    }

   private:
    Container& container_;
    size_type index_;
  };

  iterator begin() const { return iterator(container_, 0); }
  iterator end() const { return iterator(container_, size_); }

 private:
  Container& container_;
  size_type size_;
};

// Index() wraps a container so that range-based for-loops iterate by
// index instead of by iterator. This avoids iterator invalidation when
// the container reallocates during the loop.

template <typename Container>
inline IndexRange<Container> Index(Container& container) {
  return IndexRange<Container>(container);
}

template <typename Container>
inline IndexRange<const Container> Index(const Container& container) {
  return IndexRange<const Container>(container);
}

#endif  // NINJA_SAFE_RANGE_H_
