#include "ivymike/multiple_alignment.h"
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <vector>
#include <deque>
#include <map>
#include <functional>
#include <cstring>
#include <boost/io/ios_state.hpp>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/iostreams/tee.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/bind.hpp>
#include <boost/array.hpp>
#include <limits>

#include "parsimony.h"
#include "pvec.h"

#include "pars_align_seq.h"
#include "pars_align_gapp_seq.h"
#include "fasta.h"
#include "vec_unit.h"
#include "align_pvec_vec.h"

#include "ivymike/tree_parser.h"
#include "ivymike/time.h"
#include "ivymike/getopt.h"
#include "ivymike/thread.h"
#include "ivymike/demangle.h"
#include "ivymike/stupid_ptr.h"
#include "ivymike/algorithm.h"
#include "ivymike/smart_ptr.h"
#include "raxml_interface.h"

using namespace ivy_mike::tree_parser_ms;
namespace ublas = boost::numeric::ublas;

namespace {
    typedef boost::iostreams::tee_device<std::ostream, std::ofstream> log_device;
    typedef boost::iostreams::stream<log_device> log_stream;

    log_stream lout;

    template<typename stream_, typename device_>
    class bios_open_guard {
        stream_ &m_stream;
    public:
        bios_open_guard( stream_ &stream, device_ &device ) : m_stream(stream) {
            m_stream.open( device );
        }
        ~bios_open_guard() {
            m_stream.close();
        }
    };

    typedef bios_open_guard<log_stream, log_device> log_stream_guard;
}




//class my_adata : public ivy_mike::tree_parser_ms::adata {
////     static int ct;
//    //std::vector<parsimony_state> m_pvec;
//    pvec_t m_pvec;
//
//public:
////     int m_ct;
//    my_adata_gen() {
//
////         std::cout << "my_adata\n";
//
//    }
//
//    virtual ~my_adata_gen() {
//
////         std::cout << "~my_adata\n";
//
//    }
//
//    virtual void visit() {
////         std::cout << "tr: " << m_ct << "\n";
//    }
//    void init_pvec(const std::vector< uint8_t >& seq) {
//
//
//        m_pvec.init( seq );
////         std::cout << "init_pvec: " << m_pvec.size() << "\n";
////                 m_pvec.reserve(seq.size());
////         for( std::vector< uint8_t >::const_iterator it = seq.begin(); it != seq.end(); ++it ) {
////             m_pvec.push_back(dna_to_parsimony_state(*it));
////
////         }
//    }
//    pvec_t &get_pvec() {
//        return m_pvec;
//    }
//
//
//};




class log_odds_aligner {
	typedef ublas::matrix<double> dmat;
	typedef std::vector<double> dsvec;
public:
	log_odds_aligner( const dmat &state, const dsvec &gap, boost::array<double,4> state_freq )
	  : ref_state_prob_(state), ref_gap_prob_(gap), ref_len_(state.size1()),
	    state_freq_(state_freq),
	    neg_inf_( -std::numeric_limits<double>::infinity() ),
	    delta_log_(log(0.1)),
	    epsilon_log_(log(0.5))
	{
	}

	void setup( size_t qlen ) {
		assert( ref_gap_prob_.size() == ref_len_ );

		m_.resize( qlen + 1, ref_len_ + 1 );
		d_.resize( qlen + 1, ref_len_ + 1 );
		i_.resize( qlen + 1, ref_len_ + 1 );

		// matrix organization: ref->cols, q->rows

		// init first rows
		std::fill( m_.begin1().begin(), m_.begin1().end(), 0.0 );
		std::fill( d_.begin1().begin(), d_.begin1().end(), 0.0 );
		std::fill( i_.begin1().begin(), i_.begin1().end(), neg_inf_ );

		// init first columns
		std::fill( m_.begin2().begin(), m_.begin2().end(), 0.0 );
		std::fill( d_.begin2().begin(), d_.begin2().end(), neg_inf_ );
		std::fill( i_.begin2().begin(), i_.begin2().end(), 0.0 );

		precalc_log_odds();
	}

