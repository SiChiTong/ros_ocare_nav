#include <sys/mman.h>
#include "fuzzycontroller.h"

#include <pluginlib/class_list_macros.h>

ocare_controllers::FuzzyController::FuzzyController() {
}

ocare_controllers::FuzzyController::~FuzzyController() {

}

void ocare_controllers::FuzzyController::command_callback(
        const trajectory_msgs::JointTrajectoryPoint::ConstPtr &referencePoint) {
    // TODO: get TrajectoryPoint and set the value to Class
}

bool ocare_controllers::FuzzyController::init(
        hardware_interface::EffortJointInterface *robot, ros::NodeHandle &n) {

    m_node = n;
    m_robot = robot;

    XmlRpc::XmlRpcValue joint_names;
    if(!m_node.getParam("joints",joint_names)) {
        ROS_ERROR("No 'joints' in controller. (namespace: %s)",
                m_node.getNamespace().c_str());
        return false;
    }

    if(joint_names.getType() != XmlRpc::XmlRpcValue::TypeArray) {
        ROS_ERROR("'joints' is not a struct. (namespace: %s)",
                m_node.getNamespace().c_str());
        return false;
    }

    for(int i=0; i < joint_names.size();i++)
    {
        XmlRpc::XmlRpcValue &name_value=joint_names[i];
        if(name_value.getType() != XmlRpc::XmlRpcValue::TypeString)
        {
            ROS_ERROR("joints are not strings. (namespace: %s)",
                    m_node.getNamespace().c_str());
            return false;
        }

        ROS_INFO("Find \"%s\" joint !", ((std::string)name_value).data() );
        hardware_interface::JointHandle j=m_robot->
                getHandle((std::string)name_value);
        m_joints.push_back(j);
    }

    m_sub_command = m_node.subscribe("command", 1000, &FuzzyController::command_callback, this);

    std::string robot_desc_string;
    if(!m_node.getParam("/robot_description",robot_desc_string))
            {
        ROS_ERROR("Could not find '/robot_description'.");
        return false;
    }

    return true;
}

void ocare_controllers::FuzzyController::starting(const ros::Time &time) {
    struct sched_param param;
    param.sched_priority=sched_get_priority_max(SCHED_FIFO);
    if(sched_setscheduler(0,SCHED_FIFO,&param) == -1)
    {
            ROS_WARN("Failed to set real-time scheduler.");
            return;
    }
    if(mlockall(MCL_CURRENT|MCL_FUTURE) == -1)
            ROS_WARN("Failed to lock memory.");
}

void ocare_controllers::FuzzyController::update(const ros::Time &time, const ros::Duration &duration) {
   return;
}

PLUGINLIB_EXPORT_CLASS(ocare_controllers::FuzzyController,
        controller_interface::ControllerBase)
