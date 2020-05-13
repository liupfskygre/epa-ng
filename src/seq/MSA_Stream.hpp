#pragma once

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef __PREFETCH
#include <future>
#endif

#include "io/msa_reader_interface.hpp"
#include "seq/MSA.hpp"
#include "seq/MSA_Info.hpp"

#include "genesis/sequence/formats/fasta_input_iterator.hpp"

class MSA_Stream : public msa_reader {
  public:
  using container_type = MSA;
  using file_type      = genesis::sequence::FastaInputIterator;

  MSA_Stream( std::string const& msa_file,
              MSA_Info const& info,
              bool const premasking = true,
              bool const split      = false );
  MSA_Stream() = default;
  ~MSA_Stream();

  MSA_Stream( MSA_Stream const& other ) = delete;
  MSA_Stream( MSA_Stream&& other )      = default;

  MSA_Stream& operator=( MSA_Stream const& other ) = delete;
  MSA_Stream& operator=( MSA_Stream&& other ) = default;

  size_t read_next( container_type& result, size_t const number ) override;
  size_t num_sequences() const override { return info_.sequences(); }
  size_t local_seq_offset() const override { return local_seq_offset_; }

  private:
  void skip_to_sequence( size_t const );

  private:
  MSA_Info info_;
  file_type iter_;
  // container_type active_chunk_;
  container_type prefetch_chunk_;
#ifdef __PREFETCH
  std::future< void > prefetcher_;
#endif
  bool premasking_         = true;
  size_t num_read_         = 0;
  size_t max_read_         = std::numeric_limits< size_t >::max();
  size_t local_seq_offset_ = 0;
  bool first_              = true;
};
