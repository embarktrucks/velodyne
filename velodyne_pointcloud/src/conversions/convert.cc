/*
 *  Copyright (C) 2009, 2010 Austin Robot Technology, Jack O'Quin
 *  Copyright (C) 2011 Jesse Vera
 *  Copyright (C) 2012 Austin Robot Technology, Jack O'Quin
 *  License: Modified BSD Software License Agreement
 *
 *  $Id$
 */

/** @file

    This class converts raw Velodyne 3D LIDAR packets to PointCloud2.

*/

#include "convert.h"

#include <pcl_conversions/pcl_conversions.h>

namespace velodyne_pointcloud {
/** @brief Constructor. */
Convert::Convert(ros::NodeHandle node, ros::NodeHandle private_nh)
  : data_(new velodyne_rawdata::RawData()), prev_azimuth_(0.0), prev_stamp_(ros::Time())
{
  data_->setup(private_nh);


  // advertise output point cloud (before subscribing to input data)
  output_pointcloud_ = node.advertise<sensor_msgs::PointCloud2>("velodyne_points", 10);

  // advertise output deskew info
  output_deskew_info_ =
      node.advertise<velodyne_msgs::VelodyneDeskewInfo>("velodyne_deskew_info", 10);

  srv_ = boost::make_shared<dynamic_reconfigure::Server<velodyne_pointcloud::CloudNodeConfig> >(
      private_nh);
  dynamic_reconfigure::Server<velodyne_pointcloud::CloudNodeConfig>::CallbackType f;
  f = boost::bind(&Convert::callback, this, _1, _2);
  srv_->setCallback(f);

  // Add an extra entry for angle 0, for initial sweep
  deskew_info_.sweep_info.push_back(create_sweep_entry(prev_stamp_, 0.0));

  // subscribe to VelodyneScan packets
  velodyne_scan_ = node.subscribe("velodyne_packets", 10, &Convert::processScan, (Convert*)this,
                                  ros::TransportHints().tcpNoDelay(true));
}

void Convert::callback(velodyne_pointcloud::CloudNodeConfig& config, uint32_t level)
{
  ROS_INFO("Reconfigure Request");
  data_->setParameters(config.min_range, config.max_range, config.view_direction,
                       config.view_width);
}

velodyne_msgs::VelodyneSweepInfo Convert::create_sweep_entry(ros::Time stamp, float angle)
{
  velodyne_msgs::VelodyneSweepInfo sweep_info;
  sweep_info.stamp = stamp;
  sweep_info.start_angle = angle;
  return sweep_info;
}

/** @brief Callback for raw scan messages. */
void Convert::processScan(const velodyne_msgs::VelodyneScan::ConstPtr& scanMsg)
{
  if (output_pointcloud_.getNumSubscribers() == 0) // no one listening?
    return;                                        // avoid much work

  // allocate a point cloud with same time and frame ID as raw data
  velodyne_rawdata::VPointCloud::Ptr outMsg(new velodyne_rawdata::VPointCloud());

  // outMsg's header is a pcl::PCLHeader, convert it before stamp assignment
  outMsg->header.stamp = pcl_conversions::toPCL(scanMsg->header).stamp;
  outMsg->header.frame_id = scanMsg->header.frame_id;
  outMsg->height = 1;

  // Debug
  // std::cout << "Processing scan!\n";

  // process each packet provided by the driver
  for (size_t i = 0; i < scanMsg->packets.size(); ++i) {
    const velodyne_rawdata::raw_packet_t* raw =
        (const velodyne_rawdata::raw_packet_t*)&scanMsg->packets[i].data[0];

    // azimuth corresponds to the starting sweep angle for the current packet
    float azimuth = float(raw->blocks[0].rotation) / 100.0;

    // azimuth value will wrap around after a full 360 degree sweep
    // once we save all packets for the last full 360 degrees, publish them
    if (azimuth < prev_azimuth_) {
      accumulated_cloud_.header.stamp = outMsg->header.stamp;
      accumulated_cloud_.header.frame_id = outMsg->header.frame_id;
      accumulated_cloud_.height = outMsg->height;
      accumulated_cloud_.width = accumulated_cloud_.points.size();
      output_pointcloud_.publish(accumulated_cloud_);

      deskew_info_.header.stamp = scanMsg->header.stamp;
      deskew_info_.header.frame_id = scanMsg->header.frame_id;
      output_deskew_info_.publish(deskew_info_);

      accumulated_cloud_.points.clear();
      accumulated_cloud_.width = 0;
      deskew_info_.sweep_info.clear();

      // Add an extra entry for angle 0, for next sweep
      deskew_info_.sweep_info.push_back(create_sweep_entry(prev_stamp_, 0.0));
    }

    data_->unpack(scanMsg->packets[i], *outMsg);
    // Accumulate the pt cloud
    accumulated_cloud_.points.insert(accumulated_cloud_.points.end(), outMsg->points.begin(),
                                     outMsg->points.end());

    // Get the sweep info
    deskew_info_.sweep_info.push_back(create_sweep_entry(scanMsg->packets[i].stamp, azimuth));

    prev_azimuth_ = azimuth;
    prev_stamp_ = scanMsg->packets[i].stamp;
  }
}

} // namespace velodyne_pointcloud
