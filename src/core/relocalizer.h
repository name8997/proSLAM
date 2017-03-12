#pragma once
#include <queue>

#include "types/relocalizer_types.h"
#include "types/aligners/aligner_factory.h"

namespace proslam {
  class Relocalizer {

  public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    Relocalizer();
    ~Relocalizer();

  public:

    //ds initialize closer module for a new keyframe
    void init(const LocalMap* keyframe);

    //ds integrate frame into loop closing pool
    void train();

    //ds flushes all frames in current queue
    void flush();

    //ds retrieve loop closure candidates for the given cloud
    void detect(const bool& force_matching_ = false);

    //ds geometric verification
    void compute();

    //ds retrieve correspondences from matches
    inline const Correspondence* getCorrespondenceNN(const MatchPtrVector& matches_);

    inline const CorrespondenceCollectionPointerVector& closures() const {return _closures;}
    inline void setClosures(CorrespondenceCollectionPointerVector& closures_) {_closures=closures_;}
    void clear();

    //ds configuration
    void setPreliminaryMinimumInterspaceQueries(const Count& preliminary_minimum_interspace_queries_) {_preliminary_minimum_interspace_queries = preliminary_minimum_interspace_queries_;}
    void setPreliminaryMinimumMatchingRatio(const real& preliminary_minimum_matching_ratio_) {_preliminary_minimum_matching_ratio = preliminary_minimum_matching_ratio_;}
    void setMinimumAbsoluteNumberOfMatchesPointwise(const Count& minimum_absolute_number_of_matches_pointwise_) {_minimum_absolute_number_of_matches_pointwise = minimum_absolute_number_of_matches_pointwise_;}

    XYZAligner* aligner() {return _aligner;}

  protected:

    //ds currently found closures
    CorrespondenceCollectionPointerVector _closures;

    //ds active query for closure search
    Query* _query;

    //ds intermediate buffer for query frames before they enter the history
    std::queue<Query*> _query_history_queue;

    //ds frame-wise descriptor point clouds
    std::vector<Query*> _query_history;

    //ds minimum query interspace
    Count _preliminary_minimum_interspace_queries = 5;

    //ds minimum relative number of matches
    real _preliminary_minimum_matching_ratio = 0.1;

    //ds minimum absolute number of matches
    Count _minimum_absolute_number_of_matches_pointwise = 100;

    //ds correspondence retrieval
    std::set<Identifier> _mask_id_references_for_correspondences;
    Count _minimum_matches_per_correspondence = 0;
    XYZAligner* _aligner = 0;

    //ds module time consumption
    CREATE_CHRONOMETER(overall)

  }; //class LoopBox
} //namespace gtracker
