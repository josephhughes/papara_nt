#ifndef __tree_utils_h
#define __tree_utils_h

#include <algorithm>
#include <stdexcept>
#include <vector>
#include <set>
#include <cassert>
#include <stdint.h>
#include <boost/tr1/unordered_set.hpp>
#include "ivymike/smart_ptr.h"
#include "ivymike/tree_parser.h"

enum tip_case {
    TIP_TIP,
    TIP_INNER,
    INNER_INNER
    
};



template<class lnode>
struct rooted_bifurcation {
    
    
    lnode * parent;
    lnode * child1;
    lnode * child2;
    
        
    tip_case tc;
    
    rooted_bifurcation() : parent(0), child1(0), child2(0) {}
    
    rooted_bifurcation( lnode *p, lnode *c1, lnode *c2, tip_case t ) 
        : parent(p),
        child1(c1),
        child2(c2),
        tc(t)
    {}
};


template<class lnode>
inline std::ostream &operator<<( std::ostream &os, const rooted_bifurcation<lnode> &rb ) {
    const char *tc;
    
    switch( rb.tc ) {
    case TIP_TIP:
        tc = "TIP_TIP";
        break;
        
    case TIP_INNER:
        tc = "TIP_INNER";
        break;
        
        case INNER_INNER:
        tc = "INNER_INNER";
        break;
    }
    
    os << tc << " " << *(rb.parent->m_data) << " " << *(rb.child1->m_data) << " " << *(rb.child2->m_data);
    return os;
}

template <class lnode, class container>
void rooted_traveral_order_rec( lnode *n, container &cont, bool incremental = false ) {
    
    
    
    lnode *n1 = n->next->back;
    lnode *n2 = n->next->next->back;
    
    assert( n->m_data->isTip || n1 != 0 );
    assert( n->m_data->isTip || n2 != 0 );
    
    n->towards_root = true;
    n->next->towards_root = false;
    n->next->next->towards_root = false;
    
    
    
    if( n1->m_data->isTip && n2->m_data->isTip ) {
        cont.push_front( rooted_bifurcation<lnode>( n, n1, n2, TIP_TIP ));
    } else if( n1->m_data->isTip && !n2->m_data->isTip ) {
        cont.push_front( rooted_bifurcation<lnode>( n, n1, n2, TIP_INNER ));
        
        if( !incremental || !n2->towards_root ) {
            rooted_traveral_order_rec( n2, cont );
        }
    } else if( !n1->m_data->isTip && n2->m_data->isTip ) {
        cont.push_front( rooted_bifurcation<lnode>( n, n2, n1, TIP_INNER ));
        
        if( !incremental || !n1->towards_root ) {
            rooted_traveral_order_rec( n1, cont );    
        }
        
    } else {
        cont.push_front( rooted_bifurcation<lnode>( n, n1, n2, INNER_INNER ));
        
        if( !incremental || !n1->towards_root ) {
            rooted_traveral_order_rec( n1, cont );
        }
        if( !incremental || !n2->towards_root ) {
            rooted_traveral_order_rec( n2, cont );
        }
    }
}

template <class lnode, class container>
void rooted_traveral_order( lnode *n1, lnode *n2, container &cont, bool incremental ) {
    
    if( !n1->m_data->isTip ) {
        rooted_traveral_order_rec<lnode, container>( n1, cont, incremental );
    }
    if( !n2->m_data->isTip ) {
        rooted_traveral_order_rec<lnode, container>( n2, cont, incremental );
    }
    
    
    //std::reverse( cont.begin(), cont.end());
}

template <class lnode>
lnode *towards_tree( lnode *n ) {
    int ct = 0;
    
    while( n->back == 0 ) {
        n = n->next;
        
        if( ct > 3 ) {
        
            throw std::runtime_error( "node not connected to tree" );
        }
        
        ct++;
    }
    
    return n;
    
}

template <class visitor>
void visit_lnode( typename visitor::lnode *n, visitor &v, bool go_back = true ) {
    v( n );
    
    if( go_back && n->back != 0 ) {
        visit_lnode( n->back, v, false );
    }
    if( n->next->back != 0 ) {
        visit_lnode( n->next->back, v, false );   
    }

    if( n->next->next->back != 0 ) {
        visit_lnode( n->next->next->back, v, false );
    }
};

template <class visitor>
void visit_lnode_postorder( typename visitor::lnode *n, visitor &v, bool go_back = true ) {

    if( go_back && n->back != 0 ) {
        visit_lnode( n->back, v, false );
    }
    if( n->next->back != 0 ) {
        visit_lnode( n->next->back, v, false );   
    }

    if( n->next->next->back != 0 ) {
        visit_lnode( n->next->next->back, v, false );
    }
    
    v( n );
    
};

