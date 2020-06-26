#include "config.h"
#include "odrive_diff.h"

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <dynamic_reconfigure/server.h>
#include <string>
#include <iostream>
#include <fstream>
#include <math.h>


#define PI 3.141592653

odrive_diff::odrive_diff()
{
  hardware_interface::JointStateHandle left_wheel_state_handle("left_wheel", &joints[0].pos.data, &joints[0].vel.data, &joints[0].eff.data);
  hardware_interface::JointStateHandle right_wheel_state_handle("right_wheel", &joints[1].pos.data, &joints[1].vel.data, &joints[1].eff.data);
  joint_state_interface.registerHandle(left_wheel_state_handle);
  joint_state_interface.registerHandle(right_wheel_state_handle);
  registerInterface(&joint_state_interface);

  hardware_interface::JointHandle left_wheel_vel_handle(joint_state_interface.getHandle("left_wheel"), &joints[0].cmd.data);
  hardware_interface::JointHandle right_wheel_vel_handle(joint_state_interface.getHandle("right_wheel"), &joints[1].cmd.data);
  velocity_joint_interface.registerHandle(left_wheel_vel_handle);
  velocity_joint_interface.registerHandle(right_wheel_vel_handle);
  registerInterface(&velocity_joint_interface);

  // These publishers are only for debugging purposes
  // left_pos_pub = nh.advertise<std_msgs::Float64>("ODriver/left_wheel/position", 3);
  // right_pos_pub = nh.advertise<std_msgs::Float64>("ODriver/right_wheel/position", 3);
  // left_vel_pub = nh.advertise<std_msgs::Float64>("ODriver/left_wheel/velocity", 3);
  // right_vel_pub = nh.advertise<std_msgs::Float64>("ODriver/right_wheel/velocity", 3);
  // left_eff_pub = nh.advertise<std_msgs::Float64>("ODriver/left_wheel/eff", 3);
  // right_eff_pub = nh.advertise<std_msgs::Float64>("ODriver/right_wheel/eff", 3);
  // left_cmd_pub = nh.advertise<std_msgs::Float64>("ODriver/left_wheel/cmd", 3);
  // right_cmd_pub = nh.advertise<std_msgs::Float64>("ODriver/right_wheel/cmd", 3);

  vbus_pub = nh.advertise<std_msgs::Float64>("odrive/vbus", 3);

  // Odometry 
  odom_encoder_pub = nh.advertise<nav_msgs::Odometry>("odom", 50);

  // FIXME! Read parameters from ROS
  wheel_radius     = WHEEL_RADIUS;
  wheel_separation = WHEEL_SEPARATION;

  /****** ODRIVE USB SETUP *******/

  // Get odrive endpoint instance
  endpoint = new odrive_endpoint();

  // od_sn = "0x20673893304E";

  nh.param<std::string>("od_sn", od_sn, "0x00000000");
  nh.param<std::string>("od_cfg", od_cfg, "");

  try{
    // Get device serial number
    if (nh.getParam("od_sn", od_sn)) {
        ROS_INFO("Node odrive S/N: %s", od_sn.c_str());
    }
    else {
        throw "Failed to get sn parameter";
    }

  // Enumarate Odrive target
    if (endpoint->init(stoull(od_sn, 0, 16)))
    {
        throw "Device not found!";
    }

  }catch(const char* msg) {
    std::cout << msg << std::endl;
  }
  

  // Read JSON from target
  if (getJson(endpoint, &odrive_json)) {
      // Do nothing
  }
  else{
    targetJsonValid = true;

    // Process configuration file
    if (nh.searchParam("od_cfg", od_cfg)) {
        nh.getParam("od_cfg", od_cfg);
        ROS_INFO("Using configuration file: %s", od_cfg.c_str());

      updateTargetConfig(endpoint, odrive_json, od_cfg);
    }
  }

  /******** ODRIVE FINISH USB SETUP *******/
  
  // Arm axis 0 
  u8val = AXIS_STATE_CLOSED_LOOP_CONTROL;
  writeOdriveData(endpoint, odrive_json,std::string("axis0.requested_state"), u8val);
  // Arm axis 1
  u8val = AXIS_STATE_CLOSED_LOOP_CONTROL;
  writeOdriveData(endpoint, odrive_json,std::string("axis1.requested_state"), u8val);
 
  // Support dynamic reconfigure
  dsrv = new dynamic_reconfigure::Server<odrive_driver::OdriveConfig>(ros::NodeHandle("~"));
  dynamic_reconfigure::Server<odrive_driver::OdriveConfig>::CallbackType cb = boost::bind(&odrive_diff::reconfigure_callback, this, _1, _2);
  dsrv->setCallback(cb);
}

int odrive_diff::init(){
  return 1;
}

