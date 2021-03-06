#ifndef __raxml_interface_h
#define __raxml_interface_h


#include "ivymike/tree_parser.h"
#include <string>
#include <vector>
#include <map>
#include <stdint.h>
#include <boost/array.hpp>
#include <boost/numeric/ublas/fwd.hpp>
void optimize_branch_lengths( ivy_mike::tree_parser_ms::lnode *tree, const std::map<std::string, const std::vector<uint8_t> * const> &name_to_seq );
ivy_mike::tree_parser_ms::lnode *optimize_branch_lengths2( ivy_mike::tree_parser_ms::lnode *tree, const std::map<std::string, const std::vector<uint8_t> * const> &name_to_seq, ivy_mike::tree_parser_ms::ln_pool &pool );




//ivy_mike::tree_parser_ms::lnode *generate_marginal_ancestral_state_pvecs( ivy_mike::tree_parser_ms::ln_pool &pool, const std::string &tree_name, const std::string &ali_name, std::vector<boost::array<std::vector<double>, 4> > *pvecs );
ivy_mike::tree_parser_ms::lnode *generate_marginal_ancestral_state_pvecs( ivy_mike::tree_parser_ms::ln_pool &pool, const std::string &tree_name, const std::string &ali_name, std::vector<boost::numeric::ublas::matrix<double> > *pvecs );

#endif
