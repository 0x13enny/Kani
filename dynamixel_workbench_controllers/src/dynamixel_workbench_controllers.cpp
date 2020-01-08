/*******************************************************************************
* Copyright 2018 ROBOTIS CO., LTD.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

/* Authors: Taehun Lim (Darby) */
#define _USEMATH_DEFINES
#include "dynamixel_workbench_controllers/dynamixel_workbench_controllers.h"
#include <math.h>
#include <vector>
#include "tf/tf.h"
#include <tf/transform_broadcaster.h>
#include "nav_msgs/Odometry.h"
#include "geometry_msgs/Twist.h"
#include "geometry_msgs/Pose.h"
#include "geometry_msgs/PoseStamped.h"


#define PI 3.14159265


std::vector<double> current_pose (3); 

DynamixelController::DynamixelController()
  :node_handle_(""),
   priv_node_handle_("~"),
   is_joint_state_topic_(false),
   is_cmd_vel_topic_(false),
   use_moveit_(false),
   wheel_separation_x_(0.0f),
   wheel_separation_y_(0.0f),
   wheel_radius_(0.0f),
   is_moving_(false)
   // odom_frame(std::string("odom")),
   // base_link_frame(std::string("base_footprint"))
{
  is_joint_state_topic_ = priv_node_handle_.param<bool>("use_joint_states_topic", true);
  is_cmd_vel_topic_ = priv_node_handle_.param<bool>("use_cmd_vel_topic", false);
  use_moveit_ = priv_node_handle_.param<bool>("use_moveit", false);

  read_period_ = priv_node_handle_.param<double>("dxl_read_period", 0.010f);
  write_period_ = priv_node_handle_.param<double>("dxl_write_period", 0.010f);
  pub_period_ = priv_node_handle_.param<double>("publish_period", 0.010f);

  // Get frame_ids to use.
  frame_id_odom = priv_node_handle_.param<std::string>("odom_frame", std::string("odom"));
  frame_id_base_link = priv_node_handle_.param<std::string>("base_link_frame", std::string("base_footprint"));

  if (is_cmd_vel_topic_ == true)
  {
    wheel_separation_x_ = priv_node_handle_.param<double>("mobile_robot_config/seperation_between_wheels_x", 0.0);
    wheel_separation_y_ = priv_node_handle_.param<double>("mobile_robot_config/seperation_between_wheels_y", 0.0);
    wheel_radius_ = priv_node_handle_.param<double>("mobile_robot_config/radius_of_wheel", 0.0);
  }

  dxl_wb_ = new DynamixelWorkbench;
  jnt_tra_ = new JointTrajectory;
  jnt_tra_msg_ = new trajectory_msgs::JointTrajectory;

  // world coordinates
  // current_pose(3);
  // x, y, orientation


  // relative coordinates
  // std::vector<double> FR_vec[0, 0]; // x, y
  // std::vector<double> FL_vec[0, 0];
  // std::vector<double> RR_vec[0, 0];
  // std::vector<double> RL_vec[0, 0];
  ros::Time last_time_ = ros::Time::now();
  ros::Time current_time_ = ros::Time::now();

  current_pose.at(0) = 0;

  current_pose.at(1) = 0;
  current_pose.at(2) = 0;}

DynamixelController::~DynamixelController(){}

bool DynamixelController::initWorkbench(const std::string port_name, const uint32_t baud_rate)
{
  bool result = false;
  const char* log;

  result = dxl_wb_->init(port_name.c_str(), baud_rate, &log);
  if (result == false)
  {
    ROS_ERROR("%s", log);
  }

  return result;
}

bool DynamixelController::getDynamixelsInfo(const std::string yaml_file)
{
  YAML::Node dynamixel;
  dynamixel = YAML::LoadFile(yaml_file.c_str());

  if (dynamixel == NULL)
    return false;

  for (YAML::const_iterator it_file = dynamixel.begin(); it_file != dynamixel.end(); it_file++)
  {
    std::string name = it_file->first.as<std::string>();
    if (name.size() == 0)
    {
      continue;
    }

    YAML::Node item = dynamixel[name];
    for (YAML::const_iterator it_item = item.begin(); it_item != item.end(); it_item++)
    {
      std::string item_name = it_item->first.as<std::string>();
      int32_t value = it_item->second.as<int32_t>();

      if (item_name == "ID")
        dynamixel_[name] = value;

      ItemValue item_value = {item_name, value};
      std::pair<std::string, ItemValue> info(name, item_value);

      dynamixel_info_.push_back(info);
    }
  }

  return true;
}

bool DynamixelController::loadDynamixels(void)
{
  bool result = false;
  const char* log;

  for (auto const& dxl:dynamixel_)
  {
    uint16_t model_number = 0;
    result = dxl_wb_->ping((uint8_t)dxl.second, &model_number, &log);
    if (result == false)
    {
      ROS_ERROR("%s", log);
      ROS_ERROR("Can't find Dynamixel ID '%d'", dxl.second);
      return result;
    }
    else
    {      
      ROS_INFO("Name : %s, ID : %d, Model Number : %d", dxl.first.c_str(), dxl.second, model_number);
    }
  }

  return result;
}

