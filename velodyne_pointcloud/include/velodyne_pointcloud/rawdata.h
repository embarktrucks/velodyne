/* -*- mode: C++ -*-
 *
 *  Copyright (C) 2007 Austin Robot Technology, Yaxin Liu, Patrick Beeson
 *  Copyright (C) 2009, 2010, 2012 Austin Robot Technology, Jack O'Quin
 *
 *  License: Modified BSD Software License Agreement
 *
 *  $Id$
 */

/** @file
 *
 *  @brief Interfaces for interpreting raw packets from the Velodyne 3D LIDAR.
 *
 *  @author Yaxin Liu
 *  @author Patrick Beeson
 *  @author Jack O'Quin
 */

#ifndef __VELODYNE_RAWDATA_H
#define __VELODYNE_RAWDATA_H

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <boost/format.hpp>
#include <string>

#include <pcl_ros/point_cloud.h>
#include <ros/ros.h>
#include <velodyne_msgs/VelodyneScan.h>
#include <velodyne_pointcloud/calibration.h>

#include <utils/point_cloud/point_types.h>

namespace velodyne_rawdata {
// Shorthand typedefs for point cloud representations
typedef PointXYZITLaser VPoint;
typedef pcl::PointCloud<VPoint> VPointCloud;

/**
 * Raw Velodyne packet constants and structures.
 */
static const int SIZE_BLOCK = 100;
static const int RAW_SCAN_SIZE = 3;
static const int SCANS_PER_BLOCK = 32;
static const int BLOCK_DATA_SIZE = (SCANS_PER_BLOCK * RAW_SCAN_SIZE);

static const float ROTATION_RESOLUTION = 0.01f;    // [deg]
static const uint16_t ROTATION_MAX_UNITS = 36000u; // [deg/100]
static const float DISTANCE_RESOLUTION = 0.002f;   // [m]

/** @todo make this work for both big and little-endian machines */
static const uint16_t UPPER_BANK = 0xeeff;
static const uint16_t LOWER_BANK = 0xddff;

/** Special defines for VLP support **/
typedef struct vlp_spec
{
  int firing_seqs_per_block;
  int lasers_per_firing_seq;
  int lasers_per_firing;
  float firing_duration;     // [us]
  float firing_seq_duration; // [us]
  float block_duration;      // [us] = firing_seq_duration * firing_seqs_per_block
  float distance_resolution; // [us]
} vlp_spec_t;

static const vlp_spec_t VLP_16_SPEC = { 2, 16, 1, 2.304f, 55.296f, 110.592f, 0.002f };

static const vlp_spec_t VLP_32_SPEC = { 1, 32, 2, 2.304f, 55.296f, 55.296f, 0.004f };

/** \brief Raw Velodyne data block.
 *
 *  Each block contains data from either the upper or lower laser
 *  bank.  The device returns three times as many upper bank blocks.
 *
 *  use stdint.h types, so things work with both 64 and 32-bit machines
 */
typedef struct raw_block
{
  uint16_t header;   ///< UPPER_BANK or LOWER_BANK
  uint16_t rotation; ///< 0-35999, divide by 100 to get degrees
  uint8_t data[BLOCK_DATA_SIZE];
} raw_block_t;

/** used for unpacking the first two data bytes in a block
 *
 *  They are packed into the actual data stream misaligned.  I doubt
 *  this works on big endian machines.
 */
union two_bytes
{
  uint16_t uint;
  uint8_t bytes[2];
};

static const int PACKET_SIZE = 1206;
static const int BLOCKS_PER_PACKET = 12;
static const int PACKET_STATUS_SIZE = 4;
static const int SCANS_PER_PACKET = (SCANS_PER_BLOCK * BLOCKS_PER_PACKET);

/** \brief Raw Velodyne packet.
 *
 *  revolution is described in the device manual as incrementing
 *    (mod 65536) for each physical turn of the device.  Our device
 *    seems to alternate between two different values every third
 *    packet.  One value increases, the other decreases.
 *
 *  \todo figure out if revolution is only present for one of the
 *  two types of status fields
 *
 *  status has either a temperature encoding or the microcode level
 */
typedef struct raw_packet
{
  raw_block_t blocks[BLOCKS_PER_PACKET];
  uint16_t revolution;
  uint8_t status[PACKET_STATUS_SIZE];
} raw_packet_t;

/** \brief Velodyne data conversion class */
class RawData
{
 public:
  RawData();
  ~RawData()
  {
  }

  /** \brief Set up for data processing.
   *
   *  Perform initializations needed before data processing can
   *  begin:
   *
   *    - read device-specific angles calibration
   *
   *  @param private_nh private node handle for ROS parameters
   *  @returns 0 if successful;
   *           errno value for failure
   */
  int setup(ros::NodeHandle private_nh);

  /**
   * Unpack pkt points, filter based on configuration, and add OK points to pc.
   * @param pkt velodyne UDP packet payload (no UDP header)
   * @param pc output pointcloud that we add data to
   * @return azimuth value of the last point in pkt if VLP otherwise -1.0
   */
  float unpackAndAdd(const velodyne_msgs::VelodynePacket& pkt, VPointCloud& pc) const;

  void setParameters(double min_range, double max_range, double view_direction, double view_width);

 private:
  /** configuration parameters */
  typedef struct
  {
    std::string calibrationFile; ///< calibration file name
    std::string deviceModel;     ///< device model name
    double max_range;            ///< maximum range to publish
    double min_range;            ///< minimum range to publish
    int min_angle;               ///< minimum angle to publish
    int max_angle;               ///< maximum angle to publish

    double tmp_min_angle;
    double tmp_max_angle;
  } Config;
  Config config_;

  /**
   * Calibration file
   */
  velodyne_pointcloud::Calibration calibration_;
  vlp_spec_t vlp_spec_;
  bool is_vlp_; // whether or not device model is VLP
  float sin_rot_table_[ROTATION_MAX_UNITS];
  float cos_rot_table_[ROTATION_MAX_UNITS];
  std::vector<std::vector<ros::Duration>> timing_offsets_;

  /** add private function to handle the VLP16 and VLP32 **/
  float unpack_vlp(const velodyne_msgs::VelodynePacket& pkt, VPointCloud& pc) const;

  /** in-line test whether a point is in range */
  bool pointInRange(float range) const
  {
    return (range >= config_.min_range && range <= config_.max_range);
  }

  static std::vector<std::vector<ros::Duration>> getVLP32TimingOffsets();
};

} // namespace velodyne_rawdata

#endif // __VELODYNE_RAWDATA_H
