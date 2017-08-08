// Tool to convert raw CommonCrawl files into format that is ready to be passed to the deduper.
// Strips leading and trailing spaces.
// Removes document delimiter lines (those that begin with df6fa1abb58549287111ba8d776733e9).
// Removes any line that contains invalid UTF-8.
//
#include "util/fake_ofstream.hh"
#include "util/file_piece.hh"
#include "util/scoped.hh"
#include "util/utf8.hh"

#include <iostream>

#include <stdint.h>

namespace {

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
  if (argc > 1) {
    std::cerr << "Usage: " << argv[0] << "\nTakes input on stdin."
      " Removes lines which start with the magic document delimiter and which have invalid UTF-8." << std::endl;
    return 1;
  }
  try {
    StringPiece l;

    // This is the beginning of a line that delimits documents in the raw files.
    const StringPiece remove_line("df6fa1abb58549287111ba8d776733e9");
    util::FakeOFStream out(1);
    util::FilePiece in(0, "stdin", &std::cerr);
    while (in.ReadLineOrEOF(l)) {
      l = StripSpaces(l);
      // A line passes if:
      // It does not begin with the magic document delimiter.
      // and it is valid UTF-8.
      if (!starts_with(l, remove_line) && utf8::IsUTF8(l)) {
        out << l << '\n';
      }
    }
  }
  catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