bool DynamixelController::initDynamixels(void)
{
  const char* log;

  for (auto const& dxl:dynamixel_)
  {
    dxl_wb_->torqueOff((uint8_t)dxl.second);

    for (auto const& info:dynamixel_info_)
    {
      if (dxl.first == info.first)
      {
        if (info.second.item_name != "ID" && info.second.item_name != "Baud_Rate")
        {
          bool result = dxl_wb_->itemWrite((uint8_t)dxl.second, info.second.item_name.c_str(), info.second.value, &log);
          if (result == false)
          {
            ROS_ERROR("%s", log);
            ROS_ERROR("Failed to write value[%d] on items[%s] to Dynamixel[Name : %s, ID : %d]", info.second.value, info.second.item_name.c_str(), dxl.first.c_str(), dxl.second);
            return false;
          }
        }
      }
    }

    dxl_wb_->torqueOn((uint8_t)dxl.second);
  }

  return true;
}

bool DynamixelController::initControlItems(void)
{
  bool result = false;
  const char* log = NULL;

  auto it = dynamixel_.begin();

  const ControlItem *goal_position = dxl_wb_->getItemInfo(it->second, "Goal_Position");
  if (goal_position == NULL) return false;

  const ControlItem *goal_velocity = dxl_wb_->getItemInfo(it->second, "Goal_Velocity");
  if (goal_velocity == NULL)  goal_velocity = dxl_wb_->getItemInfo(it->second, "Moving_Speed");
  if (goal_velocity == NULL)  return false;

  const ControlItem *present_position = dxl_wb_->getItemInfo(it->second, "Present_Position");
  if (present_position == NULL) return false;

  const ControlItem *present_velocity = dxl_wb_->getItemInfo(it->second, "Present_Velocity");
  if (present_velocity == NULL)  present_velocity = dxl_wb_->getItemInfo(it->second, "Present_Speed");
  if (present_velocity == NULL) return false;

  const ControlItem *present_current = dxl_wb_->getItemInfo(it->second, "Present_Current");
  if (present_current == NULL)  present_current = dxl_wb_->getItemInfo(it->second, "Present_Load");
  if (present_current == NULL) return false;

  control_items_["Goal_Position"] = goal_position;
  control_items_["Goal_Velocity"] = goal_velocity;

  control_items_["Present_Position"] = present_position;
  control_items_["Present_Velocity"] = present_velocity;
  control_items_["Present_Current"] = present_current;

  return true;
}

bool DynamixelController::initSDKHandlers(void)
{
  bool result = false;
  const char* log = NULL;

  auto it = dynamixel_.begin();

  result = dxl_wb_->addSyncWriteHandler(control_items_["Goal_Position"]->address, control_items_["Goal_Position"]->data_length, &log);
  if (result == false)
  {
    ROS_ERROR("%s", log);
    return result;
  }
  else
  {
    ROS_INFO("%s", log);
  }

  result = dxl_wb_->addSyncWriteHandler(control_items_["Goal_Velocity"]->address, control_items_["Goal_Velocity"]->data_length, &log);
  if (result == false)
  {
    ROS_ERROR("%s", log);
    return result;
  }
  else
  {
    ROS_INFO("%s", log);
  }

  if (dxl_wb_->getProtocolVersion() == 2.0f)
  {  
    uint16_t start_address = std::min(control_items_["Present_Position"]->address, control_items_["Present_Current"]->address);

    /* 
      As some models have an empty space between Present_Velocity and Present Current, read_length is modified as below.
    */    
    // uint16_t read_length = control_items_["Present_Position"]->data_length + control_items_["Present_Velocity"]->data_length + control_items_["Present_Current"]->data_length;
    uint16_t read_length = control_items_["Present_Position"]->data_length + control_items_["Present_Velocity"]->data_length + control_items_["Present_Current"]->data_length+2;

    result = dxl_wb_->addSyncReadHandler(start_address,
                                          read_length,
                                          &log);
    if (result == false)
    {
      ROS_ERROR("%s", log);
      return result;
    }
   
  }

  return result;
}

