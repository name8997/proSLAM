#pragma once
#include "relocalization/local_map_correspondence.h"

namespace proslam {

  //ds the world map is an overarching map entity, generating and owning all landmarks, frames and local map objects
  class WorldMap {
  public: EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  //ds object handling
  public:

    WorldMap(const WorldMapParameters* parameters_);
    ~WorldMap();

  //ds functionality
  public:

    //ds clears all internal structures (prepares a fresh world map)
    void clear();

    //ds creates a new frame living in this instance at the provided pose
    Frame* createFrame(const TransformMatrix3D& robot_to_world_, const real& maximum_depth_near_);

    //! @brief creates a new landmark living in this instance, using the provided framepoint as origin
    //! @param[in] origin_ initial framepoint, describing the landmark
    Landmark* createLandmark(FramePoint* origin_);

    //ds attempts to create a new local map if the generation criteria are met (returns true if a local map was generated)
    const bool createLocalMap(const bool& drop_framepoints_ = false);

    //ds resets the window for the local map generation
    void resetWindowForLocalMapCreation(const bool& drop_framepoints_ = false);

    //! @brief adds a loop closure constraint between 2 local maps
    //! @param[in] query_ query local map
    //! @param[in] reference_ reference local map (fixed, closed against)
    //! @param[in] query_to_reference_ query to reference transform
    //! @param[in] landmark_correspondences_ landmark correspondences of the loop closure
    //! @param[in] information_ information value
    void addCorrespondence(LocalMap* query_,
                           const LocalMap* reference_,
                           const TransformMatrix3D& query_to_reference_,
                           const CorrespondencePointerVector& landmark_correspondences_,
                           const real& information_ = 1);

    //! @brief dump trajectory to file (in KITTI benchmark format: 4x4 isometries per line)
    //! @param[in] filename_ text file in which the poses are saved to
    void writeTrajectory(const std::string& filename_ = "") const;

    //! @brief save trajectory to a vector (in KITTI benchmark format: 4x4 isometries per line)
    //! @param[in,out] poses_ vector with poses, set in the function
    template<typename RealType>
    void writeTrajectory(std::vector<Eigen::Matrix<RealType, 4, 4>>& poses_) const {

      //ds prepare output vector
      poses_.resize(_frames.size());

      //ds add the pose for each frame
      Identifier identifier_frame = 0;
      for (const FramePointerMapElement frame: _frames) {
        poses_[identifier_frame] = frame.second->robotToWorld().matrix().cast<RealType>();
        ++identifier_frame;
      }
    }

    //! @brief this function does what you think it does
    //! @param[in] frame_ frame at which the track was broken
    void breakTrack(const Frame* frame_);

    //! @brief this function does what you think it does
    //! @param[in] frame_ frame at which the track was found again
    void setTrack(Frame* frame_);

  //ds getters/setters
  public:

    const Frame* rootFrame() const {return _root_frame;}
    Frame* currentFrame() const {return _current_frame;}
    void setCurrentFrame(Frame* current_frame_) {_current_frame = current_frame_;}
    const Frame* previousFrame() const {return _previous_frame;}
    void setPreviousFrame(Frame* previous_frame_) {_previous_frame = previous_frame_;}

    LandmarkPointerMap& landmarks() {return _landmarks;}
    const LandmarkPointerMap& landmarks() const {return _landmarks;}
    std::vector<Landmark*>& currentlyTrackedLandmarks() {return _currently_tracked_landmarks;}
    const std::vector<Landmark*>& currentlyTrackedLandmarks() const {return _currently_tracked_landmarks;}

    LocalMap* currentLocalMap() {return _current_local_map;}
    const LocalMapPointerVector& localMaps() const {return _local_maps;}

    void setRobotToWorld(const TransformMatrix3D& robot_pose_) {robot_to_world = robot_pose_;}
    const TransformMatrix3D robotToWorld() const {return robot_to_world;}

    const bool relocalized() const {return _relocalized;}
    const Count numberOfClosures() const {return _number_of_closures;}

    //ds visualization only
    const FramePointerMap& frames() const {return _frames;}
    const FramePointerVector& frameQueueForLocalMap() const {return _frame_queue_for_local_map;}
    void setRobotToWorldGroundTruth(const TransformMatrix3D& robot_to_world_ground_truth_) {if (_current_frame) {_current_frame->setRobotToWorldGroundTruth(robot_to_world_ground_truth_);}}

  //ds helpers
  public:

    //ds obtain angular values from rotation matrix - used for the local map generation criteria in rotation
    static const Vector3 toOrientationRodrigues(const Matrix3& rotation_matrix_) {
      cv::Vec<real, 3> rotation_angles;
      cv::Rodrigues(srrg_core::toCv(rotation_matrix_), rotation_angles);
      return srrg_core::fromCv(rotation_angles);
    }

  protected:

    //ds robot path information
    const Frame* _root_frame = 0;
    Frame* _current_frame    = 0;
    Frame* _previous_frame   = 0;

    //ds all permanent landmarks in the map
    LandmarkPointerMap _landmarks;

    //ds currently tracked landmarks (=visible in the current image)
    std::vector<Landmark*> _currently_tracked_landmarks;

    //ds active frames in the map
    FramePointerMap _frames;

    //ds localization
    TransformMatrix3D robot_to_world = TransformMatrix3D::Identity();
    bool _relocalized = false;

    //ds current frame window buffer for local map generation
    real _distance_traveled_window = 0;
    real _degrees_rotated_window   = 0;

    //ds local map control structures
    FramePointerVector _frame_queue_for_local_map;
    LocalMap* _current_local_map  = 0;
    LocalMapPointerVector _local_maps;

    //ds track recovery
    Frame* _last_frame_before_track_break = 0;
    LocalMap* _last_local_map_before_track_break = 0;
    LocalMap* _root_local_map = 0;

  private:

    //! @brief configurable parameters
    const WorldMapParameters* _parameters;

    //ds informative/visualization only
    Count _number_of_closures = 0;
  };
}
