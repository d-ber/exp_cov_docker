/*********************************************************************
*
* Software License Agreement (BSD License)
*
*  Copyright (c) 2009, Willow Garage, Inc.
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of Willow Garage, Inc. nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*
* Author: Javier Monroy
*********************************************************************/
#include <rotate_left_recovery/rotate_left_recovery.h>
#include <pluginlib/class_list_macros.hpp>
#include <nav_core/parameter_magic.h>
#include <tf2/utils.h>
#include <ros/ros.h>
#include <geometry_msgs/Twist.h>
#include <geometry_msgs/Point.h>
#include <angles/angles.h>
#include <algorithm>
#include <string>

//register this planner as a RecoveryBehavior plugin
PLUGINLIB_EXPORT_CLASS(rotate_left_recovery::RotateLeftRecovery, nav_core::RecoveryBehavior)

namespace rotate_left_recovery {


RotateLeftRecovery::RotateLeftRecovery(): global_costmap_(NULL), local_costmap_(NULL),
  initialized_(false), world_model_(NULL) {} 

// initialize
//------------
void RotateLeftRecovery::initialize(std::string name, tf2_ros::Buffer* tf,
costmap_2d::Costmap2DROS* global_costmap, costmap_2d::Costmap2DROS* local_costmap)
{
  if(!initialized_)
  {
    name_ = name;
    //tf_ = tf;
    global_costmap_ = global_costmap;
    local_costmap_ = local_costmap;

    //get some parameters from the parameter server
    ros::NodeHandle private_nh("~/" + name_);
    ros::NodeHandle blp_nh("~/TrajectoryPlannerROS");

    private_nh.param("rotate_speed", rotate_speed, -1.0);
    private_nh.param("rotate_time", rotate_time, 1.5);

    // we'll simulate every degree by default
    private_nh.param("sim_granularity", sim_granularity_, 0.017);
    private_nh.param("frequency", frequency_, 20.0);

    blp_nh.param("acc_lim_th", acc_lim_th_, 3.2);
    blp_nh.param("max_rotational_vel", max_rotational_vel_, 1.0);
    blp_nh.param("min_in_place_rotational_vel", min_rotational_vel_, 0.4);
    blp_nh.param("yaw_goal_tolerance", tolerance_, 0.10);

    world_model_ = new base_local_planner::CostmapModel(*local_costmap_->getCostmap());

    initialized_ = true;
  }
  else{
    ROS_ERROR("You should not call initialize twice on this object, doing nothing");
  }
}


RotateLeftRecovery::~RotateLeftRecovery(){
  delete world_model_;
}

// EXECUTION OF THE RECOVERY (see planner_common_params.yaml to see the order of recovery behaviours)
//---------------------------
void RotateLeftRecovery::runBehavior()
{
    if(!initialized_){
        ROS_ERROR("This object must be initialized before runBehavior is called");
        return;
    }

    if(global_costmap_ == NULL || local_costmap_ == NULL){
        ROS_ERROR("The costmaps passed to the RotateLeftRecovery object cannot be NULL. Doing nothing.");
        return;
    }
    ROS_WARN("Rotate left recovery behavior started.");

    ros::Rate r(frequency_);
    ros::NodeHandle n;
    ros::Publisher vel_pub = n.advertise<geometry_msgs::Twist>("cmd_vel", 10);

    geometry_msgs::PoseStamped global_pose;
    local_costmap_->getRobotPose(global_pose);

    double current_angle = tf2::getYaw(global_pose.pose.orientation);
    double start_angle = current_angle;

    bool got_45 = false;

    while (n.ok() &&
          (!got_45 ||
            std::fabs(angles::shortest_angular_distance(current_angle, start_angle + M_PI/2)) > tolerance_))
    {
      // Update Current Angle
      local_costmap_->getRobotPose(global_pose);
      current_angle = tf2::getYaw(global_pose.pose.orientation);

      // compute the distance left to rotate
      double dist_left;
      if (!got_45)
      {
        // If we haven't hit 45 yet, we need to rotate a half circle plus the distance to the 45 point
        double distance_to_45 = std::fabs(angles::shortest_angular_distance(current_angle, start_angle + M_PI/4));
        dist_left = M_PI/4 + distance_to_45;
        //ROS_ERROR("distance_to_45: %.2f", distance_to_45);

        if (distance_to_45 < tolerance_)
        {
          got_45 = true;
        }
      }
      else
      {
        // If we have hit the 45, we just have the distance back to the start
        dist_left = std::fabs(angles::shortest_angular_distance(current_angle, start_angle + M_PI/2));
      }
      //ROS_ERROR("dist_left: %.2f", dist_left);
      //ROS_ERROR("current_angle: %.2f", current_angle);
      //ROS_ERROR("start_angle: %.2f", start_angle);
      //ROS_ERROR("got45: %d", got_45);
      //ROS_ERROR("------------------");

      double x = global_pose.pose.position.x, y = global_pose.pose.position.y;

      // check if that velocity is legal by forward simulating
      double sim_angle = 0.0;
      while (sim_angle < dist_left)
      {
        double theta = current_angle + sim_angle;

        // make sure that the point is legal, if it isn't... we'll abort
        double footprint_cost = world_model_->footprintCost(x, y, theta, local_costmap_->getRobotFootprint(), 0.0, 0.0);
        if (footprint_cost < 0.0)
        {
          ROS_ERROR("Rotate left recovery can't rotate in place because there is a potential collision. Cost: %.2f",
                    footprint_cost);
          return;
        }

        sim_angle += sim_granularity_;
      }

      // compute the velocity that will let us stop by the time we reach the goal
      double vel = sqrt(2 * acc_lim_th_ * dist_left);

      // make sure that this velocity falls within the specified limits
      vel = std::min(std::max(vel, min_rotational_vel_), max_rotational_vel_);

      geometry_msgs::Twist cmd_vel;
      cmd_vel.linear.x = 0.0;
      cmd_vel.linear.y = 0.0;
      cmd_vel.angular.z = vel;

      vel_pub.publish(cmd_vel);

      r.sleep();
    }
}

};