bool DynamixelController::getPresentPosition(std::vector<std::string> dxl_name)
{
  bool result = false;
  const char* log = NULL;

  int32_t get_position[dxl_name.size()];

  uint8_t id_array[dxl_name.size()];
  uint8_t id_cnt = 0;

  for (auto const& name:dxl_name)
  {
    id_array[id_cnt++] = dynamixel_[name];
  }

  if (dxl_wb_->getProtocolVersion() == 2.0f)
  {
    result = dxl_wb_->syncRead(SYNC_READ_HANDLER_FOR_PRESENT_POSITION_VELOCITY_CURRENT,
                               id_array,
                               dxl_name.size(),
                               &log);
    if (result == false)
    {
      ROS_ERROR("%s", log);
    }

    WayPoint wp;

    result = dxl_wb_->getSyncReadData(SYNC_READ_HANDLER_FOR_PRESENT_POSITION_VELOCITY_CURRENT,
                                      id_array,
                                      id_cnt,
                                      control_items_["Present_Position"]->address,
                                      control_items_["Present_Position"]->data_length,
                                      get_position,
                                      &log);

    if (result == false)
    {
      ROS_ERROR("%s", log);
    }
    else
    {
      for(uint8_t index = 0; index < id_cnt; index++)
      {
        wp.position = dxl_wb_->convertValue2Radian(id_array[index], get_position[index]);
        pre_goal_.push_back(wp);
      }
    }
  }
  else if (dxl_wb_->getProtocolVersion() == 1.0f)
  {
    WayPoint wp;
    uint32_t read_position;
    for (auto const& dxl:dynamixel_)
    {
      result = dxl_wb_->readRegister((uint8_t)dxl.second,
                                     control_items_["Present_Position"]->address,
                                     control_items_["Present_Position"]->data_length,
                                     &read_position,
                                     &log);
      if (result == false)
      {
        ROS_ERROR("%s", log);
      }

      wp.position = dxl_wb_->convertValue2Radian((uint8_t)dxl.second, read_position);
      pre_goal_.push_back(wp);
    }
  }

  return result;
}

void DynamixelController::initPublisher()
{
  dynamixel_state_list_pub_ = priv_node_handle_.advertise<dynamixel_workbench_msgs::DynamixelStateList>("dynamixel_state", 100);
  
  //for odom->base_link transform
  tf::TransformFloatData odom_broadcaster; //TransformBroadcaster -> TransformFloatData
  geometry_msgs::TransformStamped odom_trans;


  // std::string frame_id_odom;
  // std::string frame_id_base_link;
  // pose_pub = priv_node_handle_.advertise<nav_msgs::Odometry>("pose",1000);
  // odom_tf_pub = priv_node_handle_.advertise<>

  if (is_joint_state_topic_) joint_states_pub_ = priv_node_handle_.advertise<sensor_msgs::JointState>("joint_states", 100);
}

void DynamixelController::initSubscriber()
{
  trajectory_sub_ = priv_node_handle_.subscribe("joint_trajectory", 100, &DynamixelController::trajectoryMsgCallback, this);

  if (is_cmd_vel_topic_) cmd_vel_sub_ = node_handle_.subscribe("cmd_vel", 10, &DynamixelController::commandVelocityCallback, this);
}

void DynamixelController::initServer()
{
  dynamixel_command_server_ = priv_node_handle_.advertiseService("dynamixel_command", &DynamixelController::dynamixelCommandMsgCallback, this);
}