	class log_odds {
	public:

		log_odds( double bg_prob ) : bg_prob_(bg_prob) {}

		inline double operator()( double p ) {
			return log( p / bg_prob_ );
		}

	private:
		const double bg_prob_;
	};

	void precalc_log_odds() {
		ref_state_lo_.resize( ref_state_prob_.size2(), ref_state_prob_.size1() );

		for( size_t i = 0; i < 4; ++i ) {
			const ublas::matrix_column<dmat> pcol( ref_state_prob_, i );
			ublas::matrix_row<dmat> lorow( ref_state_lo_, i );
			std::transform( pcol.begin(), pcol.end(), lorow.begin(), log_odds(state_freq_[i]));
		}

		const double gap_freq = 0.83;

		{
			log_odds lo_ngap( 1 - gap_freq );
			log_odds lo_gap( gap_freq );

			ref_ngap_lo_.resize(ref_gap_prob_.size());
			ref_gap_lo_.resize(ref_gap_prob_.size());
			for( size_t i = 0; i < ref_gap_prob_.size(); ++i ) {
				ref_ngap_lo_[i] = lo_ngap(1 - ref_gap_prob_[i]);
				ref_gap_lo_[i] = lo_gap( ref_gap_prob_[i] );
			}
		}
	}

	template<typename T>
	static T max3( const T &a, const T &b, const T &c ) {
		return std::max( a, std::max( b, c ));
	}

	double align( const std::vector<uint8_t> &qs ) {
		const size_t qlen = qs.size();

		setup( qlen );

		//dmat ref_state_trans = trans(ref_state_prob_);



		for( size_t i = 1; i < qlen + 1; ++i ) {
			const int b = qs[i-1];
//			std::cout << "b: " << b << "\n";

			const double b_freq = state_freq_.at(b);
			const ublas::matrix_column<dmat> b_state( ref_state_prob_, b );
			const ublas::matrix_row<dmat> b_state_lo( ref_state_lo_, b );

//			const ublas::matrix_row<dmat> m0( m_, i );
//			const ublas::matrix_row<dmat> m1( m_, i - 1 );
//			const ublas::matrix_row<dmat> i0( i_, i );
//			const ublas::matrix_row<dmat> i1( i_, i - 1 );
//			const ublas::matrix_row<dmat> d0( d_, i );

//			const ublas::matrix_column<dmat> ngap_prob( ref_gap_prob_, 0 );
//			const ublas::matrix_column<dmat> gap_prob( ref_gap_prob_, 1 );

			for( size_t j = 1; j < ref_len_ + 1; ++j ) {
				//ublas::matrix_row<dmat> a_state(ref_state_prob_, j-1 );
				//ublas::matrix_row<dmat> a_gap(ref_gap_prob_, j-1 );

				//double match_log_odds = log( b_state[j-1] / b_freq );
				double match_log_odds = b_state_lo[j-1];


				if( match_log_odds < -100 ) {
				//	std::cout << "odd: " << match_log_odds << b_state[j-1] << " " << b_freq << " " << b << "\n";
					match_log_odds = -100;
				}


#if 0
				const double gap_freq = 0.83;
				double ngap_log_odds = log( (1 - ref_gap_prob_[j-1]) / (1-gap_freq) );
				double gap_log_odds = log( ref_gap_prob_[j-1] / gap_freq );
				gap_log_odds = std::max(-100.0, gap_log_odds);
				ngap_log_odds = std::max(-100.0, ngap_log_odds);
#else
				double gap_log_odds = ref_gap_lo_[j-1];
				double ngap_log_odds = ref_ngap_lo_[j-1];
#endif
				double m_max = max3(
						m_(i-1, j-1) + ngap_log_odds,
						d_(i-1, j-1) + gap_log_odds,
						i_(i-1, j-1) + gap_log_odds
				);

				m_(i,j) = m_max + match_log_odds;

#if 0
				std::cout << i << " " << j << " " << m_(i,j) << " : " << m_(i-1, j-1) + ngap_log_odds
						<< " " << d_(i-1, j-1) + gap_log_odds << " " << i_(i-1, j-1) + gap_log_odds << " " << match_log_odds << " " << gap_log_odds << " " << ngap_log_odds << " max: " << m_max << "\n";
#endif
				double i_max = std::max(
						m_(i-1,j) + delta_log_,
						i_(i-1,j) + epsilon_log_
				);
				i_(i,j) = i_max;

				double d_max = std::max(
						m_(i,j-1) + delta_log_,
						d_(i,j-1) + epsilon_log_
				);

				d_(i,j) = d_max;

			}
		}
		{
			ublas::matrix_row<dmat> m_last(m_, qlen);
			ublas::matrix_row<dmat>::iterator max_it;

			max_it = std::max_element( m_last.begin() + qlen, m_last.end() );
			max_col_ = std::distance(m_last.begin(), max_it);
			max_score_ = *max_it;



			return max_score_;
		}
	}