odrive_diff::~odrive_diff()
{
  // Set velocity
  fval = 0.0;
  writeOdriveData(endpoint, odrive_json,std::string("axis0.controller.vel_setpoint"), fval);
  writeOdriveData(endpoint, odrive_json,std::string("axis1.controller.vel_setpoint"), fval);

  endpoint->remove();
  delete endpoint;
}

void odrive_diff::reconfigure_callback(odrive_driver::OdriveConfig &_config, uint32_t level)
{
  config = _config;
  have_config = true;

  // printf("[ODRIVE] No configuration available for now.")
  // printf("[ODRIVE] Reconfiguring PID to [%f, %f, %f]\n", config.Kp, config.Ki, config.Kd);

  // // Set P gain
  // fval = (float)config.Kp;
  // writeOdriveData(endpoint, odrive_json,std::string("axis0.controller.config.pos_gain"), fval);
  // writeOdriveData(endpoint, odrive_json,std::string("axis1.controller.config.pos_gain"), fval);

  // // Set I gain
  // fval = (float)config.Ki;
  // writeOdriveData(endpoint, odrive_json,std::string("axis0.controller.config.vel_integrator_gain"), fval);
  // writeOdriveData(endpoint, odrive_json,std::string("axis1.controller.config.vel_integrator_gain"), fval);

  // // Set D gain 
  // fval = (float)config.Kd;
  // writeOdriveData(endpoint, odrive_json,std::string("axis0.controller.config.vel_gain"), fval);
  // writeOdriveData(endpoint, odrive_json,std::string("axis1.controller.config.vel_gain"), fval);

  // float kp0,kp1,ki0,ki1,kd0,kd1;

  // readOdriveData(endpoint, odrive_json, std::string("axis0.controller.config.pos_gain"), kp0);
  // readOdriveData(endpoint, odrive_json, std::string("axis1.controller.config.pos_gain"), kp1);
  // readOdriveData(endpoint, odrive_json, std::string("axis0.controller.config.vel_integrator_gain"), ki0);
  // readOdriveData(endpoint, odrive_json, std::string("axis1.controller.config.vel_integrator_gain"), ki1);
  // readOdriveData(endpoint, odrive_json, std::string("axis0.controller.config.vel_gain"), kd0);
  // readOdriveData(endpoint, odrive_json, std::string("axis1.controller.config.vel_gain"), kd1);


  // printf("[ODRIVE] Reconfigured values for PID Axis0: [%f, %f, %f] Axis1: [%f, %f, %f]\n", kp0, ki0, kd0, kp1, ki1, kd1);

}