void DynamixelController::readCallback(const ros::TimerEvent&)
{
#ifdef DEBUG
  static double priv_read_secs =ros::Time::now().toSec();
#endif
 
  // here to get encoder info

  bool result = false;
  const char* log = NULL;

  dynamixel_workbench_msgs::DynamixelState  dynamixel_state[dynamixel_.size()];
  dynamixel_state_list_.dynamixel_state.clear();

  int32_t get_current[dynamixel_.size()];
  int32_t get_velocity[dynamixel_.size()];
  int32_t get_position[dynamixel_.size()];

  uint8_t id_array[dynamixel_.size()];
  uint8_t id_cnt = 0;

  for (auto const& dxl:dynamixel_)
  {
    dynamixel_state[id_cnt].name = dxl.first;
    dynamixel_state[id_cnt].id = (uint8_t)dxl.second;

    id_array[id_cnt++] = (uint8_t)dxl.second;
  }
#ifndef DEBUG
  if (is_moving_ == false)
  {
#endif
    if (dxl_wb_->getProtocolVersion() == 2.0f)
    {
      result = dxl_wb_->syncRead(SYNC_READ_HANDLER_FOR_PRESENT_POSITION_VELOCITY_CURRENT,
                                  id_array,
                                  dynamixel_.size(),
                                  &log);
      if (result == false)
      {
        ROS_ERROR("%s", log);
      }

      result = dxl_wb_->getSyncReadData(SYNC_READ_HANDLER_FOR_PRESENT_POSITION_VELOCITY_CURRENT,
                                                    id_array,
                                                    id_cnt,
                                                    control_items_["Present_Current"]->address,
                                                    control_items_["Present_Current"]->data_length,
                                                    get_current,
                                                    &log);
      if (result == false)
      {
        ROS_ERROR("%s", log);
      }

      result = dxl_wb_->getSyncReadData(SYNC_READ_HANDLER_FOR_PRESENT_POSITION_VELOCITY_CURRENT,
                                                    id_array,
                                                    id_cnt,
                                                    control_items_["Present_Velocity"]->address,
                                                    control_items_["Present_Velocity"]->data_length,
                                                    get_velocity,
                                                    &log);
      if (result == false)
      {
        ROS_ERROR("%s", log);
      }

      result = dxl_wb_->getSyncReadData(SYNC_READ_HANDLER_FOR_PRESENT_POSITION_VELOCITY_CURRENT,
                                                    id_array,
                                                    id_cnt,
                                                    control_items_["Present_Position"]->address,
                                                    control_items_["Present_Position"]->data_length,
                                                    get_position,
                                                    &log);
      if (result == false)
      {
        ROS_ERROR("%s", log);
      }

      for(uint8_t index = 0; index < id_cnt; index++)
      {
        dynamixel_state[index].present_current = get_current[index];
        dynamixel_state[index].present_velocity = get_velocity[index];
        dynamixel_state[index].present_position = get_position[index];

        dynamixel_state_list_.dynamixel_state.push_back(dynamixel_state[index]);
      }
    }
    else if(dxl_wb_->getProtocolVersion() == 1.0f)
    {
      uint16_t length_of_data = control_items_["Present_Position"]->data_length + 
                                control_items_["Present_Velocity"]->data_length + 
                                control_items_["Present_Current"]->data_length;
      uint32_t get_all_data[length_of_data];
      uint8_t dxl_cnt = 0;
      for (auto const& dxl:dynamixel_)
      {
        result = dxl_wb_->readRegister((uint8_t)dxl.second,
                                       control_items_["Present_Position"]->address,
                                       length_of_data,
                                       get_all_data,
                                       &log);
        if (result == false)
        {
          ROS_ERROR("%s", log);
        }

        dynamixel_state[dxl_cnt].present_current = DXL_MAKEWORD(get_all_data[4], get_all_data[5]);
        dynamixel_state[dxl_cnt].present_velocity = DXL_MAKEWORD(get_all_data[2], get_all_data[3]);
        dynamixel_state[dxl_cnt].present_position = DXL_MAKEWORD(get_all_data[0], get_all_data[1]);

        dynamixel_state_list_.dynamixel_state.push_back(dynamixel_state[dxl_cnt]);
        dxl_cnt++;
      }
    }
#ifndef DEBUG
  }
#endif

#ifdef DEBUG
  ROS_WARN("[readCallback] diff_secs : %f", ros::Time::now().toSec() - priv_read_secs);
  priv_read_secs = ros::Time::now().toSec();
#endif
}
// here to publish odom
void DynamixelController::publishCallback(const ros::TimerEvent&)
{
#ifdef DEBUG
  static double priv_pub_secs =ros::Time::now().toSec();
#endif
  dynamixel_state_list_pub_.publish(dynamixel_state_list_);
  // pose_pub.publish();  
  if (is_joint_state_topic_)
  {
    joint_state_msg_.header.stamp = ros::Time::now();

    joint_state_msg_.name.clear();
    joint_state_msg_.position.clear();
    joint_state_msg_.velocity.clear();
    joint_state_msg_.effort.clear();

    uint8_t id_cnt = 0;
    double vel[4] = {0,0,0,0};
    double theta[4] = {0,0,0,0};

    // here to read encoder
    for (auto const& dxl:dynamixel_)
    {
      double position = 0.0;
      double velocity = 0.0;
      double effort = 0.0;

      joint_state_msg_.name.push_back(dxl.first);

      if (dxl_wb_->getProtocolVersion() == 2.0f)
      {
        if (strcmp(dxl_wb_->getModelName((uint8_t)dxl.second), "XL-320") == 0) effort = dxl_wb_->convertValue2Load((int16_t)dynamixel_state_list_.dynamixel_state[id_cnt].present_current);
        else  effort = dxl_wb_->convertValue2Current((int16_t)dynamixel_state_list_.dynamixel_state[id_cnt].present_current);
      }
      else if (dxl_wb_->getProtocolVersion() == 1.0f) effort = dxl_wb_->convertValue2Load((int16_t)dynamixel_state_list_.dynamixel_state[id_cnt].present_current);

      velocity = dxl_wb_->convertValue2Velocity((uint8_t)dxl.second, (int32_t)dynamixel_state_list_.dynamixel_state[id_cnt].present_velocity);
      position = dxl_wb_->convertValue2Radian((uint8_t)dxl.second, (int32_t)dynamixel_state_list_.dynamixel_state[id_cnt].present_position);
      joint_state_msg_.effort.push_back(effort);
      joint_state_msg_.velocity.push_back(velocity);
      joint_state_msg_.position.push_back(position);

      if(id_cnt < 4)
      {
        vel[id_cnt] = velocity;
        ROS_INFO("velocity : id %d ,%lf",id_cnt, vel[id_cnt]);
      }
      else {
        theta[id_cnt - 4] = position;
        ROS_INFO("position : id %d ,%lf",id_cnt, theta[id_cnt - 4]);
      
      }
      id_cnt++;
    }
  }

  // ROS_INFO("")
  double dt = current_time_.toSec() - last_time_.toSec();
  // double & x = current_pose[0];
  // double & y = current_pose[1];
  // double & w = current_pose[2];

  // compute x, y, z by integration

  // FR_vec[0] = vel[0]*cos(theta[0]); num 1 5 
  // FR_vec[1] = vel[0]*sin(theta[0]);
  // FL_vec[0] = vel[1]*cos(theta[1]); num 2 6
  // FL_vec[1] = vel[1]*sin(theta[1]);
  // RR_vec[0] = vel[2]*cos(theta[2]); num 3 7
  // RR_vec[1] = vel[2]*sin(theta[2]);
  // RL_vec[0] = vel[3]*cos(theta[3]); num 4 8
  // RL_vec[1] = vel[3]*sin(theta[3]);

  // average vectors
  ROS_INFO("x, y, w : %lf, %lf, %lf", current_pose.at(0), current_pose.at(1), current_pose.at(2));
  ROS_INFO("publishing odom");

  current_pose.at(0) += (vel[0]*cos(theta[0] * PI / 180.0) + vel[1]*cos(theta[1] * PI / 180.0) + vel[2]*cos(theta[2] * PI / 180.0) + vel[3]*cos(theta[3] * PI / 180.0)) * dt; // dx
  current_pose.at(1) += (vel[0]*sin(theta[0] * PI / 180.0) + vel[1]*sin(theta[1] * PI / 180.0) + vel[2]*sin(theta[2] * PI / 180.0) + vel[3]*sin(theta[3] * PI / 180.0)) * dt; // dy
  current_pose.at(2) += 0;//dw;
  
  // publish tf
  // publishing transform odom->base_link

  ROS_INFO("publishing odom2");
  current_time_ = ros::Time::now();
  odom_trans.header.stamp = current_time_;
  odom_trans.header.frame_id = frame_id_odom;
  odom_trans.child_frame_id = frame_id_base_link;
  
  odom_trans.transform.translation.x = current_pose.at(0)/1000.0;
  odom_trans.transform.translation.y = current_pose.at(1)/1000.0;
  odom_trans.transform.translation.z = 0.0;
  odom_trans.transform.rotation = tf::createQuaternionMsgFromYaw(current_pose.at(2)*M_PI/180.0);
  
  odom_broadcaster.sendTransform(odom_trans);
  last_time_ = current_time_;
  joint_states_pub_.publish(joint_state_msg_);



#ifdef DEBUG
  ROS_WARN("[publishCallback] diff_secs : %f", ros::Time::now().toSec() - priv_pub_secs);
  priv_pub_secs = ros::Time::now().toSec();
#endif
}

