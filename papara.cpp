#include "papara.h"
#include "fasta.h"
#include "vec_unit.h"
#include "align_pvec_vec.h"
#include "stepwise_align.h"
#include "align_utils.h"

#include "ivymike/demangle.h"
#include "ivymike/time.h"

#include <iomanip>
#include <boost/bind.hpp>
#include <boost/dynamic_bitset.hpp>
#include <iterator>

using namespace ivy_mike;
using namespace ivy_mike::tree_parser_ms;

using namespace papara;

bool papara::g_dump_aux = false;

template<typename seq_tag>
queries<seq_tag>::queries( const std::string &opt_qs_name ) {


//        if( !opt_qs_name.empty() ) {
    //
    // read query sequences
    //
    
    if( !opt_qs_name.empty() ) {
        std::ifstream qsf( opt_qs_name.c_str() );
        
        if( !qsf.good() ) {
            throw std::runtime_error( "cannot open qs file");
        }
        
        // mix them with the qs from the ref alignment <-- WTF? must have been sniffing whiteboard cleaner... the qs are read before the ref seqs...
        read_fasta( qsf, m_qs_names, m_qs_seqs);
    }
    
    //            if( m_qs_names.empty() ) {
        //                throw std::runtime_error( "no qs" );
        //            }
        
        std::for_each( m_qs_names.begin(), m_qs_names.end(), std::ptr_fun( normalize_name ));
        
        //
        // setup qs best-score/best-edge lists
        //
        
        
        m_qs_pvecs.resize( m_qs_names.size() );
        //        }
        //        m_qs_bestscore.resize(m_qs_names.size());
        //        std::fill( m_qs_bestscore.begin(), m_qs_bestscore.end(), 32000);
        //        m_qs_bestedge.resize(m_qs_names.size());
        
        
        
        
}
template<typename seq_tag>
void queries<seq_tag>::preprocess() {
    //
    // preprocess query sequences
    //
    if( m_qs_seqs.empty() ) {
        throw std::runtime_error( "no query sequences" );
    }

    assert( m_qs_seqs.size() == m_qs_names.size() );
    m_qs_pvecs.resize(m_qs_seqs.size());
    m_qs_cseqs.resize(m_qs_seqs.size());

    for( size_t i = 0; i < m_qs_seqs.size(); i++ ) {
//            seq_to_nongappy_pvec( m_qs_seqs[i], m_qs_pvecs[i] );
        //          static void seq_to_nongappy_pvec( std::vector<uint8_t> &seq, std::vector<uint8_t> &pvec ) {

        // the following line means: transform sequence to pvec using seq_model::s2p as mapping
        // function and only append the mapped character into pvec, if it corresponds to a single (=non gap)
        // character.


        std::transform( m_qs_seqs[i].begin(), m_qs_seqs[i].end(),
                        back_insert_ifer(m_qs_cseqs[i], std::not1( std::ptr_fun(seq_model::cstate_is_gap) )),
                        seq_model::s2c );

        std::transform( m_qs_seqs[i].begin(), m_qs_seqs[i].end(),
                        back_insert_ifer(m_qs_pvecs[i], seq_model::is_single),
                        seq_model::s2p );

//            for( unsigned int i = 0; i < seq.size(); i++ ) {
//                seq_model::pars_state_t ps = seq_model::s2p(seq[i]);
//
//                if( seq_model::is_single(ps)) {
//                    pvec.push_back(ps);
//                }
//
//            }



    }

//        if( write_testbench ) {
//
//            write_qs_pvecs( "qs.bin" );
//            write_ref_pvecs( "ref.bin" );
//        }

}
template<typename seq_tag>
void queries<seq_tag>::add(const std::string& name, std::vector< uint8_t >* qs) {
    m_qs_names.push_back(name);
    ivy_mike::push_back_swap(m_qs_seqs, *qs );
}
template<typename seq_tag>
void queries<seq_tag>::write_pvecs(const char* name) {
    std::ofstream os( name );

    os << m_qs_pvecs.size();
    for( typename std::vector< std::vector< pars_state_t > >::iterator it = m_qs_pvecs.begin(); it != m_qs_pvecs.end(); ++it ) {
        os << " " << it->size() << " ";
        os.write( (char *)it->data(), it->size() );

    }
}
template<typename seq_tag>
size_t queries<seq_tag>::max_name_length() const {
    size_t len = 0;
    for( std::vector <std::string >::const_iterator it = m_qs_names.begin(); it != m_qs_names.end(); ++it ) {
        len = std::max( len, it->size() );
    }

    return len;
}

template<typename seq_tag>
size_t queries<seq_tag>::calc_cups_per_ref(size_t ref_len) const {
    size_t ct = 0;

    typename std::vector<std::vector <pars_state_t> >::const_iterator first = m_qs_pvecs.begin();
    const typename std::vector<std::vector <pars_state_t> >::const_iterator last = m_qs_pvecs.end();

    for(; first != last; ++first ) {
        //ct += (ref_len - first->size()) * first->size();
        ct += ref_len * first->size(); // papara now uses the 'unbanded' aligner
    }



    return ct;
}
template<typename seq_tag>
void queries<seq_tag>::normalize_name(std::string& str) {
    std::string ns;

    // replace runs of one or more whitespaces with an underscores
    bool in_ws_run = false;

    for( std::string::iterator it = str.begin(); it != str.end(); ++it ) {

        if( std::isspace(*it) ) {
            if( !in_ws_run ) {
                ns.push_back( '_' );
                in_ws_run = true;
            }
        } else {
            ns.push_back(*it);
            in_ws_run = false;
        }

    }
    str.swap(ns);

}

//////////////////////////////////////////////////////////////
// references stuff
//////////////////////////////////////////////////////////////