void odrive_diff::read()
{

  // NEW CODE //
  // Collect data
  readOdriveData(endpoint, odrive_json, std::string("vbus_voltage"), fval);
  // vbus = fval;

  // vbus_pub.publish((std_msgs::Float64)fval);

  readOdriveData(endpoint, odrive_json, std::string("axis0.error"), u16val);
  error0 = u16val;
  readOdriveData(endpoint, odrive_json, std::string("axis1.error"), u16val);
  error1 = u16val;
  readOdriveData(endpoint, odrive_json, std::string("axis0.current_state"), u8val);
  state0 = u8val;
  readOdriveData(endpoint, odrive_json, std::string("axis1.current_state"), u8val);
  state1 = u8val;
  
  if(error0 || error1)
  {
    ROS_ERROR("[ODRIVE] Axis error has ocurred, shutting down..");
    ros::shutdown();
  }

  if(state0 != 8)
  {
    ROS_ERROR("[ODRIVE] Axis 0 changed state to: %d",state0);
  }

  if(state1 != 8)
  {
    ROS_ERROR("[ODRIVE] Axis 1 changed state to: %d",state1);
  }

  readOdriveData(endpoint, odrive_json,
                  std::string("axis0.encoder.vel_estimate"), fval);
  vel0 = fval;
  readOdriveData(endpoint, odrive_json,
                  std::string("axis1.encoder.vel_estimate"), fval);
  vel1 = fval;
  readOdriveData(endpoint, odrive_json,
                  std::string("axis0.encoder.pos_estimate"), fval);
  pos0 = fval;
  readOdriveData(endpoint, odrive_json,
                  std::string("axis1.encoder.pos_estimate"), fval);
  pos1 = fval;
  // readOdriveData(endpoint, odrive_json,
  //                 string("axis0.motor.current_meas_phB"), fval);
  // curr0B = fval;
  // readOdriveData(endpoint, odrive_json,
  //                 string("axis0.motor.current_meas_phC"), fval);
  // curr0C = fval;
  // readOdriveData(endpoint, odrive_json,
  //                 string("axis1.motor.current_meas_phB"), fval);
  // curr1B = fval;
  // readOdriveData(endpoint, odrive_json,
  //                 string("axis1.motor.current_meas_phC"), fval);
  // curr1C = fval;
  // execOdriveGetTemp(endpoint, odrive_json,
  //                 string("axis0.motor.get_inverter_temp"), fval);
  // temp0 = fval;
  // execOdriveGetTemp(endpoint, odrive_json,
  //                 string("axis1.motor.get_inverter_temp"), fval);
  // temp1 = fval;
  
  //////////////

  joints[0].vel.data = DIRECTION_CORRECTION * (vel0/encoder_CPR) * 2*PI*wheel_radius;
  joints[1].vel.data = DIRECTION_CORRECTION * (vel1/encoder_CPR) * 2*PI*wheel_radius;
  joints[0].pos.data = DIRECTION_CORRECTION * (pos0/encoder_CPR) * 2*PI*wheel_radius;
  joints[1].pos.data = DIRECTION_CORRECTION * (pos1/encoder_CPR) * 2*PI*wheel_radius;

  // printf("Result speed (rad/s): %f %f\n",joints[0].vel.data,joints[1].vel.data);

  // left_vel_pub.publish(vbus);
  // right_vel_pub.publish(joints[1].vel);
  // left_pos_pub.publish(joints[0].pos);
  // right_pos_pub.publish(joints[1].pos);

  // ODOMETRY // 
  // ticks since last encoder read
  double deltaTickr = pos1 - m_tickr;
  double deltaTickl = pos0 - m_tickl;

  // ticks to distance conversion 
  m_Dr =  2*PI*wheel_radius*(deltaTickr/encoder_CPR);
  m_Dl = -2*PI*wheel_radius*(deltaTickl/encoder_CPR);

  // // Distance from wheels to distance from center line conversion 
  m_Dc = (m_Dr+m_Dl)/2;

  // Differential drive robot kinematics
  // Postition estimation 
  double x_p   = m_x   + m_Dc*cos(m_phi);
  double y_p   = m_y   + m_Dc*sin(m_phi);
  double phi_p = m_phi + (m_Dr-m_Dl)/wheel_separation;

  // Velocity estimation
  double xd_p  =  wheel_radius/2*(-vel0+vel1)*cos(m_phi);
  double yd_p  =  wheel_radius/2*(-vel0+vel1)*sin(m_phi);
  double phid_p = (wheel_radius/wheel_separation)*(-vel1+vel0);

    // Publish odometry data 
  current_time = ros::Time::now();

  //since all odometry is 6DOF we'll need a quaternion created from yaw
  geometry_msgs::Quaternion odom_quat = tf::createQuaternionMsgFromYaw(phi_p);

  //first, we'll publish the transform over tf
  geometry_msgs::TransformStamped odom_trans;
  odom_trans.header.stamp = current_time;
  odom_trans.header.frame_id = "odom";
  odom_trans.child_frame_id = "base_footprint";

  odom_trans.transform.translation.x = x_p;
  odom_trans.transform.translation.y = y_p;
  odom_trans.transform.translation.z = 0.0;
  odom_trans.transform.rotation = odom_quat;

  //send the transform
  odom_broadcaster.sendTransform(odom_trans);

  //next, we'll publish the odometry message over ROS
  nav_msgs::Odometry odom; // Create odom object 
  odom.header.stamp = current_time; 
  odom.header.frame_id = "odom";  // We will publish this as odom_encoder

  //set the position
  odom.pose.pose.position.x = x_p;
  odom.pose.pose.position.y = y_p;
  odom.pose.pose.position.z = 0.0;
  odom.pose.pose.orientation = odom_quat;

  //set the velocity
  odom.child_frame_id = "base_footprint"; 
  odom.twist.twist.linear.x = xd_p;
  odom.twist.twist.linear.y = yd_p;
  odom.twist.twist.angular.z = phid_p;

  //publish the message
  odom_encoder_pub.publish(odom);

  // Update dynamic variables
  m_x = x_p;
  m_y = y_p;
  m_phi = phi_p;
  m_tickr = pos1;
  m_tickl = pos0;

}

void odrive_diff::write()
{

  // Inform interested parties about the commands we've got
  // left_cmd_pub.publish(joints[0].cmd);
  // right_cmd_pub.publish(joints[1].cmd);

  // Convert rad/s to m/s
  double left_speed = -1 *DIRECTION_CORRECTION * (joints[0].cmd.data/(2*PI)) * encoder_CPR;
  double right_speed =    DIRECTION_CORRECTION * (joints[1].cmd.data/(2*PI)) *  encoder_CPR;

  // Set velocity
  fval = (float)left_speed;
  writeOdriveData(endpoint, odrive_json,std::string("axis0.controller.vel_setpoint"), fval);
  // Set velocity
  fval = (float)right_speed;
  writeOdriveData(endpoint, odrive_json,std::string("axis1.controller.vel_setpoint"), fval);
  
}

void odrive_diff::updateWD(void){
  // update watchdog
  execOdriveFunc(endpoint, odrive_json, "axis0.watchdog_feed");
  execOdriveFunc(endpoint, odrive_json, "axis1.watchdog_feed");
}