void DynamixelController::commandVelocityCallback(const geometry_msgs::Twist::ConstPtr &msg)
{


  // cmd_vel 往 x 但是 實際往 y
  bool result = false;
  bool result1 = false;
  const char* log = NULL;

  double wheel_velocity[dynamixel_.size()];
  int32_t dynamixel_velocity[dynamixel_.size()];

  double theta[dynamixel_.size()];  //theta of Vy and Vx
  double wheel_angle[dynamixel_.size()]; //actual angle of motor
  int32_t dynamixel_position[dynamixel_.size()];  //actual value for motor(0~4095)
  //const uint8_t LEFT = 0;
  //const uint8_t RIGHT = 1;

  const uint8_t power1 = 1;
  const uint8_t power2 = 3;
  const uint8_t power3 = 0;
  const uint8_t power4 = 2;
  const uint8_t steer1 = 6;
  const uint8_t steer2 = 7;
  const uint8_t steer3 = 5;
  const uint8_t steer4 = 4;

  double robot_lin_vel_x = msg->linear.x;
  double robot_lin_vel_y = msg->linear.y;
  double robot_ang_vel = msg->angular.z;

  // ROS_INFO("")

  // uint8_t id_array[dynamixel_.size()];

  uint8_t id_array_power[4];
  uint8_t id_array_steer[4];
  uint8_t id_cnt = 0;

  float rpm = 0.0;
  for (auto const& dxl:dynamixel_)
  {
    const ModelInfo *modelInfo = dxl_wb_->getModelInfo((uint8_t)dxl.second);
    rpm = modelInfo->rpm;
    if(id_cnt < 4){
      id_array_power[id_cnt++] = (uint8_t)dxl.second;
    }
    else{
      id_array_steer[id_cnt - 4] = (uint8_t)dxl.second;
      id_cnt++;
    }
  }

//  V = r * w = r * (RPM * 0.10472) (Change rad/sec to RPM)
//       = r * ((RPM * Goal_Velocity) * 0.10472)		=> Goal_Velocity = V / (r * RPM * 0.10472) = V * VELOCITY_CONSTATNE_VALUE

  double velocity_constant_value = 1 / (wheel_radius_ * rpm * 0.10472);

  double A;
  double B;
  double C;
  double D;

  A=robot_lin_vel_x-(robot_ang_vel*wheel_separation_y_/2);
  B=robot_lin_vel_x+(robot_ang_vel*wheel_separation_y_/2);
  C=robot_lin_vel_y-(robot_ang_vel*wheel_separation_x_/2);
  D=robot_lin_vel_y+(robot_ang_vel*wheel_separation_x_/2);
/*if(fabs(robot_lin_vel_x)>0.001))

{
  const double vel_steering_offset = (robot_ang_vel*wheel_separation_y_/2);
  const double sign=copysign*/

  wheel_velocity[power1] = sqrt(C*C+A*A); 
  wheel_velocity[power2] = sqrt(C*C+B*B); 
  wheel_velocity[power3] = sqrt(D*D+A*A); 
  wheel_velocity[power4] = sqrt(D*D+B*B);

  wheel_angle[0]=atan2(C,A);//--DB
  wheel_angle[1]=atan2(C,B);//+-CB
  wheel_angle[2]=atan2(D,A);//-+DA
  wheel_angle[3]=atan2(D,B);//++CA

 //if(abs(robot_ang_vel*wheel_separation_y_/2)>=0.001)

 /* {wheel_velocity[power1] = sqrt(C*C+A*A); 
  wheel_velocity[power2] = sqrt(D*D+A*A); 
  wheel_velocity[power3] = sqrt(C*C+B*B); 
  wheel_velocity[power4] = sqrt(B*B+D*D);

  wheel_angle[0]=atan2(D,B);//--DB
  wheel_angle[1]=atan2(C,B);//+-CB
  wheel_angle[2]=atan2(D,A);//-+DA
  wheel_angle[3]=atan2(C,A);//++CA
 }*/

  if (dxl_wb_->getProtocolVersion() == 2.0f)
  {
    if (strcmp(dxl_wb_->getModelName(id_array_power[0]), "XL-320") == 0)
    {/*
      if (wheel_velocity[LEFT] == 0.0f) dynamixel_velocity[LEFT] = 0;
      else if (wheel_velocity[LEFT] < 0.0f) dynamixel_velocity[LEFT] = ((-1.0f) * wheel_velocity[LEFT]) * velocity_constant_value + 1023;
      else if (wheel_velocity[LEFT] > 0.0f) dynamixel_velocity[LEFT] = (wheel_velocity[LEFT] * velocity_constant_value);

      if (wheel_velocity[RIGHT] == 0.0f) dynamixel_velocity[RIGHT] = 0;
      else if (wheel_velocity[RIGHT] < 0.0f)  dynamixel_velocity[RIGHT] = ((-1.0f) * wheel_velocity[RIGHT] * velocity_constant_value) + 1023;
      else if (wheel_velocity[RIGHT] > 0.0f)  dynamixel_velocity[RIGHT] = (wheel_velocity[RIGHT] * velocity_constant_value);*/
    }
    else
    {
     //if(fabs(2.0*robot_lin_vel_x)>=abs(robot_ang_vel*wheel_separation_y_/2))
      dynamixel_velocity[power1]  = wheel_velocity[power1] * velocity_constant_value;
      dynamixel_velocity[power2] = -wheel_velocity[power2] * velocity_constant_value;
      dynamixel_velocity[power3]  = wheel_velocity[power3] * velocity_constant_value;
      dynamixel_velocity[power4] = -wheel_velocity[power4] * velocity_constant_value;
     /*if(abs(robot_ang_vel*wheel_separation_y_)/2>=0.001)
     {
      dynamixel_velocity[power1]  = -wheel_velocity[power1] * velocity_constant_value;
      dynamixel_velocity[power2] = wheel_velocity[power2] * velocity_constant_value;
      dynamixel_velocity[power3]  = -wheel_velocity[power3] * velocity_constant_value;
      dynamixel_velocity[power4] = wheel_velocity[power4] * velocity_constant_value;
	}*/
      for (int i = 0; i<=3; i++)
      {
        if (wheel_angle[i]>0.5*M_PI)
        {
          dynamixel_velocity[i] = -dynamixel_velocity[i];
          wheel_angle[i] = wheel_angle[i]-M_PI;
        }
        else if (wheel_angle[i]<-0.5*M_PI)
        {
          dynamixel_velocity[i] = -dynamixel_velocity[i];
          wheel_angle[i] = wheel_angle[i]+M_PI;
        }

      }

      dynamixel_position[steer1-4] = dxl_wb_->convertRadian2Value(id_array_steer[0], wheel_angle[0]);
      dynamixel_position[steer2-4] = dxl_wb_->convertRadian2Value(id_array_steer[1], wheel_angle[1]);
      dynamixel_position[steer3-4] = dxl_wb_->convertRadian2Value(id_array_steer[2], wheel_angle[2]);
      dynamixel_position[steer4-4] = dxl_wb_->convertRadian2Value(id_array_steer[3], wheel_angle[3]);

    }
  }
  else if (dxl_wb_->getProtocolVersion() == 1.0f)
  {/*
    if (wheel_velocity[LEFT] == 0.0f) dynamixel_velocity[LEFT] = 0;
    else if (wheel_velocity[LEFT] < 0.0f) dynamixel_velocity[LEFT] = ((-1.0f) * wheel_velocity[LEFT]) * velocity_constant_value + 1023;
    else if (wheel_velocity[LEFT] > 0.0f) dynamixel_velocity[LEFT] = (wheel_velocity[LEFT] * velocity_constant_value);

    if (wheel_velocity[RIGHT] == 0.0f) dynamixel_velocity[RIGHT] = 0;
    else if (wheel_velocity[RIGHT] < 0.0f)  dynamixel_velocity[RIGHT] = ((-1.0f) * wheel_velocity[RIGHT] * velocity_constant_value) + 1023;
    else if (wheel_velocity[RIGHT] > 0.0f)  dynamixel_velocity[RIGHT] = (wheel_velocity[RIGHT] * velocity_constant_value);*/
  }

  result = dxl_wb_->syncWrite(SYNC_WRITE_HANDLER_FOR_GOAL_VELOCITY, id_array_power, 4, dynamixel_velocity, 1, &log);
  if (result == false)
  {
    ROS_ERROR("%s", log);
  }
  log = NULL;

  // error add second syncwrite
  result1 = dxl_wb_->syncWrite(SYNC_WRITE_HANDLER_FOR_GOAL_POSITION, id_array_steer, 4, dynamixel_position, 1, &log);
  if (result1 == false)
  {
    ROS_ERROR("%s", log);
  }
  log = NULL;
}