	void traceback( std::vector<uint8_t> *tb ) {
		assert( tb != 0 );

		//const ublas::matrix_column<dmat> ngap_prob( ref_gap_prob_, 0 );
		//const ublas::matrix_column<dmat> gap_prob( ref_gap_prob_, 1 );

		std::cout << "tb: max_col: " << max_col_ << "\n";

		size_t i = m_.size1() - 1;
		size_t j = m_.size2() - 1;

		while( j > max_col_ ) {
			tb->push_back(1);
			--j;
		}

		bool in_d = false;
		bool in_i = false;
		while( j > 0 && i > 0 ) {

			if( in_d ) {
				assert( !in_i );
				tb->push_back(1);
				--j;
				in_d = m_(i,j) + delta_log_ < d_(i,j) + epsilon_log_;
			} else if( in_i ) {
				assert( !in_d );

				tb->push_back(2);

				--i;
				in_i = m_(i,j) + delta_log_ < i_(i,j) + epsilon_log_;
			} else {

				// the j - 1 corrects for the +1 offset, it does not mean 'last column'!
				const double gap_freq = 0.83;
				double ngap_log_odds = log( (1-ref_gap_prob_[j-1]) / (1-gap_freq) );
				double gap_log_odds = log( ref_gap_prob_[j-1] / gap_freq );
				gap_log_odds = std::max(-100.0, gap_log_odds);
				ngap_log_odds = std::max(-100.0, ngap_log_odds);


				tb->push_back(0);

				--i;
				--j;

				double vm = m_(i,j) + ngap_log_odds;
				double vi = i_(i,j) + gap_log_odds;
				in_i = vm < vi;

				if( in_i ) {
					in_d = vi < d_(i,j) + gap_log_odds;
					in_i = !in_d; // choose between d and i
				} else {
					in_d = vm < d_(i,j) + gap_log_odds;
				}
			}

		}

		while( j > 0 ) {
			tb->push_back(1);
			--j;
		}
		while( i > 0 ) {
			tb->push_back(2);
			--i;
		}

	}

private:
	dmat ref_state_prob_;
	dsvec ref_gap_prob_;

	dmat ref_state_lo_;
	dsvec ref_gap_lo_;
	dsvec ref_ngap_lo_;

	const size_t ref_len_;
	const boost::array<double,4> state_freq_;

	const double neg_inf_;

	dmat m_;
	dmat d_;
	dmat i_;

	const double delta_log_;// = log(0.1);
	const double epsilon_log_;// = log(0.5);

	size_t max_col_;
	double max_score_;
};

class my_adata : public ivy_mike::tree_parser_ms::adata {

public:

	typedef boost::numeric::ublas::matrix<double> apvecs;


	my_adata() : anc_gap_probs_valid_(false)
	{
	}

	void init_gap_vec(const std::vector< uint8_t >& seq) {
    	gap_vec_.init(seq);

    	update_ancestral_gap_prob();
    }

