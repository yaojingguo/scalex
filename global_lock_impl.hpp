#pragma once

#include <cassert>
#include <memory>
#include <mutex>
#include <iterator>

#include "macros.hpp"

/**
 * Standard singly-linked list with a global lock for protection, and
 * standard reference counting
 *
 * References returned by this implementation are guaranteed to be valid
 * until the element is removed from the list
 */
template <typename T>
class global_lock_impl {
private:

  struct node;
  typedef std::unique_lock<std::mutex> unique_lock;
  typedef std::shared_ptr<unique_lock> unique_lock_ptr;
  typedef std::shared_ptr<node> node_ptr;

  struct node {
    // non-copyable
    node(const node &) = delete;
    node(node &&) = delete;
    node &operator=(const node &) = delete;

    node() : value_(), next_() {}
    node(const T &value, const node_ptr &next)
      : value_(value), next_(next) {}

    T value_;
    node_ptr next_;
  };

  mutable std::mutex mutex_;
  node_ptr head_;
  node_ptr tail_;

  struct iterator_ : public std::iterator<std::forward_iterator_tag, T> {
    iterator_() : lock_(), node_() {}
    iterator_(const unique_lock_ptr &lock, const node_ptr &node)
      : lock_(lock), node_(node) {}
    iterator_(unique_lock_ptr &&lock, const node_ptr &node)
      : lock_(std::move(lock)), node_(node) {}

    T &
    operator*() const
    {
      return node_->value_;
    }

    T *
    operator->() const
    {
      return &node_->value_;
    }

    bool
    operator==(const iterator_ &o) const
    {
      return node_ == o.node_;
    }

    bool
    operator!=(const iterator_ &o) const
    {
      return !operator==(o);
    }

    iterator_ &
    operator++()
    {
      node_ = node_->next_;
      return *this;
    }

    iterator_
    operator++(int)
    {
      iterator_ cur = *this;
      ++(*this);
      return cur;
    }

    unique_lock_ptr lock_;
    node_ptr node_;
  };

public:

  typedef iterator_ iterator;

  global_lock_impl() : mutex_(), head_(), tail_() {}

  size_t
  size() const
  {
    unique_lock l(mutex_);
    size_t ret = 0;
    node_ptr cur = head_;
    while (cur) {
      ret++;
      cur = cur->next_;
    }
    return ret;
  }

  inline T &
  front()
  {
    unique_lock l(mutex_);
    assert(head_);
    return head_->value_;
  }

  inline const T &
  front() const
  {
    return const_cast<global_lock_impl *>(this)->front();
  }

  inline T &
  back()
  {
    unique_lock l(mutex_);
    assert(tail_);
    assert(!tail_->next_);
    return tail_->value_;
  }

  inline const T &
  back() const
  {
    return const_cast<global_lock_impl *>(this)->back();
  }

  void
  pop_front()
  {
    unique_lock l(mutex_);
    assert(head_);
    node_ptr next = head_->next_;
    head_ = next;
    if (!head_)
      tail_.reset();
  }

  void
  push_back(const T &val)
  {
    unique_lock l(mutex_);
    node_ptr n(std::make_shared<node>(val, nullptr));
    if (!tail_) {
      assert(!head_);
      head_ = tail_ = n;
    } else {
      tail_->next_ = n;
      tail_ = n;
    }
  }

  inline void
  remove(const T &val)
  {
    unique_lock l(mutex_);
    node_ptr prev;
    node_ptr p = head_, *pp = &head_;
    while (p) {
      if (p->value_ == val) {
        // unlink
        *pp = p->next_;
        p = *pp;
        if (!*pp)
          // removed last value
          tail_ = prev;
      } else {
        prev = p;
        pp = &p->next_;
        p = p->next_;
      }
    }
  }

  std::pair<bool, T>
  try_pop_front()
  {
    unique_lock l(mutex_);
    if (unlikely(!head_)) {
      assert(!tail_);
      return std::make_pair(false, T());
    }
    T t = head_->value_;
    node_ptr next = head_->next_;
    head_ = next;
    if (!head_)
      tail_.reset();
    return std::make_pair(true, t);
  }

  iterator
  begin()
  {
    return iterator_(std::move(std::make_shared<unique_lock>(mutex_)), head_);
  }

  iterator
  end()
  {
    return iterator_(unique_lock_ptr(), node_ptr());
  }
};
