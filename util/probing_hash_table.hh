#ifndef UTIL_PROBING_HASH_TABLE_H
#define UTIL_PROBING_HASH_TABLE_H

#include "util/exception.hh"
#include "util/scoped.hh"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <vector>

#include <assert.h>
#include <stdint.h>

namespace util {

/* Thrown when table grows too large */
class ProbingSizeException : public Exception {
  public:
    ProbingSizeException() throw() {}
    ~ProbingSizeException() throw() {}
};

// std::identity is an SGI extension :-(
struct IdentityHash {
  template <class T> T operator()(T arg) const { return arg; }
};

template <class EntryT, class HashT, class EqualT> class AutoProbing;

/* Non-standard hash table
 * Buckets must be set at the beginning and must be greater than maximum number
 * of elements, else it throws ProbingSizeException.
 * Memory management and initialization is externalized to make it easier to
 * serialize these to disk and load them quickly.
 * Uses linear probing to find value.
 * Only insert and lookup operations.
 */
template <class EntryT, class HashT, class EqualT = std::equal_to<typename EntryT::Key> > class ProbingHashTable {
  public:
    typedef EntryT Entry;
    typedef typename Entry::Key Key;
    typedef const Entry *ConstIterator;
    typedef Entry *MutableIterator;
    typedef HashT Hash;
    typedef EqualT Equal;

    static uint64_t Size(uint64_t entries, float multiplier) {
      uint64_t buckets = std::max(entries + 1, static_cast<uint64_t>(multiplier * static_cast<float>(entries)));
      return buckets * sizeof(Entry);
    }

    // Must be assigned to later.
    ProbingHashTable() : entries_(0)
#ifdef DEBUG
      , initialized_(false)
#endif
    {}

    ProbingHashTable(void *start, std::size_t allocated, const Key &invalid = Key(), const Hash &hash_func = Hash(), const Equal &equal_func = Equal())
      : begin_(reinterpret_cast<MutableIterator>(start)),
        buckets_(allocated / sizeof(Entry)),
        end_(begin_ + buckets_),
        invalid_(invalid),
        hash_(hash_func),
        equal_(equal_func),
        entries_(0)
#ifdef DEBUG
        , initialized_(true)
#endif
    {}

    ProbingHashTable(void *start, std::size_t allocated, std::size_t entries = 0, const Key &invalid = Key(), const Hash &hash_func = Hash(), const Equal &equal_func = Equal())
      : begin_(reinterpret_cast<MutableIterator>(start)),
        buckets_(allocated / sizeof(Entry)),
        end_(begin_ + buckets_),
        invalid_(invalid),
        hash_(hash_func),
        equal_(equal_func),
        entries_(entries)
#ifdef DEBUG
      , initialized_(true)
#endif
    {}

    void Relocate(void *new_base) {
      begin_ = reinterpret_cast<MutableIterator>(new_base);
      end_ = begin_ + buckets_;
    }

    template <class T> MutableIterator Insert(const T &t) {
#ifdef DEBUG
      assert(initialized_);
#endif
      UTIL_THROW_IF(++entries_ >= buckets_, ProbingSizeException, "Hash table with " << buckets_ << " buckets is full.");
      return UncheckedInsert(t);
    }

    // Return true if the value was found (and not inserted).  This is consistent with Find but the opposite if hash_map!
    template <class T> bool FindOrInsert(const T &t, MutableIterator &out) {
#ifdef DEBUG
      assert(initialized_);
#endif
      for (MutableIterator i = Ideal(t);;) {
        Key got(i->GetKey());
        if (equal_(got, t.GetKey())) { out = i; return true; }
        if (equal_(got, invalid_)) {
          UTIL_THROW_IF(++entries_ >= buckets_, ProbingSizeException, "Hash table with " << buckets_ << " buckets is full.");
          *i = t;
          out = i;
          return false;
        }
        if (++i == end_) i = begin_;
      }   
    }

    void FinishedInserting() {}

    // Don't change anything related to GetKey,  
    template <class Key> bool UnsafeMutableFind(const Key key, MutableIterator &out) {
#ifdef DEBUG
      assert(initialized_);
#endif
      for (MutableIterator i(begin_ + (hash_(key) % buckets_));;) {
        Key got(i->GetKey());
        if (equal_(got, key)) { out = i; return true; }
        if (equal_(got, invalid_)) return false;
        if (++i == end_) i = begin_;
      }
    }

    // Like UnsafeMutableFind, but the key must be there.
    template <class Key> MutableIterator UnsafeMutableMustFind(const Key key) {
       for (MutableIterator i(begin_ + (hash_(key) % buckets_));;) {
        Key got(i->GetKey());
        if (equal_(got, key)) { return i; }
        assert(!equal_(got, invalid_));
        if (++i == end_) i = begin_;
      }
    }


    template <class Key> bool Find(const Key key, ConstIterator &out) const {
#ifdef DEBUG
      assert(initialized_);
#endif
      for (ConstIterator i(begin_ + (hash_(key) % buckets_));;) {
        Key got(i->GetKey());
        if (equal_(got, key)) { out = i; return true; }
        if (equal_(got, invalid_)) return false;
        if (++i == end_) i = begin_;
      }
    }

    // Like Find but we're sure it must be there.
    template <class Key> ConstIterator MustFind(const Key key) const {
      for (ConstIterator i(begin_ + (hash_(key) % buckets_));;) {
        Key got(i->GetKey());
        if (equal_(got, key)) { return i; }
        assert(!equal_(got, invalid_));
        if (++i == end_) i = begin_;
      }
    }

    void Clear() {
      Entry invalid;
      invalid.SetKey(invalid_);
      std::fill(begin_, end_, invalid);
      entries_ = 0;
    }

    MutableIterator Begin() {
      return begin_;
    }

    // Return number of entries assuming no serialization went on.
    std::size_t SizeNoSerialization() const {
      return entries_;
    }

    // Return memory size expected by Double.
    std::size_t DoubleTo() const {
      return buckets_ * 2 * sizeof(Entry);
    }

    // Inform the table that it has double the amount of memory.
    // Pass clear_new = false if you are sure the new memory is initialized
    // properly (to invalid_) i.e. by mremap.
    void Double(void *new_base, bool clear_new = true) {
      begin_ = static_cast<MutableIterator>(new_base);
      MutableIterator old_end = begin_ + buckets_;
      buckets_ *= 2;
      end_ = begin_ + buckets_;
      if (clear_new) {
        Entry invalid;
        invalid.SetKey(invalid_);
        std::fill(old_end, end_, invalid);
      }
      std::vector<Entry> rolled_over;
      // Move roll-over entries to a buffer because they might not roll over anymore.  This should be small.
      for (MutableIterator i = begin_; i != old_end && !equal_(i->GetKey(), invalid_); ++i) {
        rolled_over.push_back(*i);
        i->SetKey(invalid_);
      }
      /* Re-insert everything.  Entries might go backwards to take over a
       * recently opened gap, stay, move to new territory, or wrap around.   If
       * an entry wraps around, it might go to a pointer greater than i (which
       * can happen at the beginning) and it will be revisited to possibly fill
       * in a gap created later.
       */
      Entry temp;
      for (MutableIterator i = begin_; i != old_end; ++i) {
        if (!equal_(i->GetKey(), invalid_)) {
          temp = *i;
          i->SetKey(invalid_);
          UncheckedInsert(temp);
        }
      }
      // Put the roll-over entries back in.
      for (typename std::vector<Entry>::const_iterator i(rolled_over.begin()); i != rolled_over.end(); ++i) {
        UncheckedInsert(*i);
      }
    }

    // Mostly for tests, check consistency of every entry.
    void CheckConsistency() {
      MutableIterator last;
      for (last = end_ - 1; last >= begin_ && !equal_(last->GetKey(), invalid_); --last) {}
      UTIL_THROW_IF(last == begin_, ProbingSizeException, "Completely full");
      MutableIterator i;
      // Beginning can be wrap-arounds.
      for (i = begin_; !equal_(i->GetKey(), invalid_); ++i) {
        MutableIterator ideal = Ideal(*i);
        UTIL_THROW_IF(ideal > i && ideal <= last, Exception, "Inconsistency at position " << (i - begin_) << " should be at " << (ideal - begin_));
      }
      MutableIterator pre_gap = i;
      for (; i != end_; ++i) {
        if (equal_(i->GetKey(), invalid_)) {
          pre_gap = i;
          continue;
        }
        MutableIterator ideal = Ideal(*i);
        UTIL_THROW_IF(ideal > i || ideal <= pre_gap, Exception, "Inconsistency at position " << (i - begin_) << " with ideal " << (ideal - begin_));
      }
    }

  private:
    friend class AutoProbing<Entry, Hash, Equal>;

    template <class T> MutableIterator Ideal(const T &t) {
      return begin_ + (hash_(t.GetKey()) % buckets_);
    }

    template <class T> MutableIterator UncheckedInsert(const T &t) {
      for (MutableIterator i(Ideal(t));;) {
        if (equal_(i->GetKey(), invalid_)) { *i = t; return i; }
        if (++i == end_) { i = begin_; }
      }
    }

    MutableIterator begin_;
    std::size_t buckets_;
    MutableIterator end_;
    Key invalid_;
    Hash hash_;
    Equal equal_;
    std::size_t entries_;
#ifdef DEBUG
    bool initialized_;
#endif
};

// Resizable linear probing hash table.  This owns the memory.
template <class EntryT, class HashT, class EqualT = std::equal_to<typename EntryT::Key> > class AutoProbing {
  private:
    typedef ProbingHashTable<EntryT, HashT, EqualT> Backend;
  public:
    typedef EntryT Entry;
    typedef typename Entry::Key Key;
    typedef const Entry *ConstIterator;
    typedef Entry *MutableIterator;
    typedef HashT Hash;
    typedef EqualT Equal;

    AutoProbing(std::size_t initial_size = 10, const Key &invalid = Key(), const Hash &hash_func = Hash(), const Equal &equal_func = Equal()) :
      allocated_(Backend::Size(initial_size, 1.5)), mem_(util::MallocOrThrow(allocated_)), backend_(mem_.get(), allocated_, invalid, hash_func, equal_func) {
      threshold_ = initial_size * 1.2;
      Clear();
    }

    AutoProbing(void* mem, std::size_t entries, std::size_t allocated) :
      allocated_(allocated),
      mem_(mem),
      backend_(mem_.get(), allocated_, entries, Key(), Hash(), Equal()) {
      threshold_ = static_cast<size_t>( 0.8 * static_cast<float>(allocated) / static_cast<float>(sizeof(Entry)) );
    }

    AutoProbing& operator=(AutoProbing &&other) {
      mem_.reset(other.mem_.release());
      allocated_ = std::move(other.allocated_);
      threshold_ = std::move(other.threshold_);
      backend_ = std::move(other.backend_);
      return *this;
    }

    // Assumes that the key is unique.  Multiple insertions won't cause a failure, just inconsistent lookup.
    template <class T> MutableIterator Insert(const T &t) {
      DoubleIfNeeded();
      return backend_.UncheckedInsert(t);
    }

    template <class T> bool FindOrInsert(const T &t, MutableIterator &out) {
      DoubleIfNeeded();
      return backend_.FindOrInsert(t, out);
    }

    template <class Key> bool UnsafeMutableFind(const Key key, MutableIterator &out) {
      return backend_.UnsafeMutableFind(key, out);
    }

    template <class Key> MutableIterator UnsafeMutableMustFind(const Key key) {
      return backend_.UnsafeMutableMustFind(key);
    }

    template <class Key> bool Find(const Key key, ConstIterator &out) const {
      return backend_.Find(key, out);
    }

    template <class Key> ConstIterator MustFind(const Key key) const {
      return backend_.MustFind(key);
    }

    std::size_t Size() const {
      return backend_.SizeNoSerialization();
    }

    std::size_t Allocated() const {
      return allocated_;
    }

    MutableIterator Begin() {
      return backend_.Begin();
    }

    void Clear() {
      backend_.Clear();
    }

  private:
    void DoubleIfNeeded() {
      if (Size() < threshold_)
        return;
      mem_.call_realloc(backend_.DoubleTo());
      allocated_ = backend_.DoubleTo();
      backend_.Double(mem_.get());
      threshold_ *= 2;
    }

    std::size_t allocated_;
    util::scoped_malloc mem_;
    Backend backend_;
    std::size_t threshold_;
};

} // namespace util

#endif // UTIL_PROBING_HASH_TABLE_H