template<typename pvec_t, typename seq_tag>
references<pvec_t,seq_tag>::references(const char* opt_tree_name, const char* opt_alignment_name, queries<seq_tag>* qs) : m_ln_pool(new ln_pool( std::auto_ptr<node_data_factory>(new my_fact<my_adata>) )), spg_(pvec_pgap::pgap_model, &pm_)
{

    //std::cerr << "papara_nt instantiated as: " << typeid(*this).name() << "\n";
    lout << "papara_nt instantiated as: " << ivy_mike::demangle(typeid(*this).name()) << "\n";




//        std::cerr << ivy_mike::isa<papara_nt<pvec_cgap> >(*this) << " " << ivy_mike::isa<papara_nt<pvec_pgap> >(*this) << "\n";
    // load input data: ref-tree, ref-alignment and query sequences

    //
    // parse the reference tree
    //


    ln_pool &pool = *m_ln_pool;
    tree_parser_ms::parser tp( opt_tree_name, pool );
    tree_parser_ms::lnode * n = tp.parse();

    n = towards_tree( n );
    //
    // create map from tip names to tip nodes
    //
    typedef tip_collector<lnode> tc_t;
    tc_t tc;

    visit_lnode( n, tc );

    //boost::dynamic_bitset<> found_tree_taxa( tc.m_nodes.size(), true );



    std::map<std::string, sptr::shared_ptr<lnode> > name_to_lnode;

    for( std::vector< sptr::shared_ptr<lnode> >::iterator it = tc.m_nodes.begin(); it != tc.m_nodes.end(); ++it ) {
//             std::cout << (*it)->m_data->tipName << "\n";
        name_to_lnode[(*it)->m_data->tipName] = *it;
    }


    {
        //
        // read reference alignment: store the ref-seqs in the tips of the ref-tree
        //
        multiple_alignment ref_ma;
        ref_ma.load_phylip( opt_alignment_name );

        std::vector<my_adata *> tmp_adata;
        boost::dynamic_bitset<> unmasked;

        for( unsigned int i = 0; i < ref_ma.names.size(); i++ ) {

            std::map< std::string, sptr::shared_ptr<lnode> >::iterator it = name_to_lnode.find(ref_ma.names[i]);

            // process sequences from the ref_ma depending on, if they are contained in the tree.
            // if they are, they are 'swapped' into m_ref_seqs
            // if they are not, into m_qs_seqs. (gaps in the QS are removed later)
            //
            // additionally, all columns that contain only gaps are removed from the reference sequences.

            if( it != name_to_lnode.end() ) {


                sptr::shared_ptr< lnode > ln = it->second;
                //      adata *ad = ln->m_data.get();

                assert( ivy_mike::isa<my_adata>(ln->m_data.get()) ); //typeid(*ln->m_data.get()) == typeid(my_adata ) );
                my_adata *adata = static_cast<my_adata *> (ln->m_data.get());

                // store the adata ptr corresponding to the current ref sequence for later use.
                // (their indices in m_ref_seqs and tmp_adata correspond.)
                assert( tmp_adata.size() == m_ref_seqs.size() );

                tmp_adata.push_back(adata);
                m_ref_names.push_back(std::string() );
                m_ref_seqs.push_back(std::vector<uint8_t>() );

                m_ref_names.back().swap( ref_ma.names[i] );
                m_ref_seqs.back().swap( ref_ma.data[i] );

                // mark all non-gap positions of the current reference in bit-vector 'unmasked'
                const std::vector<uint8_t> &seq = m_ref_seqs.back();
                if( unmasked.empty() ) {
                    unmasked.resize( seq.size() );
                }
                assert( unmasked.size() == seq.size() );


                for( size_t j = 0, e = seq.size(); j != e; ++j ) {
                    unmasked[j] |= !seq_model::is_gap( seq_model::s2p(seq[j]));
                }

                // erase it from the name to lnode* map, so that it can be used to ideantify tree-taxa without corresponding entries in the alignment
                name_to_lnode.erase(it);

            } else {
                qs->add(ref_ma.names[i], &ref_ma.data[i]);
            }
        }

        if( !name_to_lnode.empty() ) {
            std::cerr << "error: there are " << name_to_lnode.size() << " taxa in the tree with no corresponding sequence in the reference alignment. names:\n";

            for( std::map< std::string, sptr::shared_ptr< lnode > >::iterator it = name_to_lnode.begin(); it != name_to_lnode.end(); ++it ) {
                std::cout << it->first << "\n";
            }

            throw std::runtime_error( "bailing out due to inconsitent input data\n" );

        }

        {
            // remove all 'pure-gap' columns from the ref sequences

            assert( tmp_adata.size() == m_ref_seqs.size() );

            // retrieve a list of non-gap indices from the bit-vector
            std::vector<size_t> unmasked_idx;
            {
                size_t i = unmasked.find_first();
                while( i != unmasked.npos ) {
//                         std::cout << "um: " << i << "\n";

                    unmasked_idx.push_back(i);
                    i = unmasked.find_next(i);
                }
            }
            for( size_t i = 0, e = m_ref_seqs.size(); i != e; ++i ) {

                std::vector<uint8_t> seq_tmp;
                seq_tmp.reserve(unmasked_idx.size());

                const std::vector<uint8_t> &seq_orig = m_ref_seqs[i];

                // copy all unmasked ref characters to seq_tmp
                for( std::vector<size_t>::iterator it = unmasked_idx.begin(); it != unmasked_idx.end(); ++it ) {
                    assert( *it < seq_orig.size() );


                    seq_tmp.push_back( seq_orig[*it] );
                }
                m_ref_seqs[i].swap( seq_tmp );

                //initialize the corresponding adata object with the cleaned ref seq.
                tmp_adata.at(i)->init_pvec( m_ref_seqs[i] );
            }
        }
    }
    pm_.reset( m_ref_seqs );
    std::cout << "p: " << pm_.setup_pmatrix(0.1) << "\n";

    //
    // collect list of edges
    //

    visit_edges( n, m_ec );

    lout << "edges: " << m_ec.m_edges.size() << "\n";

}

