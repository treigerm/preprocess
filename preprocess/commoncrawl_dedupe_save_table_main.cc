// Tool to convert raw CommonCrawl files into deduplicated files.
// Includes option to save the hash table which identifies duplicate lines to disk.
// Strips leading and trailing spaces.
// Removes document delimiter lines (those that begin with df6fa1abb58549287111ba8d776733e9).
// Removes duplicate lines.
// Removes any line that contains invalid UTF-8.
//
#include "util/fake_ofstream.hh"
#include "util/file_piece.hh"
#include "util/murmur_hash.hh"
#include "util/probing_hash_table.hh"
#include "util/serialize_hash_table.hh"
#include "util/scoped.hh"
#include "util/utf8.hh"

#include <iostream>

#include <stdint.h>
#include <string.h>

namespace {

// Hash table with 64-bit keys.
struct Entry {
  typedef uint64_t Key;
  uint64_t key;
  uint64_t GetKey() const { return key; }
  void SetKey(uint64_t to) { key = to; }
};

typedef util::AutoProbing<Entry, util::IdentityHash> Table;

// Use 64-bit MurmurHash in the hash table.
bool IsNewLine(Table &table, StringPiece l) {
  Table::MutableIterator it;
  Entry entry;
  entry.key = util::MurmurHashNative(l.data(), l.size(), 1);
  return !table.FindOrInsert(entry, it);
}

// Remove leading and trailing space characters.
StringPiece StripSpaces(StringPiece ret) {
  while (ret.size() && util::kSpaces[static_cast<unsigned char>(*ret.data())]) {
    ret = StringPiece(ret.data() + 1, ret.size() - 1);
  }
  while (ret.size() && util::kSpaces[static_cast<unsigned char>(ret.data()[ret.size() - 1])]) {
    ret = StringPiece(ret.data(), ret.size() - 1);
  }
  return ret;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc > 4 || argc < 4) {
    std::cerr << "Usage: " << argv[0] << " file_to_remove src_hash_table out_hash_table" << std::endl
              << "file_to_remove\teach line in this file won't be added to the output" << std::endl
              << "src_hash_table\thash table of a previous run of this program saved to disk" << std::endl
              << "out_hash_table\tfile name for writing the hash table to disk" << std::endl
              << "If you do not want to provide any of the arguments substitute \"/dev/null\" at their place" << std::endl;
    return 1;
  }
  try {
    Table table;
    StringPiece l;

    // Read hash table if a file is given.
    if (!strcmp("/dev/null", argv[2])) {
      util::LoadTable(table, argv[2]);
    }

    // If there's a file to remove lines from, add it to the hash table of lines.
    util::FilePiece removing(argv[1]);
    while (removing.ReadLineOrEOF(l)) {
      IsNewLine(table, StripSpaces(l));
    }

    // This is the beginning of a line that delimits documents in the raw files.
    const StringPiece remove_line("df6fa1abb58549287111ba8d776733e9");
    util::FakeOFStream out(1);
    util::FilePiece in(0, "stdin", &std::cerr);
    while (in.ReadLineOrEOF(l)) {
      l = StripSpaces(l);
      // A line passes if:
      // It does not begin with the magic document delimiter.
      // Its 64-bit hash has not been seen before.
      // and it is valid UTF-8.
      if (!starts_with(l, remove_line) && IsNewLine(table, l) && utf8::IsUTF8(l)) {
        out << l << '\n';
      }
    }

    // Save hash table to disk.
    util::SaveTable(table, argv[3]);
  }
  catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