    void init_anc_prob_vecs( const apvecs &apv ) {
    	anc_state_probs_ = apv; // if this uses too much memory, change to move semantics.
    }

    const pvec_pgap &get_gap_vec() const {
    	return gap_vec_;
    }

    pvec_pgap *get_gap_vec_ptr() {
    	anc_gap_probs_valid_ = false; // we are exposing internal state, so this _might_ invalidate the cache
		return &gap_vec_;
	}

    void update_ancestral_gap_prob() { // aahrg, this is stupid

    	anc_gap_prob_.resize(gap_vec_.size() );
    	gap_vec_.to_ancestral_gap_prob(anc_gap_prob_.begin());


//    	anc_gap_probs_ = trans( gap_vec_.get_pgap() );
    	anc_gap_probs_valid_ = true;
    }

    void print_vecs( std::ostream &os ) {


    	const size_t len = anc_state_probs_.size1();

    	assert( len == anc_gap_prob_.size() );


    	for( size_t i = 0; i < len; ++i ) {
    		os << "( ";
    		for( size_t j = 0; j < 4; ++j ) {
    			os << anc_state_probs_(i,j) << " ";
    		}
    		os << ") " << anc_gap_prob_[i] << "\n";
    	}
    }

    const apvecs &state_probs() {
    	return anc_state_probs_;
    }

    const std::vector<double> &gap_probs() {
    	assert( anc_gap_probs_valid_ && "extra anal check because of the stupid cached transposed gap state vector." );
    	return anc_gap_prob_;
    }

private:
    apvecs anc_state_probs_;
    //apvecs anc_gap_probs_; // this is actually a transposed version of gap_vec_.get_pgap()
    pvec_pgap gap_vec_;
    std::vector<double> anc_gap_prob_;

    bool anc_gap_probs_valid_;
};


class my_fact : public ivy_mike::tree_parser_ms::node_data_factory {

    virtual my_adata *alloc_adata() {
        return new my_adata;
    }

};


class queries {
public:
	void load_fasta( const char *name ) {
		std::ifstream is( name );
		assert( is.good() );
		assert( names_.size() == raw_seqs_.size() );
		read_fasta( is, names_, raw_seqs_ );
	}


	// WARNING: unsafe move semantics on parameter seq!
	void add( const std::string &name, std::vector<uint8_t> &seq ) {
		names_.push_back(name);
		ivy_mike::push_back_swap(raw_seqs_, seq );
	}



	void preprocess() {
		seqs_.resize(raw_seqs_.size());

		for( size_t i = 0; i < raw_seqs_.size(); ++i ) {
			seqs_.at(i).clear();

			// recode non-gap characters in raw_seq into seq
			// once again i'm starting doubt that it's a good idea to do _everyting_
			// with stl algorithms *g*.

			std::transform( raw_seqs_[i].begin(), raw_seqs_[i].end(),
					back_insert_ifer(seqs_[i], std::bind2nd(std::less_equal<uint8_t>(), 3 ) ),
					encode_dna );

		}
	}

	size_t size() {
		return names_.size();
	}
	const std::vector<uint8_t> &get_raw( size_t i ) {
		return raw_seqs_.at(i);
	}
	const std::vector<uint8_t> &get_recoded( size_t i ) {
		return seqs_.at(i);
	}

private:
	static bool is_dna( uint8_t c ) {
		switch( c ) {
		case 'a':
		case 'c':
		case 'g':
		case 't':
		case 'A':
		case 'C':
		case 'G':
		case 'T':
			return true;
		default:
		{}
		}
		return false;
	}

	static uint8_t encode_dna( uint8_t c ) {
		switch( c ) {
		case 'a':
		case 'A':
			return 0;

		case 'c':
		case 'C':
			return 1;

		case 'g':
		case 'G':
			return 2;

		case 't':
		case 'T':
			return 3;

		default:
		{}
		}
		return std::numeric_limits<uint8_t>::max();
	}

