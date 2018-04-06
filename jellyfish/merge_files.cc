/*  This file is part of Jellyfish.

    Jellyfish is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Jellyfish is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Jellyfish.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <jellyfish/merge_files.hpp>

#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <string>

#include <jellyfish/err.hpp>
#include <jellyfish/misc.hpp>
#include <jellyfish/mer_heap.hpp>
#include <jellyfish/jellyfish.hpp>
#include <jellyfish/rectangular_binary_matrix.hpp>
#include <jellyfish/cpp_array.hpp>

namespace err = jellyfish::err;

using jellyfish::file_header;
using jellyfish::RectangularBinaryMatrix;
using jellyfish::mer_dna;
using jellyfish::cpp_array;
typedef std::auto_ptr<binary_reader> binary_reader_ptr;
typedef std::auto_ptr<text_reader> text_reader_ptr;


struct file_info {
  std::ifstream is;
  file_header   header;

  file_info(const char* path) :
  is(path),
  header(is)
  { }
};
typedef std::auto_ptr<RectangularBinaryMatrix> matrix_ptr;

template<typename reader_type, typename writer_type>
void do_merge(cpp_array<file_info>& files, std::ostream& out, writer_type& writer,
              uint64_t min, uint64_t max, std::vector<const char*> input_files, int cutoff, const char * proband) {
  cpp_array<reader_type> readers(files.size());
  typedef jellyfish::mer_heap::heap<mer_dna, reader_type> heap_type;
  typedef typename heap_type::const_item_t heap_item;
  heap_type heap(files.size());

  for(size_t i = 0; i < files.size(); ++i) {
    readers.init(i, files[i].is, &files[i].header);
    //std::cout << "input file is: " << input_files[i] << "\n";
    if(readers[i].next()){
      readers[i].file_ = input_files[i];
      //std::cout << "printing readers[i].file_: " << readers[i].file_ << "\n";
      //std::cout << "files[i].is: " << files[i].is << "  &files[i].header is: " << files[i].header << "File origin is: " << readers[i].file_ << "\n";
      heap.push(readers[i]);
    }
  }

  heap_item head = heap.head();
  mer_dna   key;
  std::string file;
  while(heap.is_not_empty()) {
    file = head->file_;
    key = head->key_;
    std::vector<uint64_t> sum;
    do {
      sum.push_back(head->val_);
      heap.pop();
      if(head->it_->next())
        heap.push(*head->it_);
      head = heap.head();
    } while(head->key_ == key && heap.is_not_empty());
        
    if (sum.size() ==1) {
	if (sum[0] >= cutoff and sum[0] < 300 and file.compare(proband)==0) {
	    std::cout << key <<'\t' << sum[0] << std::endl;
	  }
      }
  }
}

// Merge files. Throws an error if unsuccessful.
void merge_files(std::vector<const char*> input_files,
                 const char* out_file,
                 file_header& out_header,
                 uint64_t min, uint64_t max, int cutoff, const char * proband) {
  unsigned int key_len            = 0;
  size_t       max_reprobe_offset = 0;
  size_t       size               = 0;
  unsigned int out_counter_len    = std::numeric_limits<unsigned int>::max();
  std::string  format;
  matrix_ptr   matrix;
  std::string file;

  cpp_array<file_info> files(input_files.size());

  // create an iterator for each hash file
  for(size_t i = 0; i < files.size(); i++) {
    files.init(i, input_files[i]);
    //std::cout << "input file is: " << input_files[i] << "\n";
    if(!files[i].is.good())
      //throw MergeError(err::msg() << "Failed to open input file '" << input_files[i] << "'");
      continue;

    file_header& h = files[i].header;
    h.file_ = input_files[i];
    if(i == 0) {
      key_len            = h.key_len();
      max_reprobe_offset = h.max_reprobe_offset();
      size               = h.size();
      matrix.reset(new RectangularBinaryMatrix(h.matrix()));
      out_header.size(size);
      out_header.key_len(key_len);
      format = h.format();
      out_header.matrix(*matrix);
      out_header.max_reprobe(h.max_reprobe());
      size_t reprobes[h.max_reprobe() + 1];
      h.get_reprobes(reprobes);
      out_header.set_reprobes(reprobes);
      out_counter_len = std::min(out_counter_len, h.counter_len());
    } else {
      if(format != h.format())
        throw MergeError(err::msg() << "Can't merge files with different formats (" << format << ", " << h.format() << ")");
      if(h.key_len() != key_len)
        throw MergeError(err::msg() << "Can't merge hashes of different key lengths (" << key_len << ", " << h.key_len() << ")");
      if(h.max_reprobe_offset() != max_reprobe_offset)
        throw MergeError("Can't merge hashes with different reprobing strategies");
      if(h.size() != size)
        throw MergeError(err::msg() << "Can't merge hash with different size (" << size << ", " << h.size() << ")");
      if(h.matrix() != *matrix)
        throw MergeError("Can't merge hash with different hash function");
    }
  }
  mer_dna::k(key_len / 2);

  std::ofstream out(out_file);
  if(!out.good())
    throw MergeError(err::msg() << "Can't open out file '" << out_file << "'");
  out_header.format(format);

  if(!format.compare(binary_dumper::format)) {
    out_header.counter_len(out_counter_len);
    out_header.write(out);
    binary_writer writer(out_counter_len, key_len);
    do_merge<binary_reader, binary_writer>(files, out, writer, min, max, input_files, cutoff, proband);
  } else if(!format.compare(text_dumper::format)) {
    out_header.write(out);
    text_writer writer;
    do_merge<text_reader, text_writer>(files, out, writer, min, max, input_files, cutoff, proband);
  } else {
    throw MergeError(err::msg() << "Unknown format '" << format << "'");
  }
  out.close();
}
