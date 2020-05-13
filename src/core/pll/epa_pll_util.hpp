#pragma once

#include <string>
#include <vector>

#include "core/pll/pllhead.hpp"
#include "core/raxml/Model.hpp"
#include "seq/MSA.hpp"
#include "seq/MSA_Stream.hpp"
#include "seq/Sequence.hpp"
#include "tree/Tree.hpp"
#include "tree/Tree_Numbers.hpp"

void link_tree_msa( pll_utree_t* tree,
                    pll_partition_t* partition,
                    raxml::Model& model,
                    MSA const& msa,
                    unsigned int const num_tip_nodes );
void precompute_clvs( pll_utree_t const* const tree,
                      pll_partition_t* partition,
                      Tree_Numbers const& nums );
void split_combined_msa( MSA& source,
                         MSA& target,
                         Tree& tree );
raxml::Model get_model( pll_partition_t* partition );

// operator overloads
bool operator==( pll_unode_t const* node, Sequence const& s );
bool operator==( Sequence const& s, pll_unode_t const* node );