	std::vector<std::string> names_;
	std::vector<std::vector<uint8_t> > raw_seqs_; // seqs in the source alphabet (e.g. ACGT)

	//
	// stuff initialized by preprocess
	//
	std::vector<std::vector<uint8_t> > seqs_; // seqs in recoded alphabet (= 0 1 2 3...)
};

class references {
public:
	references( sptr::shared_ptr<ln_pool> pool, const std::string &tree_name, const std::string &ali_name )
	  : pool_(pool), tree_name_(tree_name), ali_name_(ali_name)
	{}


	void preprocess( queries &qs ) {

		std::vector<my_adata::apvecs> all_pvecs;

		ivy_mike::timer t1;

		lnode *t = generate_marginal_ancestral_state_pvecs(*pool_, tree_name_, ali_name_, &all_pvecs );

		std::cout << "generate: " << t1.elapsed() << "\n";

		std::vector<lnode *> nodes;
		iterate_lnode( t, std::back_inserter(nodes) );

		{
			// load reference seqs and filter out the seqs that are not in the tree
			// (they are added to the query seqs.)

			ivy_mike::multiple_alignment ref_ma;
			ref_ma.load_phylip( ali_name_.c_str() );


			std::vector<std::string> tip_names;

			for( std::vector<lnode *>::iterator it = nodes.begin(); it != nodes.end(); ++it ) {
				if( (*it)->m_data->isTip ) {
					tip_names.push_back( (*it)->m_data->tipName );
				}
			}

			std::sort( tip_names.begin(), tip_names.end() );
			for( size_t i = 0; i < ref_ma.names.size(); ++i ) {
				if( std::binary_search(tip_names.begin(), tip_names.end(), ref_ma.names[i] )) {
					ivy_mike::push_back_swap( ref_names_, ref_ma.names[i]);
					ivy_mike::push_back_swap( ref_seqs_, ref_ma.data[i]);
				} else {
					qs.add( ref_ma.names[i], ref_ma.data[i] );
				}
			}
		}


		//
		// inject the probgap model ...
		//

		probgap_model pm( ref_seqs_ );
		std::cout << "p: " << pm.setup_pmatrix(0.1) << "\n";
		ivy_mike::stupid_ptr_guard<probgap_model> spg( pvec_pgap::pgap_model, &pm );


		//
		// initialize node/tip data
		//
		lnode *root = 0;
		for( std::vector<lnode *>::iterator it = nodes.begin(); it != nodes.end(); ++it ) {
			const std::string &nl = (*it)->m_data->nodeLabel;

			my_adata *mad = (*it)->m_data->get_as<my_adata>();

			if( !nl.empty() ) {
				assert( std::count_if(nl.begin(), nl.end(), isdigit)  == ptrdiff_t(nl.size()) );

				size_t nodenum = atoi( nl.c_str() );


				mad->init_anc_prob_vecs( all_pvecs.at(nodenum) );
			} else {
				assert( root == 0 );
				root = *it;
			}

			if( mad->isTip ) {
				size_t idx = std::find(ref_names_.begin(), ref_names_.end(), mad->tipName ) - ref_names_.begin();
				std::cout << "tip name: " << mad->tipName << " " << idx << "\n";
				//size_t idx = mp->second;
				mad->init_gap_vec( ref_seqs_.at(idx));
			}
		}



		assert( root != 0 );
		std::cout << "pvecs: " << all_pvecs.size() << "\n";

		base_freqs_ = count_base_freqs( ref_seqs_.begin(), ref_seqs_.end());


		//
		// generate rooted traversal order
		//

		trav_order_.clear();
		rooted_preorder_traversal( root->back, std::front_inserter(trav_order_), false );
		rooted_preorder_traversal( root->next->back, std::front_inserter(trav_order_), false );
		rooted_preorder_traversal( root->next->next->back, std::front_inserter(trav_order_), false );


		if( false ) { // unit test...
			std::deque<rooted_bifurcation<lnode> > trav_order_old;
			rooted_traversal_order( root->back, root->next->back, root->next->next->back, trav_order_old, false );

			assert( trav_order_.size() == trav_order_old.size() );
			assert( std::equal( trav_order_.begin(), trav_order_.end(), trav_order_old.begin()) );
		}


		//
		// do a whole-tree newview to calculate the gap probabilities
		//
		for( std::deque< rooted_bifurcation< ivy_mike::tree_parser_ms::lnode > >::iterator it = trav_order_.begin(); it != trav_order_.end(); ++it ) {
//         std::cout << *it << "\n";

			my_adata *p = dynamic_cast<my_adata *>( it->parent->m_data.get());
			const my_adata *c1 = dynamic_cast<my_adata *>( it->child1->m_data.get());
			const my_adata *c2 = dynamic_cast<my_adata *>( it->child2->m_data.get());
//         rooted_bifurcation<ivy_mike::tree_parser_ms::lnode>::tip_case tc = it->tc;

//			std::cout << "tip case: " << (*it) << "\n";
			p->get_gap_vec_ptr()->newview( c1->get_gap_vec(), c2->get_gap_vec(), it->child1->backLen, it->child2->backLen, it->tc);

			p->update_ancestral_gap_prob();
			//p->cache_transposed_gap_probs();
			//p->print_vecs(std::cout);

		}



	}

