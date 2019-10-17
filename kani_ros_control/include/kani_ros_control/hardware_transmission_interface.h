#include <hardware_interface/joint_command_interface.h>
#include <hardware_interface/joint_state_interface.h>
#include <hardware_interface/robot_hw.h>

class kani_robot : public hardware_interface::RobotHW
{
public:
  kani_robot() 
 { 
   // connect and register the joint state interface
   hardware_interface::JointStateHandle state_handle_a("A", &pos[0], &vel[0], &eff[0]);
   jnt_state_interface.registerHandle(state_handle_a);

   hardware_interface::JointStateHandle state_handle_b("B", &pos[1], &vel[1], &eff[1]);
   jnt_state_interface.registerHandle(state_handle_b);

   
   hardware_interface::JointStateHandle state_handle_c("C", &pos[2], &vel[2], &eff[2]);
   jnt_state_interface.registerHandle(state_handle_c);

   hardware_interface::JointStateHandle state_handle_d("D", &pos[3], &vel[3], &eff[3]);
   jnt_state_interface.registerHandle(state_handle_d);


   registerInterface(&jnt_state_interface);

   // connect and register the joint position interface
   hardware_interface::JointHandle pos_handle_a(jnt_state_interface.getHandle("A"), &cmd[0]);
   jnt_pos_interface.registerHandle(pos_handle_a);

   hardware_interface::JointHandle pos_handle_b(jnt_state_interface.getHandle("B"), &cmd[1]);
   jnt_pos_interface.registerHandle(pos_handle_b);
 
   hardware_interface::JointHandle pos_handle_c(jnt_state_interface.getHandle("C"), &cmd[2]);
   jnt_pos_interface.registerHandle(pos_handle_c);
   
   hardware_interface::JointHandle pos_handle_d(jnt_state_interface.getHandle("D"), &cmd[3]);
   jnt_pos_interface.registerHandle(pos_handle_d);

   registerInterface(&jnt_pos_interface);
  }

private:
  hardware_interface::JointStateInterface jnt_state_interface;
  hardware_interface::PositionJointInterface jnt_pos_interface;
  double cmd[4];
  double pos[4];
  double vel[4];
  double eff[4];
};