void DynamixelController::writeCallback(const ros::TimerEvent&)
{
#ifdef DEBUG
  static double priv_pub_secs =ros::Time::now().toSec();
#endif
  bool result = false;
  const char* log = NULL;

  uint8_t id_array[dynamixel_.size()];
  uint8_t id_cnt = 0;

  int32_t dynamixel_position[dynamixel_.size()];

  static uint32_t point_cnt = 0;
  static uint32_t position_cnt = 0;

  for (auto const& joint:jnt_tra_msg_->joint_names)
  {
    id_array[id_cnt] = (uint8_t)dynamixel_[joint];
    id_cnt++;
  }

  if (is_moving_ == true)
  {
    for (uint8_t index = 0; index < id_cnt; index++)
      dynamixel_position[index] = dxl_wb_->convertRadian2Value(id_array[index], jnt_tra_msg_->points[point_cnt].positions.at(index));

    result = dxl_wb_->syncWrite(SYNC_WRITE_HANDLER_FOR_GOAL_POSITION, id_array, id_cnt, dynamixel_position, 1, &log);
    if (result == false)
    {
      ROS_ERROR("%s", log);
    }

    position_cnt++;
    if (position_cnt >= jnt_tra_msg_->points[point_cnt].positions.size())
    {
      point_cnt++;
      position_cnt = 0;
      if (point_cnt >= jnt_tra_msg_->points.size())
      {
        is_moving_ = false;
        point_cnt = 0;
        position_cnt = 0;

        ROS_INFO("Complete Execution");
      }
    }
  }

#ifdef DEBUG
  ROS_WARN("[writeCallback] diff_secs : %f", ros::Time::now().toSec() - priv_pub_secs);
  priv_pub_secs = ros::Time::now().toSec();
#endif
}

