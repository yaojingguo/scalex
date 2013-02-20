#pragma once

#include <cassert>
#include <memory>

#include "atomic_reference.hpp"
#include "macros.hpp"

namespace private_ {
struct nop_scoper {
  template <typename T>
  inline void release(T *) const {}
};
}

/**
 * Lock-free singly-linked list implemention, with configurable ref counting
 * and garbage collection policies
 *
 * References returned by this implementation are guaranteed to be valid until
 * the element is removed from the list
 */
template <typename T,
          typename RefCountImpl = atomic_ref_counted,
          typename ScopedImpl = private_::nop_scoper>
class lock_free_impl {
private:

  struct node;
  typedef atomic_ref_ptr<node> node_ptr;

  struct node : public RefCountImpl {
    // non-copyable
    node(const node &) = delete;
    node(node &&) = delete;
    node &operator=(const node &) = delete;

    node() : value_(), next_() {}
    node(const T &value, const node_ptr &next)
      : value_(value), next_(next) {}

    ~node()
    {
      // sanity check
      assert(next_.get_mark());
    }

    T value_;
    node_ptr next_;

    inline bool
    is_marked() const
    {
      return next_.get_mark();
    }
  };

  node_ptr head_; // head_ points to a sentinel beginning node

  struct iterator_ {
    iterator_() : node_(), scoper_() {}
    iterator_(const node_ptr &node)
      : node_(node), scoper_() {}

    T &
    operator*() const
    {
      // could return deleted value
      return node_->value_;
    }

    T *
    operator->() const
    {
      // could return deleted value
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
      do {
        node_ = node_->next_;
      } while (node_ && node_->is_marked());
      return *this;
    }

    iterator_
    operator++(int)
    {
      iterator_ cur = *this;
      ++(*this);
      return cur;
    }

    node_ptr node_;
    ScopedImpl scoper_;
  };

public:

  typedef iterator_ iterator;

  lock_free_impl() : head_(new node) {}

  size_t
  size() const
  {
    ScopedImpl scoper UNUSED;
    assert(!head_->is_marked());
    size_t ret = 0;
    node_ptr cur = head_->next_;
    while (cur) {
      if (!cur->is_marked());
        ret++;
      cur = cur->next_;
    }
    return ret;
  }

  inline T &
  front()
  {
  retry:
    ScopedImpl scoper UNUSED;
    assert(!head_->is_marked());
    node_ptr p = head_->next_;
    assert(p);
    if (p->is_marked())
      goto retry;
    T &ref = p->value_;
    if (p->is_marked())
      goto retry;
    // we have stability on a reference
    return ref;
  }

  inline const T &
  front() const
  {
  retry:
    ScopedImpl scoper UNUSED;
    assert(!head_->is_marked());
    node_ptr p = head_->next_;
    assert(p);
    if (p->is_marked())
      goto retry;
    T &ref = p->value_;
    if (p->is_marked())
      goto retry;
    // we have stability on a reference
    return ref;
  }

  void
  pop_front()
  {
  retry:
    ScopedImpl scoper;
    assert(!head_->is_marked());
    node_ptr &prev = head_;
    node_ptr cur = prev->next_;
    assert(cur);

    if (!cur->next_.mark())
      // was concurrently deleted
      goto retry;

    // we don't need to CAS the prev ptr here, because we know that the
    // sentinel node will never be deleted (that is, the first node of a list
    // will ALWAYS be the first node until it is deleted)
    prev->next_ = cur->next_; // semantics of assign() do not copy marked bits
    scoper.release(cur.get());
  }

  void
  push_back(const T &val)
  {
  retry:
    ScopedImpl scoper UNUSED;
    assert(!head_->is_marked());
    node_ptr p = head_->next_, *pp = &head_->next_;
    for (; p; pp = &p->next_, p = p->next_)
      ;
    node_ptr n(new node(val, node_ptr()));
    assert(!p.get_mark()); // b/c node ptrs don't propagate mark bits
    if (!pp->compare_exchange_strong(p, n))
      goto retry;
  }

  inline void
  remove(const T &val)
  {
    ScopedImpl scoper;
    node_ptr p = head_->next_, *pp = &head_->next_;
    while (p) {
      if (p->value_ == val) {
        // mark removed
        if (p->next_.mark()) {
          // try to unlink- ignore success value
          if (pp->compare_exchange_strong(p, p->next_)) {
            // successful unlink, report
            scoper.release(p.get());
          }
        }
        // in any case, advance the current ptr, but keep the
        // prev ptr the same
        p = p->next_;
      } else {
        pp = &p->next_;
        p = p->next_;
      }
    }
  }

  iterator
  begin()
  {
    ScopedImpl scoper UNUSED;
    return iterator_(head_->next_);
  }

  iterator
  end()
  {
    return iterator_(node_ptr());
  }
};

