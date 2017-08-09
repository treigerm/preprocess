#ifndef UTIL_SERIALIZE_HASH_TABLE_H
#define UTIL_SERIALIZE_HASH_TABLE_H

#include "util/file_piece.hh"

namespace util {

template <class Table>
void LoadTable(Table &table, const char *name) {
  util::scoped_fd f(util::OpenReadOrThrow(name));
  std::size_t allocated;
  util::ReadOrThrow(f.get(), &allocated, sizeof(std::size_t));
  std::size_t entries;
  util::ReadOrThrow(f.get(), &entries, sizeof(std::size_t));
  void* mem = util::MallocOrThrow(allocated);
  util::ReadOrThrow(f.get(), mem, allocated);
  table = Table(mem, entries, allocated);
}

template <class Table>
void SaveTable(Table &table, const char *name) {
  util::scoped_fd f(util::CreateOrThrow(name));
  std::size_t allocated = table.Allocated();
  util::WriteOrThrow(f.get(), &allocated, sizeof(size_t));
  std::size_t entries = table.Size();
  util::WriteOrThrow(f.get(), &entries, sizeof(size_t));
  util::WriteOrThrow(f.get(), table.Begin(), allocated);
}

} // namespace util

#endif // UTIL_SERIALIZE_HASH_TABLE_H
