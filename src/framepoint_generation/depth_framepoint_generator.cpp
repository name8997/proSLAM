#include "depth_framepoint_generator.h"

namespace proslam {

DepthFramePointGenerator::DepthFramePointGenerator(DepthFramePointGeneratorParameters* parameters_): BaseFramePointGenerator(parameters_),
                                                                                                     _parameters(parameters_) {
  LOG_INFO(std::cerr << "DepthFramePointGenerator::DepthFramePointGenerator|constructed" << std::endl)
}

//ds the stereo camera setup must be provided
void DepthFramePointGenerator::configure(){
  LOG_INFO(std::cerr << "DepthFramePointGenerator::configure|configuring" << std::endl)
  assert(_camera_right);

  //ds update base
  BaseFramePointGenerator::configure();

  //ds info
  LOG_INFO(std::cerr << "DepthFramePointGenerator::configure|configured" << std::endl)
}

//ds cleanup of dynamic structures
DepthFramePointGenerator::~DepthFramePointGenerator() {
  LOG_INFO(std::cerr << "DepthFramePointGenerator::~DepthFramePointGenerator|destroyed" << std::endl)
}

void DepthFramePointGenerator::initialize(Frame* frame_, const bool& extract_features_) {
  CHRONOMETER_START(depth_map_generation)
  _computeDepthMap(frame_->intensityImageRight());
  CHRONOMETER_STOP(depth_map_generation)

  //ds detect new features
  detectKeypoints(frame_->intensityImageLeft(), frame_->keypointsLeft());

  //ds adjust detector thresholds for next frame
  adjustDetectorThresholds();

  //ds extract descriptors for detected features
  computeDescriptors(frame_->intensityImageLeft(), frame_->keypointsLeft(), frame_->descriptorsLeft());

  //ds initialize matchers for left frame
  _feature_matcher_left.setFeatures(frame_->keypointsLeft(), frame_->descriptorsLeft());
}

//ds computes framepoints stored in a image-like matrix (_framepoints_in_image) for provided stereo images
void DepthFramePointGenerator::compute(Frame* frame_) {
  assert(frame_->intensityImageRight().type() == CV_16UC1);
  if (!frame_) {
    throw std::runtime_error("DepthFramePointGenerator::compute|called with empty frame");
  }
  FramePointPointerVector& framepoints(frame_->points());
  const Count number_of_points_tracked = framepoints.size();

  //ds store already present points for optional binning
  if (_parameters->enable_keypoint_binning) {
    for (FramePoint* point: frame_->points()) {
      const Index row_bin = std::rint(static_cast<real>(point->row)/_parameters->bin_size_pixels);
      const Index col_bin = std::rint(static_cast<real>(point->col)/_parameters->bin_size_pixels);
      _bin_map_left[row_bin][col_bin] = point;
    }
  }

  //ds prepare for fast stereo matching
  IntensityFeaturePointerVector& features_left(_feature_matcher_left.feature_vector);

  //ds new framepoints - optionally filtered in a consecutive binning
  FramePointPointerVector framepoints_new(features_left.size());
  Count number_of_new_points = 0;

  //ds compute depth for all remaining features
  for (const IntensityFeature* feature_left: features_left) {

    //ds retrieve depth point at given pixel
    const cv::Vec3f& depth_point = _space_map_left_meters.at<const cv::Vec3f>(feature_left->row, feature_left->col);

    //ds skip if above maximum depth
    if (depth_point[2] >= _parameters->maximum_depth_meters) {
      continue;
    }

    //ds skip if below minimum depth
    if (depth_point[2] < _parameters->minimum_depth_meters) {
      continue;
    }

    //ds obtain coordinates in the depth image
    cv::KeyPoint keypoint_right = feature_left->keypoint;
    keypoint_right.pt.x = _col_map.at<short>(feature_left->row, feature_left->col);
    keypoint_right.pt.y = _row_map.at<short>(feature_left->row, feature_left->col);

    //ds add to framepoint map
    FramePoint* framepoint = frame_->createFramepoint(feature_left->keypoint,
                                                      feature_left->descriptor,
                                                      keypoint_right,
                                                      feature_left->descriptor,
                                                      PointCoordinates(depth_point[0], depth_point[1], depth_point[2]));

    //ds store point for optional binning
    if (_parameters->enable_keypoint_binning) {
      const Index row_bin = std::rint(static_cast<real>(feature_left->row)/_parameters->bin_size_pixels);
      const Index col_bin = std::rint(static_cast<real>(feature_left->col)/_parameters->bin_size_pixels);

      //ds if there is already a point in the bin
      if (_bin_map_left[row_bin][col_bin]) {

        //ds if the point in the bin is not tracked, we prefer points with lower depth
        if (!_bin_map_left[row_bin][col_bin]->previous() &&
            framepoint->depthMeters() < _bin_map_left[row_bin][col_bin]->depthMeters()) {

          //ds overwrite the entry
          _bin_map_left[row_bin][col_bin] = framepoint;
        }
      } else {

        //ds add a new entry
        _bin_map_left[row_bin][col_bin] = framepoint;
      }
    }

    //ds set point to buffer
    framepoints_new[number_of_new_points] = framepoint;
    ++number_of_new_points;
  }
  framepoints_new.resize(number_of_new_points);
  LOG_DEBUG(std::cerr << "DepthFramePointGenerator::compute|number of new points: " << number_of_new_points << std::endl)

  //ds update framepoints - optionally binning them
  if (_parameters->enable_keypoint_binning) {

    //ds reserve space for the best case (all points can be added)
    Count number_of_points_binned = number_of_points_tracked;
    framepoints.resize(number_of_points_tracked+number_of_new_points);

    //ds accumulate new points over bin grid
    for (Index row = 0; row < _number_of_rows_bin; ++row) {
      for (Index col = 0; col < _number_of_cols_bin; ++col) {
        if (_bin_map_left[row][col] && !_bin_map_left[row][col]->previous()) {
          framepoints[number_of_points_binned] = _bin_map_left[row][col];
          ++number_of_points_binned;
        }
        _bin_map_left[row][col] = nullptr;
      }
    }
    framepoints.resize(number_of_points_binned);
    LOG_DEBUG(std::cerr << "DepthFramePointGenerator::compute|number of new points binned: " << number_of_points_binned-number_of_points_tracked << std::endl)
  } else {

    //ds add all points to frame
    framepoints.insert(framepoints.end(), framepoints_new.begin(), framepoints_new.end());
  }
}

void DepthFramePointGenerator::track(Frame* frame_,
                                     Frame* frame_previous_,
                                     const TransformMatrix3D& camera_left_previous_in_current_,
                                     FramePointPointerVector& previous_framepoints_without_tracks_,
                                     const bool track_by_appearance_) {
  if (!frame_ || !frame_previous_) {
    throw std::runtime_error("DepthFramePointGenerator::track|called with invalid frames");
  }
  frame_->clear();
  const Matrix3& camera_calibration_matrix = _camera_left->cameraMatrix();
  FramePointPointerVector& framepoints(frame_->points());
  FramePointPointerVector& framepoints_previous(frame_previous_->points());

  //ds allocate space, existing framepoints will be overwritten!
  framepoints.resize(framepoints_previous.size());

  //ds store points for which we couldn't find a track candidate
  previous_framepoints_without_tracks_.resize(framepoints_previous.size());

  //ds tracked and triangulated features (to not consider them in the exhaustive stereo matching)
  std::set<uint32_t> matched_indices_left;
  Count number_of_points       = 0;
  Count number_of_points_lost  = 0;
  _number_of_tracked_landmarks = 0;

  //ds for each previous point
  for (FramePoint* point_previous: framepoints_previous) {

    //ds transform the point into the current camera frame
    const Vector3 point_in_camera_left_prediction(camera_left_previous_in_current_*point_previous->cameraCoordinatesLeft());

    //ds project the point into the current left image plane
    const Vector3 point_in_image_left(camera_calibration_matrix*point_in_camera_left_prediction);
    const int32_t col_projection_left = point_in_image_left.x()/point_in_image_left.z();
    const int32_t row_projection_left = point_in_image_left.y()/point_in_image_left.z();

    //ds skip point if not in image plane
    if (col_projection_left < 0 || col_projection_left > _number_of_cols_image ||
        row_projection_left < 0 || row_projection_left > _number_of_rows_image) {
      continue;
    }

    //ds TRACKING obtain matching feature in left image (if any)
    real descriptor_distance_best = _parameters->matching_distance_tracking_threshold;

    //ds define search region (rectangular ROI)
    int32_t row_start_point = std::max(row_projection_left-_projection_tracking_distance_pixels, 0);
    int32_t row_end_point   = std::min(row_projection_left+_projection_tracking_distance_pixels+1, _number_of_rows_image);
    int32_t col_start_point = std::max(col_projection_left-_projection_tracking_distance_pixels, 0);
    int32_t col_end_point   = std::min(col_projection_left+_projection_tracking_distance_pixels+1, _number_of_cols_image);

    //ds find the best match for the previous left feature (i.e. track it)
    IntensityFeature* feature_left = _feature_matcher_left.getMatchingFeatureInRectangularRegion(row_projection_left,
                                                                                                 col_projection_left,
                                                                                                 point_previous->descriptorLeft(),
                                                                                                 row_start_point,
                                                                                                 row_end_point,
                                                                                                 col_start_point,
                                                                                                 col_end_point,
                                                                                                 _parameters->matching_distance_tracking_threshold,
                                                                                                 track_by_appearance_,
                                                                                                 descriptor_distance_best);

    //ds if we found a match
    if (feature_left) {

      //ds retrieve depth point at given pixel
      const cv::Vec3f& depth_point = _space_map_left_meters.at<const cv::Vec3f>(feature_left->row, feature_left->col);

      //ds skip if above maximum reliable depth
      if (depth_point[2] >= _parameters->maximum_depth_meters) {
        continue;
      }

      //ds skip if below minimum depth
      if (depth_point[2] < _parameters->minimum_depth_meters) {
        continue;
      }

      //ds obtain coordinates in the depth image
      cv::KeyPoint keypoint_right = feature_left->keypoint;
      keypoint_right.pt.x = _col_map.at<short>(feature_left->row, feature_left->col);
      keypoint_right.pt.y = _row_map.at<short>(feature_left->row, feature_left->col);

      //ds add to framepoint map
      FramePoint* framepoint = frame_->createFramepoint(feature_left->keypoint,
                                                        feature_left->descriptor,
                                                        keypoint_right,
                                                        feature_left->descriptor,
                                                        PointCoordinates(depth_point[0], depth_point[1], depth_point[2]),
                                                        point_previous);

      //ds VSUALIZATION ONLY
      framepoint->setProjectionEstimateLeft(cv::Point2f(col_projection_left, row_projection_left));

      //ds store and move to next slot
      framepoints[number_of_points] = framepoint;
      ++number_of_points;

      //ds block matching in exhaustive matching (later)
      matched_indices_left.insert(feature_left->index_in_vector);

      //ds remove feature from lattices
      _feature_matcher_left.feature_lattice[feature_left->row][feature_left->col]    = nullptr;

      if (framepoint->landmark()) {
        ++_number_of_tracked_landmarks;
      }
    }

    //ds if we couldn't track the point
    if (!point_previous->next()) {
      previous_framepoints_without_tracks_[number_of_points_lost] = point_previous;
      ++number_of_points_lost;
    }
  }
  framepoints.resize(number_of_points);
  previous_framepoints_without_tracks_.resize(number_of_points_lost);

  //ds remove matched indices from candidate pools
  _feature_matcher_left.prune(matched_indices_left);
  LOG_DEBUG(std::cerr << "DepthFramePointGenerator::track|tracked points with depth: " << number_of_points
                      << "/" << framepoints_previous.size() << " (landmarks: " << _number_of_tracked_landmarks << ")" << std::endl)
  LOG_DEBUG(std::cerr << "DepthFramePointGenerator::track|lost points: " << number_of_points_lost
                      << "/" << framepoints_previous.size() << std::endl)
}

void DepthFramePointGenerator::_computeDepthMap(const cv::Mat& right_depth_image) {
  if (right_depth_image.type()!=CV_16UC1){
    throw std::runtime_error("depth tracker requires a 16bit mono image to encode depth");
  }

  //ds allocate new space map and initialize fields with invalid maximum depth
  _space_map_left_meters.create(_number_of_rows_image,_number_of_cols_image, CV_32FC3);
  for (int32_t row = 0; row < _number_of_rows_image; ++row) {
    for (int32_t col = 0; col < _number_of_cols_image; ++col) {
      _space_map_left_meters.at<cv::Vec3f>(row, col) = cv::Vec3f(0, 0, _parameters->maximum_depth_meters);
    }
  }

  _row_map.create(_number_of_rows_image,_number_of_cols_image, CV_16SC1);
  _row_map=-1;

  _col_map.create(_number_of_rows_image,_number_of_cols_image, CV_16SC1);
  _col_map=-1;

  const Matrix3 inverse_camera_matrix_right = _camera_right->cameraMatrix().inverse();
  const Matrix3 camera_matrix_left          = _camera_left->cameraMatrix();
  const real _depth_pixel_to_meters         = 1e-3;

  TransformMatrix3D right_to_left_transform = _camera_left->robotToCamera()*_camera_right->cameraToRobot();
  for (int32_t r=0; r<_number_of_rows_image; ++r){
    const unsigned short* raw_depth=right_depth_image.ptr<const unsigned short>(r);
    for (int32_t c=0; c<_number_of_cols_image; ++c, raw_depth++){
      if (!*raw_depth)
        continue;
      // retrieve depth
      const real depth_right_meters=(*raw_depth)*_depth_pixel_to_meters;
      // retrieve point in right camers, meters
      Vector3 point_in_right_camera_meters=inverse_camera_matrix_right*Vector3(c*depth_right_meters, r*depth_right_meters,depth_right_meters);
      // map the point to the left camera
      Vector3 point_in_left_camera_meters=right_to_left_transform*point_in_right_camera_meters;
      // if beyond camera, discard
      const real depth_left_meters=point_in_left_camera_meters.z();
      if (depth_left_meters<=0)
        continue;
      // project to image coordinates
      Vector3 point_in_left_camera_pixels = camera_matrix_left*point_in_left_camera_meters;
      point_in_left_camera_pixels /= point_in_left_camera_pixels.z();

      // round to int
      const int32_t dest_r=round(point_in_left_camera_pixels.y());
      const int32_t dest_c=round(point_in_left_camera_pixels.x());

      // if outside skip
      if (dest_r<0 ||
          dest_r>=_number_of_rows_image ||
          dest_c<0 ||
          dest_c>=_number_of_cols_image)
        continue;

      // do z buffering and update indices
      cv::Vec3f& dest_space=_space_map_left_meters.at<cv::Vec3f>(dest_r, dest_c);

      if (dest_space[2] > depth_left_meters){
        dest_space=cv::Vec3f(point_in_left_camera_meters.x(),
                             point_in_left_camera_meters.y(),
                             point_in_left_camera_meters.z());
        _row_map.at<short>(dest_r, dest_c)=r;
        _col_map.at<short>(dest_r, dest_c)=c;
      }
    }
  }
}
}