	size_t node_size() {
		return trav_order_.size();
	}
	lnode *get_node( size_t i ) {
		return trav_order_.at(i).parent;
	}

	const boost::array<double,4> &base_freqs() {
		return base_freqs_;
	}

private:
	template<typename iiter>
	static boost::array<double,4> count_base_freqs( iiter start, iiter end ) {
		boost::array<double,4> counts;
		counts.fill(0);

		while( start != end ) {
			const std::vector<uint8_t> &seq = *start++;

			for( std::vector<uint8_t>::const_iterator it = seq.begin(); it != seq.end(); ++it ) {
				switch( *it ) {
				case 'a':
				case 'A':
					++counts[0];
					break;

				case 'c':
				case 'C':
					++counts[1];
					break;

				case 'g':
				case 'G':
					++counts[2];
					break;

				case 't':
				case 'T':
					++counts[3];
					break;

				default:
				{}
				}
			}
		}

		// this STL stuff is becoming kind of a bad habit ...
		// the following code divides the elements of 'counts' by the sum of all elements
		double all_count = std::accumulate( counts.begin(), counts.end(), 0 );
		std::transform( counts.begin(), counts.end(), counts.begin(), std::bind2nd(std::divides<double>(), all_count ));

		std::copy( counts.begin(), counts.end(), std::ostream_iterator<double>(std::cout, " "));
		std::cout << "\n";
		return counts;
	}

	sptr::shared_ptr<ln_pool> pool_;

	const std::string tree_name_;
	const std::string ali_name_;


	//
	// stuff initialized by preprocess
	//
	std::vector <std::string > ref_names_;
	std::vector <std::vector<uint8_t> > ref_seqs_;

	boost::array<double,4> base_freqs_;

	std::deque<rooted_bifurcation<lnode> > trav_order_;
};

std::string filename( const std::string &run_name, const char *type ) {
	std::stringstream ss;

	ss << "propara_" << type << "." << run_name;

	return ss.str();
}

bool file_exists(const char *filename)
{
	std::ifstream is(filename);
	return is;
}


uint8_t decode_dna( int s ) {
	assert( s >= 0 && s < 4 );
	const static uint8_t map[4] = {'A','C','G','T'};

	return map[size_t(s)];
}

void realize_trace( const std::vector<uint8_t> seq, const std::vector<uint8_t> &tb, std::vector<uint8_t> *out ) {
	assert( out != 0 );

	std::vector<uint8_t>::const_reverse_iterator tb_it;
	std::vector<uint8_t>::const_iterator seq_it;
	for( tb_it = tb.rbegin(), seq_it = seq.begin(); tb_it != tb.rend(); ++tb_it ) {
		if( *tb_it == 0  ) {
			assert( seq_it != seq.end() );

			out->push_back(decode_dna(*seq_it));
			++seq_it;

		} else if( *tb_it == 2 ) {
			assert( seq_it != seq.end() );
			++seq_it;
		} else {
			out->push_back('-');
		}
	}
}