void DynamixelController::trajectoryMsgCallback(const trajectory_msgs::JointTrajectory::ConstPtr &msg)
{
  uint8_t id_cnt = 0;
  bool result = false;
  WayPoint wp;

  if (is_moving_ == false)
  {
    jnt_tra_msg_->joint_names.clear();
    jnt_tra_msg_->points.clear();
    pre_goal_.clear();

    result = getPresentPosition(msg->joint_names);
    if (result == false)
      ROS_ERROR("Failed to get Present Position");

    for (auto const& joint:msg->joint_names)
    {
      ROS_INFO("'%s' is ready to move", joint.c_str());

      jnt_tra_msg_->joint_names.push_back(joint);
      id_cnt++;
    }

    if (id_cnt != 0)
    {
      uint8_t cnt = 0;
      while(cnt < msg->points.size())
      {
        std::vector<WayPoint> goal;
        for (std::vector<int>::size_type id_num = 0; id_num < msg->points[cnt].positions.size(); id_num++)
        {
          wp.position = msg->points[cnt].positions.at(id_num);

          if (msg->points[cnt].velocities.size() != 0)  wp.velocity = msg->points[cnt].velocities.at(id_num);
          else wp.velocity = 0.0f;

          if (msg->points[cnt].accelerations.size() != 0)  wp.acceleration = msg->points[cnt].accelerations.at(id_num);
          else wp.acceleration = 0.0f;

          goal.push_back(wp);
        }

        if (use_moveit_ == true)
        {
          trajectory_msgs::JointTrajectoryPoint jnt_tra_point_msg;

          for (uint8_t id_num = 0; id_num < id_cnt; id_num++)
          {
            jnt_tra_point_msg.positions.push_back(goal[id_num].position);
            jnt_tra_point_msg.velocities.push_back(goal[id_num].velocity);
            jnt_tra_point_msg.accelerations.push_back(goal[id_num].acceleration);
          }

          jnt_tra_msg_->points.push_back(jnt_tra_point_msg);

          cnt++;
        }
        else
        {
          jnt_tra_->setJointNum((uint8_t)msg->points[cnt].positions.size());

          double move_time = 0.0f;
          if (cnt == 0) move_time = msg->points[cnt].time_from_start.toSec();
          else move_time = msg->points[cnt].time_from_start.toSec() - msg->points[cnt-1].time_from_start.toSec();

          jnt_tra_->init(move_time,
                         write_period_,
                         pre_goal_,
                         goal);

          std::vector<WayPoint> way_point;
          trajectory_msgs::JointTrajectoryPoint jnt_tra_point_msg;

          for (double index = 0.0; index < move_time; index = index + write_period_)
          {
            way_point = jnt_tra_->getJointWayPoint(index);

            for (uint8_t id_num = 0; id_num < id_cnt; id_num++)
            {
              jnt_tra_point_msg.positions.push_back(way_point[id_num].position);
              jnt_tra_point_msg.velocities.push_back(way_point[id_num].velocity);
              jnt_tra_point_msg.accelerations.push_back(way_point[id_num].acceleration);
            }

            jnt_tra_msg_->points.push_back(jnt_tra_point_msg);
            jnt_tra_point_msg.positions.clear();
            jnt_tra_point_msg.velocities.clear();
            jnt_tra_point_msg.accelerations.clear();
          }

          pre_goal_ = goal;
          cnt++;
        }
      }
      ROS_INFO("Succeeded to get joint trajectory!");
      is_moving_ = true;
    }
    else
    {
      ROS_WARN("Please check joint_name");
    }
  }
  else
  {
    ROS_WARN("Dynamixel is moving");
  }
}