template<typename pvec_t, typename seq_tag>
void references<pvec_t,seq_tag>::build_ref_vecs() {
    // pre-create the ancestral state vectors. This step is necessary for the threaded version, because otherwise, each
    // thread would need an independent copy of the tree to do concurrent newviews. Anyway, having a copy of the tree
    // in each thread will most likely use more memory than storing the pre-calculated vectors.

    // TODO: maybe try lazy create/cache of the asv's in the threads

    ivy_mike::timer t1;



    assert( m_ref_aux.empty() && m_ref_pvecs.empty() );

    m_ref_pvecs.reserve( m_ec.m_edges.size() );
    m_ref_aux.reserve( m_ec.m_edges.size() );


    for( size_t i = 0; i < m_ec.m_edges.size(); i++ ) {
        pvec_t root_pvec;

//             std::cout << "newview for branch " << i << ": " << *(m_ec.m_edges[i].first->m_data) << " " << *(m_ec.m_edges[i].second->m_data) << "\n";

        if( i == 340 ) {
            g_dump_aux = true;
        }

        driver<pvec_t,seq_tag>::do_newview( root_pvec, m_ec.m_edges[i].first, m_ec.m_edges[i].second, true );

        g_dump_aux = false;
        // TODO: try something fancy with rvalue refs...

        m_ref_pvecs.push_back( std::vector<int>() );
        m_ref_aux.push_back( std::vector<unsigned int>() );

        root_pvec.to_int_vec(m_ref_pvecs.back());
        root_pvec.to_aux_vec(m_ref_aux.back());


        m_ref_gapp.push_back( std::vector<double>() );

        if( ivy_mike::same_type<pvec_t,pvec_pgap>::result ) {
            // WTF: this is why mixing static and dynamic polymorphism is a BAD idea!
            pvec_pgap *rvp = reinterpret_cast<pvec_pgap *>(&root_pvec);
            rvp->to_gap_post_vec(m_ref_gapp.back());

//              std::transform( m_ref_gapp.back().begin(), m_ref_gapp.back().end(), std::ostream_iterator<int>(std::cout), ivy_mike::scaler_clamp<double>(10,0,9) );
//
//              std::cout << "\n";
        }


    }

    std::cout << "pvecs created: " << t1.elapsed() << "\n";

}

template<typename pvec_t, typename seq_tag>
void references<pvec_t,seq_tag>::write_pvecs(const char* name) {
    std::ofstream os( name );

    os << m_ref_pvecs.size();
    for( size_t i = 0; i < m_ref_pvecs.size(); ++i ) {
        os << " " << m_ref_pvecs[i].size() << " ";
        os.write( (char *)m_ref_pvecs[i].data(), m_ref_pvecs[i].size() * sizeof(int));
        os.write( (char *)m_ref_aux[i].data(), m_ref_aux[i].size() * sizeof(unsigned int));
    }
}

template<typename pvec_t, typename seq_tag>
size_t references<pvec_t,seq_tag>::max_name_length() const {
    size_t len = 0;
    for( std::vector <std::string >::const_iterator it = m_ref_names.begin(); it != m_ref_names.end(); ++it ) {
        len = std::max( len, it->size() );
    }

    return len;
}


bool scoring_results::offer(size_t qs, size_t ref, int score) {
    ivy_mike::lock_guard<ivy_mike::mutex> lock(mtx_);

    candss_.at( qs ).offer( score, ref );

    if( best_score_.at(qs) < score || (best_score_.at(qs) == score && ref < best_ref_.at(qs))) {
        best_score_[qs] = score;
        best_ref_.at(qs) = ref;
        return true;
    }

    return false;

}




namespace papara {
void scoring_results::candidates::offer(int score, size_t ref) {

    candidate c( score,ref);

    insert(std::lower_bound( begin(), end(), c), c );

    if( size() > max_num_) {
        pop_back();
    }
}

bool scoring_results::candidate::operator<(const scoring_results::candidate& other) const {
    if( score_ > other.score_ ) {
        return true;
    } else {
        if( score_ == other.score_ ) {
            return ref_ < other.ref_;
        } else {
            return false;
        }
    }
}


////////////////////////////////////////////////////////
// driver stuff
////////////////////////////////////////////////////////

template<typename seq_tag>
class worker {



    const static size_t VW = vu_config<seq_tag>::width;
    typedef typename vu_config<seq_tag>::scalar vu_scalar_t;
    typedef typename block_queue<seq_tag>::block_t block_t;
    typedef model<seq_tag> seq_model;



    block_queue<seq_tag> &block_queue_;
    scoring_results &results_;

    const queries<seq_tag> &qs_;

    const size_t rank_;

    const papara_score_parameters sp_;