int main( int argc, char *argv[] ) {
    namespace igo = ivy_mike::getopt;
    ivy_mike::getopt::parser igp;
    std::string opt_tree_name;
    std::string opt_alignment_name;
    std::string opt_qs_name;
    bool opt_use_cgap;
    int opt_num_threads;
    std::string opt_run_name;
    bool opt_write_testbench;
    bool opt_use_gpu;
    bool opt_force_overwrite;
    igp.add_opt('t', igo::value<std::string>(opt_tree_name));
    igp.add_opt('s', igo::value<std::string>(opt_alignment_name));
    igp.add_opt('q', igo::value<std::string>(opt_qs_name));
    igp.add_opt('c', igo::value<bool>(opt_use_cgap, true).set_default(false));
    igp.add_opt('j', igo::value<int>(opt_num_threads).set_default(1));
    igp.add_opt('n', igo::value<std::string>(opt_run_name).set_default("default"));
    igp.add_opt('b', igo::value<bool>(opt_write_testbench, true).set_default(false));
    igp.add_opt('g', igo::value<bool>(opt_use_gpu, true).set_default(false));
    igp.add_opt('f', igo::value<bool>(opt_force_overwrite, true).set_default(false));
    igp.parse(argc, argv);
    if(igp.opt_count('t') != 1 || igp.opt_count('s') != 1){
        std::cerr << "missing options -t and/or -s (-q is optional)\n";
        return 0;
    }
    ivy_mike::timer t;
    const char *qs_name = 0;
    if(!opt_qs_name.empty()){
        qs_name = opt_qs_name.c_str();
    }
    std::string log_filename = filename(opt_run_name, "log");
    if(opt_run_name != "default" && !opt_force_overwrite && file_exists(log_filename.c_str())){
        std::cout << "log file already exists for run '" << opt_run_name << "'\n";
        return 0;
    }
    std::ofstream logs(log_filename.c_str());
    if(!logs){
        std::cout << "could not open logfile for writing: " << log_filename << std::endl;
        return 0;
    }
    log_device ldev(std::cout, logs);
    log_stream_guard lout_guard(lout, ldev);
    lout << "bla\n";
    sptr::shared_ptr<ln_pool> pool(new ln_pool(std::auto_ptr<node_data_factory>(new my_fact)));
    queries qs;
    if(qs_name != 0){
        qs.load_fasta(qs_name);
    }
    references refs(pool, opt_tree_name, opt_alignment_name);
    refs.preprocess(qs);
    qs.preprocess();

    {
    	const std::vector<uint8_t> &b = qs.get_recoded(0);


    	for( size_t i = 0; i < refs.node_size(); ++i ) {
			lnode *a = refs.get_node(i);
			assert( a->towards_root );
			assert( a->m_data != 0 );

			my_adata *ma = a->m_data->get_as<my_adata>();


			log_odds_aligner ali( ma->state_probs(), ma->gap_probs(), refs.base_freqs() );

			double score = ali.align(b);

			std::cout << "score: " << score << "\n";

#if 0
			std::vector<uint8_t> tb;

			ali.traceback(&tb);

			std::copy( tb.begin(), tb.end(), std::ostream_iterator<int>(std::cout));
			std::cout << "\n";

			std::vector<uint8_t> rtb;
			realize_trace( b, tb, &rtb );

			std::copy( rtb.begin(), rtb.end(), std::ostream_iterator<char>(std::cout));
			std::cout << "\n";
#endif
	//		break;
    	}
    }




    std::cout << t.elapsed() << std::endl;
    lout << "SUCCESS " << t.elapsed() << std::endl;
    return 0;
}
