#ifndef SORTED_SET_H
#define SORTED_SET_H

#include <algorithm>
#include <cassert>
#include <vector>

/// Modifying a key in associative containers leads to UB. This is an std::set
/// reimplementation with worse characteristics (such as re-sorting the
/// container on every push), but one that allows for elements to be
/// out-of-order (which can happen after a key was modified)
template<class Elem, class Compare>
struct SortedSet {
  SortedSet(): comp(Compare()) {}
  size_t size() const {
    return data.size();
  }

  bool has_element(const Elem& elem) const {
    return std::find(data.begin(), data.end(), elem) != data.end();
  }

  void insert(const Elem& elem) {
    if (has_element(elem))
      return;
    data.push_back(elem);
    sort(data.begin(), data.end(), comp);
  }

  bool empty() const {
    return data.empty();
  }

  void clear() {
    data.clear();
  }

  struct iterator {
    typedef typename std::vector<Elem>::iterator internal_iter;
    iterator(SortedSet<Elem, Compare>& parent, internal_iter pointed_by)
      : parent_(parent), pointed_by_(pointed_by) {}

    iterator& operator++() {
      ++pointed_by_;
      return *this;
    }

    iterator& operator--() {
      --pointed_by_;
      return *this;
    }

    Elem& operator*() {
      return *pointed_by_;
    }

    bool operator==(const iterator& iter) const {
      return pointed_by_ == iter.pointed_by_;
    }

    bool operator!=(const iterator& iter) const {
      return pointed_by_ != iter.pointed_by_;
    }

    Elem drop() {
      Elem ret = *pointed_by_;
      parent_.data.erase(pointed_by_);
      return ret;
    }

  private:
    SortedSet<Elem, Compare>& parent_;
    internal_iter pointed_by_;
  };

  iterator begin() {
    assert(!empty());
    return iterator(*this, data.begin());
  }

  iterator end() {
    return iterator(*this, data.end());
  }

  Elem operator[](size_t i) {
    return data[i];
  }

private:
  std::vector<Elem> data;
  const Compare comp;
};

#endif // SORTED_SET_H
