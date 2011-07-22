#ifndef __tree_similarity_h
#define __tree_similarity_h
#include <vector>
#include <boost/dynamic_bitset_fwd.hpp>

namespace ivy_mike {
    namespace tree_parser_ms {
        struct lnode;
    }
} 
   
   
typedef std::vector<boost::dynamic_bitset<> > split_set_t;

bool split_sets_equal( const split_set_t &s1, const split_set_t &s2 );
double compare_trees( ivy_mike::tree_parser_ms::lnode *t1, ivy_mike::tree_parser_ms::lnode *t2, split_set_t &t2_splits );


#endif