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
#include <stepback_recovery/stepback_recovery.h>
#include <pluginlib/class_list_macros.h>

//register this planner as a RecoveryBehavior plugin
PLUGINLIB_EXPORT_CLASS(stepback_recovery::StepBackRecovery, nav_core::RecoveryBehavior)

namespace stepback_recovery {


StepBackRecovery::StepBackRecovery(): global_costmap_(NULL), local_costmap_(NULL),
  initialized_(false), world_model_(NULL) {} 

// initialize
//------------
void StepBackRecovery::initialize(std::string name, tf2_ros::Buffer* tf,
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

    //we'll simulate every degree by default
    private_nh.param("setpback_speed", stepback_speed, -0.1);
    private_nh.param("stepback_time", stepback_time, 0.8);

    blp_nh.param("acc_lim_th", acc_lim_th_, 3.2);
    blp_nh.param("max_rotational_vel", max_rotational_vel_, 1.0);
    blp_nh.param("min_in_place_rotational_vel", min_rotational_vel_, 0.4);
    blp_nh.param("yaw_goal_tolerance", tolerance_, 0.10);

    world_model_ = new base_local_planner::CostmapModel(*local_costmap_->getCostmap());

    initialized_ = true;
  }
  else{
    ROS_ERROR("[StepBack_Recovery] You should not call initialize twice on this object, doing nothing");
  }
}


StepBackRecovery::~StepBackRecovery(){
  delete world_model_;
}

// EXECUTION OF THE RECOVERY (see planner_common_params.yaml to see the order of recovery behaviours)
//---------------------------
void StepBackRecovery::runBehavior()
{
    if(!initialized_){
        ROS_ERROR("[StepBack_Recovery] This object must be initialized before runBehavior is called");
        return;
    }

    if(global_costmap_ == NULL || local_costmap_ == NULL){
        ROS_ERROR("[StepBack_Recovery] The costmaps passed to the StepBackRecovery object cannot be NULL. Doing nothing.");
        return;
    }
    ROS_INFO("[StepBack_Recovery] StepBack recovery behavior started.");

    ros::Rate r(10);
    ros::NodeHandle n;
    ros::Publisher vel_pub = n.advertise<geometry_msgs::Twist>("cmd_vel", 10);

    // Just move back
    ros::Time start_time = ros::Time::now();

    while ( n.ok() && (ros::Time::now()-start_time) < ros::Duration(stepback_time) )
    {
        // Set speed
        geometry_msgs::Twist cmd_vel;
        cmd_vel.linear.x = stepback_speed;
        cmd_vel.linear.y = 0.0;
        cmd_vel.angular.z = 0.0;
        vel_pub.publish(cmd_vel);

        r.sleep();
    }

  /*
  tf::Stamped<tf::Pose> global_pose;
  local_costmap_->getRobotPose(global_pose);

  double current_angle = -1.0 * M_PI;
  bool got_180 = false;

  double start_offset = 0 - angles::normalize_angle(tf::getYaw(global_pose.getRotation()));

  while(n.ok())
  {
    local_costmap_->getRobotPose(global_pose);

    double norm_angle = angles::normalize_angle(tf::getYaw(global_pose.getRotation()));
    current_angle = angles::normalize_angle(norm_angle + start_offset);

    //compute the distance left to rotate
    double dist_left = M_PI - current_angle;

    double x = global_pose.getOrigin().x(), y = global_pose.getOrigin().y();

    //check if that velocity is legal by forward simulating
    double sim_angle = 0.0;
    while(sim_angle < dist_left){
      double theta = tf::getYaw(global_pose.getRotation()) + sim_angle;

      //make sure that the point is legal, if it isn't... we'll abort
      double footprint_cost = world_model_->footprintCost(x, y, theta, local_costmap_->getRobotFootprint(), 0.0, 0.0);
      if(footprint_cost < 0.0){
        ROS_ERROR("StepBack recovery can't rotate in place because there is a potential collision. Cost: %.2f", footprint_cost);
        return;
      }

      sim_angle += sim_granularity_;
    }

    //compute the velocity that will let us stop by the time we reach the goal
    double vel = sqrt(2 * acc_lim_th_ * dist_left);

    //make sure that this velocity falls within the specified limits
    vel = std::min(std::max(vel, min_rotational_vel_), max_rotational_vel_);

    geometry_msgs::Twist cmd_vel;
    cmd_vel.linear.x = 0.0;
    cmd_vel.linear.y = 0.0;
    cmd_vel.angular.z = vel;

    vel_pub.publish(cmd_vel);

    //makes sure that we won't decide we're done right after we start
    if(current_angle < 0.0)
      got_180 = true;

    //if we're done with our in-place rotation... then return
    if(got_180 && current_angle >= (0.0 - tolerance_))
      return;

    r.sleep();
  }
  */
}

};