template <class LNODE, class CONT = std::vector<sptr::shared_ptr<LNODE> > >
struct tip_collector {
    typedef LNODE lnode;
    typedef CONT container;
    
    //container<lnode *> m_nodes;
  
    container m_nodes;
    
public:
    void operator()( lnode *n ) {
        if( n->m_data->isTip ) {
            m_nodes.push_back(n->get_smart_ptr().lock());
        }
    }
};

template <class LNODE>
struct tip_collector_dumb {
    typedef LNODE lnode;
    
    
    //container<lnode *> m_nodes;
  
    std::vector<lnode *> m_nodes;
    
public:
    void operator()( lnode *n ) {
        if( n->m_data->isTip ) {
            m_nodes.push_back(n);
        }
    }
};


template <class visitor>
void visit_edges( typename visitor::lnode *n, visitor &v, bool at_root = true ) {
    assert( n->back != 0 );
    
    if( !at_root ) {
        v( n, n->back );
    }
    
    if( at_root && n->back != 0 ) {
        visit_edges( n->back, v, false );
    }
    if( n->next->back != 0 ) {
        visit_edges( n->next->back, v, false );   
    }

    
    if( n->next->next->back != 0 ) {
        visit_edges( n->next->next->back, v, false );
    }
    // at the root, the edge between n and n->back will be visited when recursing to n->back
      
};

template <class LNODE>
struct edge_collector {
    typedef LNODE lnode;
    
    typedef std::pair<LNODE *, LNODE *> edge;
    std::vector<edge> m_edges;
    
public:
    void operator()( lnode *n1, lnode *n2 ) {
//         std::cout << "edge: " << n1 << " " << n2 << "\n";
        m_edges.push_back( edge( n1, n2 ) );
        
    }
};


class node_level_assignment {
	typedef ivy_mike::tree_parser_ms::lnode lnode;

    std::vector<std::pair<int,lnode *> > m_level_mapping;

    std::tr1::unordered_set<lnode *>m_mix;
    std::tr1::unordered_set<lnode *>m_closed;




    size_t round( int level ) {

//        std::cerr << "round " << level << " " << m_mix.size() << "\n";

        std::tr1::unordered_set<lnode *> cand;

        std::vector<lnode *>rm;


        for( std::tr1::unordered_set<lnode *>::iterator it = m_mix.begin(); it != m_mix.end(); ++it ) {
        	lnode *n = *it;
//        	std::cout << "mix: " << level << " " << n << " " << n->next << "\n";

        	assert( !n->m_data->isTip );

        	if( m_mix.find(n->next) != m_mix.end() ) {
//        		std::cout << "level: " << level << " " << *(n->next->next->m_data) << "\n";

//        		m_level_mapping.push_back( std::pair<int,lnode*>(level, n->next->next ));
        		rm.push_back( n );
//        		rm.push_back( n->next );
//        		if( m_closed.find( n->next->next->back ) == m_closed.end() ) {
//        			m_mix.insert( n->next->next->back );
//        		}
        	}

        }

        for( std::vector<lnode *>::iterator it = rm.begin(); it != rm.end(); ++it ) {
        	lnode *n = *it;
        	m_level_mapping.push_back( std::pair<int,lnode*>(level, n->next->next ));
        	m_closed.insert(n->next->next);

        	m_mix.erase( *it );
        	m_mix.erase( (*it)->next );
        }
        for( std::vector<lnode *>::iterator it = rm.begin(); it != rm.end(); ++it ) {
        	lnode *n = *it;
        	if( m_closed.find( n->next->next->back ) == m_closed.end() ) {
        		m_mix.insert( n->next->next->back );
        	}
        }

    	return m_mix.size();
    }

public:
    node_level_assignment( std::vector<lnode *> tips ) {

        for( std::vector<lnode *>::iterator it = tips.begin(); it != tips.end(); ++it ) {
            m_level_mapping.push_back( std::pair<int,lnode*>( 0, *it ) );
//            m_closed.insert( *it );

            m_closed.insert(*it);
            assert( (*it)->back != 0 );

            if( !(*it)->back->m_data->isTip ) {
            	m_mix.insert( (*it)->back );
            }

        }

        int level = 1;

        while( round( level++ ) != 0 ) {}


//        for( std::vector<std::pair<int,lnode *> >::iterator it = m_level_mapping.begin(); it != m_level_mapping.end(); ++it ) {
//        	std::cout << "level: " << it->first << " " << *(it->second->m_data) << "\n";
//        }

//        std::cout << "end\n";
    }

    std::vector<std::pair<int,lnode *> > &get_level_mapping() {
    	return m_level_mapping;

    }
};





#endif