    static void copy_to_profile( const block_t &block, aligned_buffer<vu_scalar_t> *prof, aligned_buffer<vu_scalar_t> *aux_prof ) {
        size_t reflen = block.ref_len;



        assert( reflen * VW == prof->size() );
        assert( reflen * VW == aux_prof->size() );

        typename aligned_buffer<vu_scalar_t>::iterator it = prof->begin();
        typename aligned_buffer<vu_scalar_t>::iterator ait = aux_prof->begin();
    //         std::cout << "reflen: " << reflen << " " << size_t(&(*it)) << "\n";

        for( size_t i = 0; i < reflen; ++i ) {
            for( size_t j = 0; j < VW; ++j ) {
    //                 std::cout << "ij: " << i << " " << j << " " << pvecs[j].size() <<  "\n";


                *it = vu_scalar_t(block.seqptrs[j][i]);
                *ait = (block.auxptrs[j][i] == AUX_CGAP) ? vu_scalar_t(-1) : 0;

                ++it;
                ++ait;
            }
        }


        assert( it == prof->end());
        assert( ait == aux_prof->end());

    }

public:
    worker( block_queue<seq_tag> *bq, scoring_results *res, const queries<seq_tag> &qs, size_t rank, const papara_score_parameters &sp ) 
      : block_queue_(*bq), results_(*res), qs_(qs), rank_(rank), sp_(sp) {}
    void operator()() {



        ivy_mike::timer tstatus;
        ivy_mike::timer tprint;

        uint64_t cups_per_ref = -1;


        uint64_t ncup = 0;

        uint64_t inner_iters = 0;
        uint64_t ticks_all = 0;

        uint64_t ncup_short = 0;

        uint64_t inner_iters_short = 0;
        uint64_t ticks_all_short = 0;


        aligned_buffer<vu_scalar_t> pvec_prof;
        aligned_buffer<vu_scalar_t> aux_prof;
        align_vec_arrays<vu_scalar_t> arrays;
        aligned_buffer<vu_scalar_t> out_scores(VW);
        aligned_buffer<vu_scalar_t> out_scores2(VW);

        while( true ) {
            block_t block;

            if( !block_queue_.get_block(&block)) {
                break;
            }

            if( cups_per_ref == uint64_t(-1) ) {
                cups_per_ref = qs_.calc_cups_per_ref(block.ref_len );
            }

#if 1
       //     assert( VW == 8 );

            pvec_prof.resize( VW * block.ref_len );
            aux_prof.resize( VW * block.ref_len );

            copy_to_profile(block, &pvec_prof, &aux_prof );

            pvec_aligner_vec<vu_scalar_t,VW> pav( block.seqptrs, block.auxptrs, block.ref_len, sp_.match, sp_.match_cgap, sp_.gap_open, sp_.gap_extend, seq_model::c2p, seq_model::num_cstates() );

//            const align_pvec_score<vu_scalar_t,VW> aligner( block.seqptrs, block.auxptrs, block.ref_len, score_mismatch, score_match_cgap, score_gap_open, score_gap_extend );
            for( unsigned int i = 0; i < qs_.size(); i++ ) {

                //align_pvec_score_vec<vu_scalar_t, VW, false, typename seq_model::pars_state_t>( pvec_prof, aux_prof, qs_.pvec_at(i), score_match, score_match_cgap, score_gap_open, score_gap_extend, out_scores, arrays );


                pav.align( qs_.cseq_at(i).begin(), qs_.cseq_at(i).end(), sp_.match, sp_.match_cgap, sp_.gap_open, sp_.gap_extend, out_scores.begin() );

                //aligner.align(qs_.pvec_at(i).begin(), qs_.pvec_at(i).end());
                //const vu_scalar_t *score_vec = aligner.get_scores();



                //ncup += block.num_valid * block.ref_len * qs_.pvec_at(i).size();
#if 0 // test against old version
                align_pvec_score_vec<vu_scalar_t, VW>( pvec_prof.begin(), pvec_prof.end(), aux_prof.begin(), qs_.pvec_at(i).begin(), qs_.pvec_at(i).end(), score_match, score_match_cgap, score_gap_open, score_gap_extend, out_scores2.begin(), arrays );
                bool eq = std::equal( out_scores.begin(), out_scores.end(), out_scores2.begin() );


                if( !eq ) {
                    std::cout << "meeeeeeep!\n";
                }
                //std::cout << "eq: " << eq << "\n";
#endif
                results_.offer( i, block.edges, block.edges + block.num_valid, out_scores.begin() );

            }

            ncup += block.num_valid * cups_per_ref;
            ncup_short += block.num_valid * cups_per_ref;

            ticks_all += pav.ticks_all();
            ticks_all_short += pav.ticks_all();

            inner_iters += pav.inner_iters_all();
            inner_iters_short += pav.inner_iters_all();

            if( rank_ == 0 &&  tprint.elapsed() > 10 ) {

                //std::cout << "thread " << rank_ << " " << ncup << " in " << tstatus.elapsed() << " : "
                std::cout << ncup / (tstatus.elapsed() * 1e9) << " gncup/s, " << ticks_all / double(inner_iters) << " tpili (short: " << ncup_short / (tprint.elapsed() * 1e9) << ", " << ticks_all_short / double(inner_iters_short) << ")\n";

                ncup_short = 0;
                ticks_all_short = 0;
                inner_iters_short = 0;

                tprint = ivy_mike::timer();
            }

        }
#else

//        assert( block.gapp_ptrs[0] != 0 );
//        assert( VW == 4 );
//        const align_pvec_gapp_score<4> aligner( block.seqptrs, block.gapp_ptrs, block.ref_len, score_mismatch, score_match_cgap, score_gap_open, score_gap_extend );
//        for( unsigned int i = 0; i < m_pnt.m_qs_names.size(); i++ ) {
//
//            size_t stride = 1;
//            size_t aux_stride = 1;
//
//            aligner.align(m_pnt.m_qs_pvecs[i]);
//            const float *score_vec = aligner.get_scores();
//
//            ncup += block.num_valid * block.ref_len * m_pnt.m_qs_pvecs[i].size();
//            {
//                ivy_mike::lock_guard<ivy_mike::mutex> lock( m_pnt.m_qmtx );
//
//                for( int k = 0; k < block.num_valid; k++ ) {
//
//
//
//                    if( score_vec[k] < m_pnt.m_qs_bestscore[i] || (score_vec[k] == m_pnt.m_qs_bestscore[i] && block.edges[k] < m_pnt.m_qs_bestedge[i] )) {
//                        const bool validate = false;
//                        if( validate ) {
//                            const int *seqptr = block.seqptrs[k];
//                            const double *gapp_ptr = block.gapp_ptrs[k];
//
////                                std::vector<double> gapp_tmp(gapp_ptr, gapp_ptr + block.ref_len);
//
//
//                            pars_align_gapp_seq pas( seqptr, m_pnt.m_qs_pvecs[i].data(), block.ref_len, m_pnt.m_qs_pvecs[i].size(), stride, gapp_ptr, aux_stride, seq_arrays_gapp, 0, score_gap_open, score_gap_extend, score_mismatch, score_match_cgap );
//                            int res = pas.alignFreeshift(INT_MAX);
//
//                            if( res != score_vec[k] ) {
//
//
//                                std::cout << "meeeeeeep! score: " << score_vec[k] << " " << res << "\n";
//                            }
//                        }
//
//                        m_pnt.m_qs_bestscore[i] = score_vec[k];
//                        m_pnt.m_qs_bestedge[i] = block.edges[k];
//                    }
//                }
//            }
//        }
//    }

#endif
        {
            ivy_mike::lock_guard<ivy_mike::mutex> lock( *block_queue_.hack_mutex() );
            std::cout << "thread " << rank_ << ": " << ncup / (tstatus.elapsed() * 1e9) << " gncup/s\n";
        }
    }
};


template <typename pvec_t,typename seq_tag>
void driver<pvec_t,seq_tag>::calc_scores(size_t n_threads, const my_references& refs, const my_queries& qs, scoring_results* res, const papara_score_parameters& sp) {

    //
    // build the alignment blocks
    //


    block_queue<seq_tag> bq;
    build_block_queue(refs, &bq);

    //
    // work
    //
    ivy_mike::timer t1;
    ivy_mike::thread_group tg;
    lout << "start scoring, using " << n_threads <<  " threads" << std::endl;

    typedef worker<seq_tag> worker_t;

    for( size_t i = 1; i < n_threads; ++i ) {
        tg.create_thread(worker_t(&bq, res, qs, i, sp));
    }

    worker_t w0(&bq, res, qs, 0, sp );

    w0();

    tg.join_all();

    lout << "scoring finished: " << t1.elapsed() << std::endl;

}

template <typename pvec_t,typename seq_tag>
void driver<pvec_t,seq_tag>::do_newview(pvec_t& root_pvec, lnode* n1, lnode* n2, bool incremental) {
    typedef my_adata_gen<pvec_t, seq_tag > my_adata;

    std::deque<rooted_bifurcation<im_tree_parser::lnode> > trav_order;

    //std::cout << "traversal for branch: " << *(n1->m_data) << " " << *(n2->m_data) << "\n";

    rooted_traversal_order( n1, n2, trav_order, incremental );
    //     std::cout << "traversal: " << trav_order.size() << "\n";

    for( std::deque< rooted_bifurcation< ivy_mike::tree_parser_ms::lnode > >::iterator it = trav_order.begin(); it != trav_order.end(); ++it ) {
        //         std::cout << *it << "\n";

        my_adata *p = it->parent->m_data->get_as<my_adata>();
        my_adata *c1 = it->child1->m_data->get_as<my_adata>();
        my_adata *c2 = it->child2->m_data->get_as<my_adata>();
        //         rooted_bifurcation<ivy_mike::tree_parser_ms::lnode>::tip_case tc = it->tc;



        //         std::cout << "tip case: " << (*it) << "\n";


        pvec_t::newview(p->get_pvec(), c1->get_pvec(), c2->get_pvec(), it->child1->backLen, it->child2->backLen, it->tc);

    }





    {
        my_adata *c1 = n1->m_data->get_as<my_adata>();
        my_adata *c2 = n2->m_data->get_as<my_adata>();

        //         tip_case tc;

        if( c1->isTip && c2->isTip ) {
            //                 std::cout << "root: TIP TIP\n";
            pvec_t::newview(root_pvec, c1->get_pvec(), c2->get_pvec(), n1->backLen, n2->backLen, TIP_TIP );
        } else if( c1->isTip && !c2->isTip ) {
            //                 std::cout << "root: TIP INNER\n";
            pvec_t::newview(root_pvec, c1->get_pvec(), c2->get_pvec(), n1->backLen, n2->backLen, TIP_INNER );
            //             root_pvec = c2->get_pvec();
        } else if( !c1->isTip && c2->isTip ) {
            //                 std::cout << "root: INNER TIP\n";
            pvec_t::newview(root_pvec, c2->get_pvec(), c1->get_pvec(), n1->backLen, n2->backLen, TIP_INNER );
            //             root_pvec = c1->get_pvec();
        } else {
            //                 std::cout << "root: INNER INNER\n";
            pvec_t::newview(root_pvec, c1->get_pvec(), c2->get_pvec(), n1->backLen, n2->backLen, INNER_INNER );
        }


    }
    //     std::cout << std::hex;
    //     for( std::vector< parsimony_state >::const_iterator it = root_pvec.begin(); it != root_pvec.end(); ++it ) {
    //         std::cout << *it;
    //     }
    //
    //     std::cout << std::dec << std::endl;

}


template <typename pvec_t,typename seq_tag>
void driver<pvec_t,seq_tag>::build_block_queue(const my_references& refs, my_block_queue* bq) {
    // creates the list of ref-block to be consumed by the worker threads.  A ref-block onsists of N ancestral state sequences, where N='width of the vector unit'.
    // The vectorized alignment implementation will align a QS against a whole ref-block at a time, rather than a single ancestral state sequence as in the
    // sequencial algorithm.

    const static size_t VW = vu_config<seq_tag>::width;

    typedef typename block_queue<seq_tag>::block_t block_t;


    size_t n_groups = (refs.num_pvecs() / VW);
    if( (refs.num_pvecs() % VW) != 0 ) {
        n_groups++;
    }


    //         std::vector<int> seqlist[VW];
    //         const int *seqptrs[VW];
    //         std::vector<unsigned int> auxlist[VW];
    //         const unsigned int *auxptrs[VW];



    for ( size_t j = 0; j < n_groups; j++ ) {
        int num_valid = 0;



        block_t block;

        for( unsigned int i = 0; i < VW; i++ ) {

            size_t edge = j * VW + i;
            if( edge < refs.num_pvecs()) {
                block.edges[i] = edge;
                block.num_valid++;

                block.seqptrs[i] = refs.pvec_at(edge).data();
                block.auxptrs[i] = refs.aux_at(edge).data();

                //                if( !m_ref_gapp[edge].empty() ) {
                //                    block.gapp_ptrs[i] = m_ref_gapp[edge].data();
                //                } else {
                block.gapp_ptrs[i] = 0;
                //                }

                block.ref_len = refs.pvec_size();
                //                     do_newview( root_pvec, m_ec.m_edges[edge].first, m_ec.m_edges[edge].second, true );
                //                     root_pvec.to_int_vec(seqlist[i]);
                //                     root_pvec.to_aux_vec(auxlist[i]);
                //
                //                     seqptrs[i] = seqlist[i].data();
                //                     auxptrs[i] = auxlist[i].data();

                num_valid++;
            } else {
                if( i < 1 ) {
                    std::cout << "edge: " << edge << " " << refs.num_pvecs() << std::endl;

                    throw std::runtime_error( "bad integer mathematics" );
                }
                block.edges[i] = block.edges[i-1];

                block.seqptrs[i] = block.seqptrs[i-1];
                block.auxptrs[i] = block.auxptrs[i-1];
                block.gapp_ptrs[i] = block.gapp_ptrs[i-1];
            }

        }
        bq->push_back(block);
    }
}

template <typename pvec_t,typename seq_tag>
void driver<pvec_t,seq_tag>::seq_to_position_map(const std::vector< uint8_t >& seq, std::vector< int >& map) {
    typedef model<seq_tag> seq_model;

    for( size_t i = 0; i < seq.size(); ++i ) {
        if( seq_model::is_single(seq_model::s2p(seq[i]))) {
            map.push_back(int(i));
        }
    }
}

template <typename pvec_t,typename seq_tag>
void driver<pvec_t,seq_tag>::print_best_scores(std::ostream& os, const my_queries& qs, const scoring_results& res) {
    boost::io::ios_all_saver ioss(os);
    os << std::setfill ('0');
    for( unsigned int i = 0; i < qs.size(); i++ ) {
        os << qs.name_at(i) << " "  << std::setw (4) << res.bestedge_at(i) << " " << std::setw(5) << res.bestscore_at(i) << "\n";
    }
}

template <typename pvec_t,typename seq_tag>
std::vector< std::vector< uint8_t > > driver<pvec_t,seq_tag>::generate_traces(std::ostream& os_quality, std::ostream& os_cands, const my_queries& qs, const my_references& refs, const scoring_results& res, const papara_score_parameters& sp) {


    typedef typename queries<seq_tag>::pars_state_t pars_state_t;
    typedef model<seq_tag> seq_model;


    lout << "generating best scoring alignments\n";
    ivy_mike::timer t1;



    std::vector<pars_state_t> out_qs_ps;
    align_arrays_traceback<int> arrays;

    std::vector<std::vector<uint8_t> > qs_traces( qs.size() );

    std::vector<uint8_t> cand_trace;

    for( size_t i = 0; i < qs.size(); i++ ) {
        size_t best_edge = res.bestedge_at(i);

        assert( size_t(best_edge) < refs.num_pvecs() );

        int score = -1;



        const std::vector<pars_state_t> &qp = qs.pvec_at(i);

        score = align_freeshift_pvec<int>(
                    refs.pvec_at(best_edge).begin(), refs.pvec_at(best_edge).end(),
                    refs.aux_at(best_edge).begin(),
                    qp.begin(), qp.end(),
                    sp.match, sp.match_cgap, sp.gap_open, sp.gap_extend, qs_traces.at(i), arrays
                );


        if( score != res.bestscore_at(i) ) {
            std::cout << "meeeeeeep! score: " << res.bestscore_at(i) << " " << score << "\n";
        }





        if( os_cands.good() ) {
            const scoring_results::candidates &cands = res.candidates_at(i);

            std::vector<std::vector<uint8_t> > unique_traces;

            for( size_t j = 0; j < cands.size(); ++j ) {
                const scoring_results::candidate &cand = cands[j];

                cand_trace.clear();

                score = align_freeshift_pvec<int>(
                            refs.pvec_at(cand.ref()).begin(), refs.pvec_at(cand.ref()).end(),
                            refs.aux_at(cand.ref()).begin(),
                            qp.begin(), qp.end(),
                            sp.match, sp.match_cgap, sp.gap_open, sp.gap_extend, cand_trace, arrays
                        );
                out_qs_ps.clear();

                std::vector<std::vector<uint8_t> >::iterator it;// = unique_traces.end(); //

                if( !true ) {
                    it = std::lower_bound(unique_traces.begin(), unique_traces.end(), cand_trace );
                } else {
                    it = unique_traces.end();
                }

                if( it == unique_traces.end() || *it != cand_trace ) {

                    gapstream_to_alignment_no_ref_gaps(cand_trace, qp, &out_qs_ps, seq_model::gap_state() );

                    unique_traces.insert(it, cand_trace);
                    os_cands << i << " " << cand.ref() << " " << cand.score() << "\t";

                    //os << std::setw(pad) << std::left << qs.name_at(i);
                    std::transform( out_qs_ps.begin(), out_qs_ps.end(), std::ostream_iterator<char>(os_cands), seq_model::p2s );
                    os_cands << std::endl;
                }

            }
        }

    }



    return qs_traces;
}

template <typename pvec_t,typename seq_tag>
void driver<pvec_t,seq_tag>::align_best_scores2(std::ostream& os, std::ostream& os_quality, std::ostream& os_cands, const my_queries& qs, const my_references& refs, const scoring_results& res, size_t pad, const bool ref_gaps, const papara_score_parameters& sp) {

    typedef typename queries<seq_tag>::pars_state_t pars_state_t;
    typedef model<seq_tag> seq_model;


    std::vector<std::vector<uint8_t> > qs_traces = generate_traces(os_quality, os_cands, qs, refs, res, sp );
    std::vector<pars_state_t> out_qs_ps;
    for( size_t i = 0; i < qs.size(); ++i ) {
        const std::vector<pars_state_t> &qp = qs.pvec_at(i);

        out_qs_ps.clear();


        gapstream_to_alignment_no_ref_gaps(qs_traces.at(i), qp, &out_qs_ps, seq_model::gap_state() );
        //boost::dynamic_bitset<> bs( out_qs_ps.size() );
        std::vector<bool> bs( out_qs_ps.size() );
        std::transform( out_qs_ps.begin(), out_qs_ps.end(), bs.begin(), seq_model::is_gap );
    }


}

template <typename pvec_t,typename seq_tag>
void driver<pvec_t,seq_tag>::align_best_scores(std::ostream& os, std::ostream& os_quality, std::ostream& os_cands, const my_queries& qs, const my_references& refs, const scoring_results& res, size_t pad, const bool ref_gaps, const papara_score_parameters& sp) {
    // create the actual alignments for the best scoring insertion position (=do the traceback)

    typedef typename queries<seq_tag>::pars_state_t pars_state_t;
    typedef model<seq_tag> seq_model;


    //     lout << "generating best scoring alignments\n";
    //     ivy_mike::timer t1;



    double mean_quality = 0.0;
    double n_quality = 0.0;
    //


    std::vector<pars_state_t> out_qs_ps;

    // create the best alignment traces per qs


    std::vector<std::vector<uint8_t> > qs_traces = generate_traces(os_quality, os_cands, qs, refs, res, sp );


    // collect ref gaps introduiced by qs
    ref_gap_collector rgc( refs.pvec_size() );
    for( std::vector<std::vector<uint8_t> >::iterator it = qs_traces.begin(); it != qs_traces.end(); ++it ) {
        rgc.add_trace(*it);
    }




    if( ref_gaps ) {
        os << refs.num_seqs() + qs.size() << " " << rgc.transformed_ref_len() << "\n";
    } else {
        os << refs.num_seqs() + qs.size() << " " << refs.pvec_size() << "\n";
    }
    // write refs (and apply the ref gaps)

    for( size_t i = 0; i < refs.num_seqs(); i++ ) {
        os << std::setw(pad) << std::left << refs.name_at(i);

        if( ref_gaps ) {
            rgc.transform( refs.seq_at(i).begin(), refs.seq_at(i).end(), std::ostream_iterator<char>(os), '-' );
        } else {
            std::transform( refs.seq_at(i).begin(), refs.seq_at(i).end(), std::ostream_iterator<char>(os), seq_model::normalize);
        }

        //std::transform( m_ref_seqs[i].begin(), m_ref_seqs[i].end(), std::ostream_iterator<char>(os), seq_model::normalize );
        os << "\n";
    }



    for( size_t i = 0; i < qs.size(); i++ ) {

        const std::vector<pars_state_t> &qp = qs.pvec_at(i);

        out_qs_ps.clear();

        if( ref_gaps ) {
            gapstream_to_alignment(qs_traces.at(i), qp, &out_qs_ps, seq_model::gap_state(), rgc);
        } else {
            gapstream_to_alignment_no_ref_gaps(qs_traces.at(i), qp, &out_qs_ps, seq_model::gap_state() );
        }

        os << std::setw(pad) << std::left << qs.name_at(i);
        std::transform( out_qs_ps.begin(), out_qs_ps.end(), std::ostream_iterator<char>(os), seq_model::p2s );
        os << std::endl;





        if( os_quality.good() && qs.seq_at(i).size() == refs.pvec_size()) {


            std::vector<int> map_ref;
            std::vector<int> map_aligned;
            seq_to_position_map( qs.seq_at(i), map_ref );
            align_utils::trace_to_position_map( qs_traces[i], &map_aligned );


            if( map_ref.size() != map_aligned.size() ) {
                throw std::runtime_error( "alignment quirk: map_ref.size() != map_aligned.size()" );
            }

            size_t num_equal = ivy_mike::count_equal( map_ref.begin(), map_ref.end(), map_aligned.begin() );

            //std::cout << "size: " << map_ref.size() << " " << map_aligned.size() << " " << m_qs_seqs[i].size() << "\n";
            //std::cout << num_equal << " equal of " << map_ref.size() << "\n";

            double score = num_equal / double(map_ref.size());
            //double score = alignment_quality( out_qs, m_qs_seqs[i], debug );

            os_quality << qs.name_at(i) << " " << score << "\n";

            mean_quality += score;
            n_quality += 1;
        }


    }

    lout << "mean quality: " << mean_quality / n_quality << "\n";

}

template <typename pvec_t,typename seq_tag>
void driver<pvec_t,seq_tag>::align_best_scores_oa( output_alignment *oa, const my_queries &qs, const my_references &refs, const scoring_results &res, size_t pad, const bool ref_gaps, const papara_score_parameters &sp ) {
    typedef typename queries<seq_tag>::pars_state_t pars_state_t;
    typedef model<seq_tag> seq_model;


    //     lout << "generating best scoring alignments\n";
    //     ivy_mike::timer t1;



    double mean_quality = 0.0;
    double n_quality = 0.0;
    //


    std::vector<pars_state_t> out_qs_ps;

    

    // supply non-open ofstreams to keep it quiet. This is actually quite a bad interface...
    std::ofstream os_quality;
    std::ofstream os_cands;
    
    
    // create the best alignment traces per qs
    std::vector<std::vector<uint8_t> > qs_traces = generate_traces(os_quality, os_cands, qs, refs, res, sp );


    // collect ref gaps introduiced by qs
    ref_gap_collector rgc( refs.pvec_size() );
    for( std::vector<std::vector<uint8_t> >::iterator it = qs_traces.begin(); it != qs_traces.end(); ++it ) {
        rgc.add_trace(*it);
    }




    if( ref_gaps ) {
        oa->set_size(refs.num_seqs() + qs.size(), rgc.transformed_ref_len());
    } else {
        oa->set_size(refs.num_seqs() + qs.size(), refs.pvec_size());
    }
    oa->set_max_name_length( pad );
    
    // write refs (and apply the ref gaps)

    std::vector<char> tmp;
    for( size_t i = 0; i < refs.num_seqs(); i++ ) {
        tmp.clear();
        
        

        if( ref_gaps ) {
            rgc.transform( refs.seq_at(i).begin(), refs.seq_at(i).end(), std::back_inserter(tmp), '-' );
        } else {
            std::transform( refs.seq_at(i).begin(), refs.seq_at(i).end(), std::back_inserter(tmp), seq_model::normalize);
        }

        oa->push_back( refs.name_at(i), tmp, output_alignment::type_ref );
        //std::transform( m_ref_seqs[i].begin(), m_ref_seqs[i].end(), std::ostream_iterator<char>(os), seq_model::normalize );
        
    }



    for( size_t i = 0; i < qs.size(); i++ ) {
        tmp.clear();
        const std::vector<pars_state_t> &qp = qs.pvec_at(i);

        out_qs_ps.clear();

        if( ref_gaps ) {
            gapstream_to_alignment(qs_traces.at(i), qp, &out_qs_ps, seq_model::gap_state(), rgc);
        } else {
            gapstream_to_alignment_no_ref_gaps(qs_traces.at(i), qp, &out_qs_ps, seq_model::gap_state() );
        }

        //os << std::setw(pad) << std::left << qs.name_at(i);
        std::transform( out_qs_ps.begin(), out_qs_ps.end(), std::back_inserter(tmp), seq_model::p2s );
        
        oa->push_back( qs.name_at(i), tmp, output_alignment::type_qs );





        if( os_quality.good() && qs.seq_at(i).size() == refs.pvec_size()) {


            std::vector<int> map_ref;
            std::vector<int> map_aligned;
            seq_to_position_map( qs.seq_at(i), map_ref );
            align_utils::trace_to_position_map( qs_traces[i], &map_aligned );


            if( map_ref.size() != map_aligned.size() ) {
                throw std::runtime_error( "alignment quirk: map_ref.size() != map_aligned.size()" );
            }

            size_t num_equal = ivy_mike::count_equal( map_ref.begin(), map_ref.end(), map_aligned.begin() );

            //std::cout << "size: " << map_ref.size() << " " << map_aligned.size() << " " << m_qs_seqs[i].size() << "\n";
            //std::cout << num_equal << " equal of " << map_ref.size() << "\n";

            double score = num_equal / double(map_ref.size());
            //double score = alignment_quality( out_qs, m_qs_seqs[i], debug );

            os_quality << qs.name_at(i) << " " << score << "\n";

            mean_quality += score;
            n_quality += 1;
        }


    }
}
void output_alignment_phylip::write_seq_phylip(const std::string& name, const out_seq& seq) {
    size_t pad = std::max( max_name_len_, name.size() + 1 );

    os_ << std::setw(pad) << std::left << name;

    std::copy( seq.begin(), seq.end(), std::ostream_iterator<char>(os_) );
    os_ << "\n";
}
void output_alignment_phylip::push_back(const std::string& name, const out_seq& seq, output_alignment::seq_type t) {
    if( !header_flushed_ ) {
        os_ << num_rows_ << " " << num_cols_ << "\n";
        header_flushed_ = true;
    }
    
    write_seq_phylip( name, seq );
}

void output_alignment_fasta::push_back(const std::string& name, const out_seq& seq, output_alignment::seq_type t) {
    os_ << ">" << name << "\n";
    
    std::copy( seq.begin(), seq.end(), std::ostream_iterator<char>(os_) );
    os_ << "\n";
}

}



template class queries<tag_dna>;
template class queries<tag_aa>;

template class references<pvec_pgap,tag_dna>;
template class references<pvec_cgap,tag_dna>;

template class references<pvec_cgap,tag_aa>;
template class references<pvec_pgap,tag_aa>;


template class driver<pvec_pgap,tag_dna>;
template class driver<pvec_cgap,tag_dna>;

template class driver<pvec_cgap,tag_aa>;
template class driver<pvec_pgap,tag_aa>;