bool DynamixelController::dynamixelCommandMsgCallback(dynamixel_workbench_msgs::DynamixelCommand::Request &req,
                                                      dynamixel_workbench_msgs::DynamixelCommand::Response &res)
{
  bool result = false;
  const char* log;

  uint8_t id = req.id;
  std::string item_name = req.addr_name;
  int32_t value = req.value;

  result = dxl_wb_->itemWrite(id, item_name.c_str(), value, &log);
  if (result == false)
  {
    ROS_ERROR("%s", log);
    ROS_ERROR("Failed to write value[%d] on items[%s] to Dynamixel[ID : %d]", value, item_name.c_str(), id);
  }

  res.comm_result = result;

  return true;
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "dynamixel_workbench_controllers");
  ros::NodeHandle node_handle("");

  std::string port_name = "/dev/power";
  // std::string 
  uint32_t baud_rate = 57600;
  nav_msgs::Odometry position;


  // std::string frame_id_odom;

  if (argc < 2)
  {
    ROS_ERROR("Please set '-port_name' and  '-baud_rate' arguments for connected Dynamixels");
    return 0;
  }
  else
  {
    port_name = argv[1];
    baud_rate = atoi(argv[2]);
  }

  DynamixelController dynamixel_controller;

  bool result = false;

  std::string yaml_file = node_handle.param<std::string>("dynamixel_info", "");

 // std::string frame_id_odom = node_handle.param<std::string>("odom_frame", std::string("odom"));

  result = dynamixel_controller.initWorkbench(port_name, baud_rate);
  if (result == false)
  {
    ROS_ERROR("Please check USB port name");
    return 0;
  }

  result = dynamixel_controller.getDynamixelsInfo(yaml_file);
  if (result == false)
  {
    ROS_ERROR("Please check YAML file");
    return 0;
  }

  result = dynamixel_controller.loadDynamixels();
  if (result == false)
  {
    ROS_ERROR("Please check Dynamixel ID or BaudRate");
    return 0;
  }

  result = dynamixel_controller.initDynamixels();
  if (result == false)
  {
    ROS_ERROR("Please check control table (http://emanual.robotis.com/#control-table)");
    return 0;
  }

  result = dynamixel_controller.initControlItems();
  if (result == false)
  {
    ROS_ERROR("Please check control items");
    return 0;
  }

  result = dynamixel_controller.initSDKHandlers();
  if (result == false)
  {
    ROS_ERROR("Failed to set Dynamixel SDK Handler");
    return 0;
  }

  dynamixel_controller.initPublisher();
  dynamixel_controller.initSubscriber();
  //dynamixel_controller.initServer();

  ros::Timer read_timer = node_handle.createTimer(ros::Duration(dynamixel_controller.getReadPeriod()), &DynamixelController::readCallback, &dynamixel_controller);
  ros::Timer write_timer = node_handle.createTimer(ros::Duration(dynamixel_controller.getWritePeriod()), &DynamixelController::writeCallback, &dynamixel_controller);
  ros::Timer publish_timer = node_handle.createTimer(ros::Duration(dynamixel_controller.getPublishPeriod()), &DynamixelController::publishCallback, &dynamixel_controller);

  ros::spin();

  return 0;
}
