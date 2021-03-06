﻿// Include the main ROS header
#include<ros/ros.h>
#include<ros/console.h>

// Use the enum of Topic CMD for Diff and Arm
#include"ocare_hw_node.h"

#include"std_msgs/MultiArrayDimension.h"
#include"std_msgs/MultiArrayLayout.h"
#include"std_msgs/UInt16MultiArray.h"
#include"std_msgs/Int32.h"
#include"sensor_msgs/LaserScan.h"
#include"sensor_msgs/Imu.h"
#include"geometry_msgs/Twist.h"
#include"tf/tf.h"

#include<cmath>

//#define __arm__

//#define _DEBUG_SENSOR

#ifdef __arm__

extern "C" {
#include <wiringPi.h>
}

#define PIN_SW_NEED_BULB_TESK (21)
#define PIN_SW_START          (22)
bool fg_need_bulb(false);
bool fg_start(false);
int  fg_mode(-1);
#else
bool fg_need_bulb(true);
bool fg_start(false);
int  fg_mode(-1);

#endif


/****************    Config   *******************/

#define NO_LINE_THROSHOLD (SENSOR_REG_COUNT * 100 - 100)
#define SENSOR_BLACK_THROSHOLD  (87)
#define SENSOR_WHITE_THROSHOLD  (13)
#define SIZE_DATA_RECOARD (10)
#define CONVERG_THROSHOLD (M_PI * 5.0/180.0)

#define ORIENT_RIGHT_KP         (10)

#define TASK_8_LENGTH_FRONT     (0.6)
#define TASK_10_LENGTH_RIGHT    (0.4)
#define TASK_13_LENGTH_FRONT    (0.6)
#define TASK_13_LENGTH_RIGHT    (0.47)
#define TASK_15_LENGTH_RIGHT    (0.5)
#define TASK_17_LENGTH_RIGHT    (0.38)
#define TASK_19_LENGTH_RIGHT    (0.38)


#define TASK_1_DURATION         (1.3)
#define TASK_2_DURATION         (2.5)

#define TASK_220_DURATION       (1.8)
#define TASK_221_DURATION       (4.0)
#define TASK_222_DURATION       (1.5)

#define TASK_110_DURATION       (3)
#define TASK_111_DURATION       (1.5)
#define TASK_112_DURATION       (5)
#define TASK_114_DURATION       (1.3)

#define TASK_14_1_DURATION      (1.0)
#define TASK_15_1_DURATION      (1.0)

#define TASK_17_DURATION        (2.0)
#define TASK_19_DURATION        (3.0)

/************************************************/

/**************** DEBUG Flags ******************/

//#define _DEBUG_SENSOR

#define DT_STAGE_CHANGE_DETECT "StageChangeDetecter"
#define DT_LOOP_TESK_DEBUG     "LoopTesk"

//#define _STAGE_CHANGE_DETECT_DEBUG
//#define _LOOP_TESK_DEBUG

/***********************************************/

/**************** Mode Define ******************/

// Diff Mode
#define MODE_AUTO_START     (1)
#define MODE_REMOTE_START   (2)
#define MODE_STOP           (-1)

// Arm Mode
#define MODE_ARM_R_SLIDER_CMD_MASK    (1u << 6)
#define MODE_ARM_R_SLIDER_HOME        (0u << 6)
#define MODE_ARM_R_SLIDER_OPEN        (1u << 6)

#define MODE_ARM_L_SLIDER_CMD_MASK    (1u << 7)
#define MODE_ARM_L_SLIDER_HOME        (0u << 7)
#define MODE_ARM_L_SLIDER_OPEN        (1u << 7)

#define MODE_ARM_HOME_POSE          (-2)
#define MODE_ARM_BTN_POSE           (2)
#define MODE_ARM_FREE_CONTROL       (3)

/***********************************************/

float front_length(10);
float right_length(10);
float left_length(10);

double orient(0);
int stage(0);

uint16_t arm_mode = ArmModbus::ArmModeCMD::ARM_HOME_CMD;
uint16_t slider_mode = 0;
uint16_t catch_level = 0;

bool sensor_ready(false);
bool imu_ready(false);
bool laser_ready(false);

float laser_angle_max;
float laser_angle_min;
float laser_angle_increment;
float laser_count_max;


sensor_msgs::LaserScan laser_msg;


int sensor_value[SENSOR_REG_COUNT] = { 0 };

void loop_tesk(int _stage, ros::Publisher* _diff_pub, ros::Publisher* _diff_twist_pub);
void do_gohome_tesk(int _stage, std_msgs::UInt16MultiArray* _mode_msg, geometry_msgs::Twist* _twist_msg);
void do_button_tesk(int _stage, std_msgs::UInt16MultiArray* _mode_msg, geometry_msgs::Twist* _twist_msg);
bool stage_change_detect(int _stage);
double get_sensor_average();
bool is_sensor_noline_all_black();
bool is_sensor_noline_all_white();
double cot_angle(double _degree);
inline float get_laser_distence(float _angle);
inline float get_right_distence(float _orient_offset);
inline float get_left_distence(float _orient_offset);
inline float get_front_distence(float _orient_offset);

class ConvergDetector {
public:
    ConvergDetector() :
        index_counter(0),
        fg_inited(false),
        fg_started(false),
        ref_data(0.0) {}

    ~ConvergDetector() {}

private:

    // Flag for Detector status
    bool fg_inited;
    bool fg_started;
    int index_counter;

    // Detector registers
    double ref_data;
    double data_recoard[SIZE_DATA_RECOARD];
public:
    // Need init first
    void init(double _ref) {
        fg_inited = true;
        ref_data = _ref;
        for(int i=0;i<SIZE_DATA_RECOARD;i++) {
            data_recoard[i] = 10.0;
        }
    }

    bool start() {
        if(fg_inited) {
            fg_started = true;
            return true;
        }
        else {
            return false;
        }
    }

    void stop() {
        fg_started = false;
        fg_inited = false;
    }

    void update() {
        if(fg_started) {
            data_recoard[index_counter] = cot_angle(fabs(ref_data - orient));
            index_counter++;
            if(index_counter == SIZE_DATA_RECOARD) index_counter = 0;
            ROS_INFO_NAMED("ConvergDetector", "Debug : Error= %f ,ref_data = %f,orient = %f",
                           fabs(ref_data - orient),ref_data,orient);
        }
        else {
            ROS_ERROR_NAMED("ConvergDetector", "Need start first!");
        }
    }

    bool isConverged() {

        if(fg_started) {
            double sum(0);
            double avg(0);
            for(int i=0;i<SIZE_DATA_RECOARD;i++) {
                sum += data_recoard[i];
            }
            avg = sum / SIZE_DATA_RECOARD;
            ROS_INFO_NAMED("ConvergDetector", "Debug : Avg = %f ",avg);
            return (CONVERG_THROSHOLD > avg);

        }

        else {
            ROS_ERROR_NAMED("ConvergDetector", "Need start first!");
            return false;
        }

    }

    bool isStarted() {
        return fg_started;
    }
};

class WBDetector {
public:
    WBDetector() :
        fg_inited(false),
        fg_started(false) {}

    ~WBDetector() {}

private:

    // Flag for Detector status
    bool fg_inited;
    bool fg_started;
    bool fg_w2b;

    int head;
    int w_detected;
    int b_detected;

public:
    // Need init first
    void init(bool _is_w2b) {
        fg_w2b = _is_w2b;
        head = 0;
        w_detected = -1;
        b_detected = -1;
        fg_inited = true;
    }

    bool start() {
        if(fg_inited) {
            fg_started = true;
            return true;
        }
        else {
            return false;
        }
    }

    void stop() {
        fg_started = false;
        fg_inited = false;
    }

    void update() {
        ROS_DEBUG_NAMED("WBDetector", "w_detected:%d, b_detected:%d, sensor AVG: %f",
                        w_detected, b_detected, get_sensor_average());
        if(fg_started) {
            if( get_sensor_average() > 70.0 && b_detected < 0)
                b_detected = head ++;
            if( get_sensor_average() < 30.0 && w_detected < 0)
                w_detected = head ++;
        }
        else {
            ROS_ERROR_NAMED("WBDetector", "Need start first!");
        }
    }

    bool isDetected() {
        if(fg_started) {
            if(b_detected < 0 || w_detected < 0) {
                return false;
            }
            else {
                if(fg_w2b)
                    return b_detected > w_detected;
                else
                    return w_detected > b_detected;
            }
        }
        else {
            ROS_ERROR_NAMED("WBDetector", "Need start first!");
            return false;
        }

    }

    bool isStarted() {
        return fg_started;
    }
};

ConvergDetector stable_detector;
WBDetector wb_detector;

// The GUI Panel command callback
void callback_stage_cmd(const std_msgs::Int32ConstPtr &msg) {

    stage = msg->data;

    // Reset Stage Detector
    stable_detector.stop();
    wb_detector.stop();
}

void callback_arm_cmd(const std_msgs::Int32ConstPtr &msg) {

    switch(msg->data) {
    case MODE_ARM_HOME_POSE:
        arm_mode = ArmModbus::ArmModeCMD::ARM_HOME_CMD;
        break;
    case MODE_ARM_BTN_POSE:
        arm_mode = ArmModbus::ArmModeCMD::ARM_BUTTON_POSE_CMD;
        break;
    case MODE_ARM_FREE_CONTROL:
        arm_mode = ArmModbus::ArmModeCMD::ARM_FREE_CONTROLL_CMD;
        break;
    default:

        if( (MODE_ARM_R_SLIDER_CMD_MASK & msg->data) != 0x00) {
            slider_mode &= ~SLIDER_RIGHT_CMD_MASK;
            slider_mode |= ArmModbus::SLIDER_RO_CMD;
        }
        else {
            slider_mode &= ~SLIDER_RIGHT_CMD_MASK;
            slider_mode |= ArmModbus::SLIDER_RC_CMD;
        }

        if( (MODE_ARM_L_SLIDER_CMD_MASK & msg->data) != 0x00) {
            slider_mode &= ~SLIDER_LEFT_CMD_MASK;
            slider_mode |= ArmModbus::SLIDER_LO_CMD;
        }
        else {
            slider_mode &= ~SLIDER_LEFT_CMD_MASK;
            slider_mode |= ArmModbus::SLIDER_LC_CMD;
        }

        break;

    }

}

void callback_arm_catch(const std_msgs::Int32ConstPtr &msg) {

    int a_catch_level = msg->data;
    if( a_catch_level > 100) a_catch_level = 100;
    else if( a_catch_level < 0 ) a_catch_level = 0;

    catch_level = a_catch_level;

}


void callback_control(const std_msgs::Int32ConstPtr &msg) {

    // Register the GUI controll MSG
    fg_mode = msg->data;
    switch(msg->data) {
    case MODE_AUTO_START:
    case MODE_REMOTE_START:
        fg_start = true;
        break;
    case MODE_STOP:
    default:
        fg_start = false;
        break;

    }
}

void callback_laser(sensor_msgs::LaserScan msg) {

    // Initial the Laser data at first time received /scan topic
    if( !laser_ready ) {

        laser_angle_max = msg.angle_max;
        laser_angle_min = msg.angle_min;
        laser_angle_increment = msg.angle_increment;
        laser_count_max = (laser_angle_max - laser_angle_min) / laser_angle_increment - 1;

        laser_ready = true;
    }

    laser_msg = msg;


    float length_sum(0);
    for(int i=84 - 5 ;i< 84 + 5;i++)
     length_sum += msg.ranges[i];
    right_length = length_sum / 10;

    length_sum = 0;
    for(int i= 84 + 256 - 5;i< 84 + 256 + 5;i++)
     length_sum += msg.ranges[i];
    front_length = length_sum / 10;

    length_sum = 0;
    for(int i= 84 + 512 - 5;i< 84 + 512 + 5;i++)
     length_sum += msg.ranges[i];
    left_length = length_sum / 10;


}

void callback_imu(const sensor_msgs::ImuConstPtr &msg) {
    tf::Matrix3x3 m(tf::Quaternion(msg->orientation.x,msg->orientation.y,msg->orientation.z,msg->orientation.w));
    double roll, pitch, yaw;
    m.getRPY(roll,pitch,yaw);
    orient = (yaw);
    imu_ready = true;
}

void callback_sensor(const std_msgs::UInt16MultiArrayConstPtr &msg) {
    const short unsigned int* ptr = msg->data.data();
    for(int i=0; i<SENSOR_REG_COUNT; i++) {
        sensor_value[i] = 100-ptr[i];
    }
    sensor_ready = true;

}

int main(int argc, char** argv) {

#ifdef __arm__
    if( wiringPiSetup() == -1)
        ROS_ERROR("WiringPi LIBRARY can't loading.");
#endif

    // Initial the ROS
    ros::init(argc,argv,"diff_test_stage_node");

    // Create a ros node
    ros::NodeHandle node;

/******************************** Subscriber ********************************/

    // Subscribe the laser scan topic
    ros::Subscriber laser_sub =
            node.subscribe<sensor_msgs::LaserScan>("/scan", 50, callback_laser);

    // Subscribe the IMU topic
    ros::Subscriber imu_sub =
            node.subscribe<sensor_msgs::Imu>("/imu", 50, callback_imu);

    // Subscribe the tracking line sensor topic
    ros::Subscriber sensor_sub =
            node.subscribe<std_msgs::UInt16MultiArray>("/track_line_sensor", 50, callback_sensor);

    // Subscribe the stage cmd topic
    ros::Subscriber stage_sub =
            node.subscribe<std_msgs::Int32>("/stage_set_cmd", 10, callback_stage_cmd);

    // Subscribe the cmd topic for this stage controller
    ros::Subscriber control_sub =
            node.subscribe<std_msgs::Int32>("/diff_mode_controller_cmd", 10, callback_control);

    // Subscribe the arm cmd topic from GUI interface
    ros::Subscriber arm_control_sub =
            node.subscribe<std_msgs::Int32>("/arm_mode_controller_cmd", 10, callback_arm_cmd);

    // subscriber for arm catch level command
    ros::Subscriber m_sub_catch_command =
            node.subscribe("/arm_catch_level", 50, callback_arm_catch);



/*****************************************************************************/

/******************************** Publishers ********************************/
    /********** The Diff command Topic Struct for uint16_t array
    *  enum DiffTopicCMD {
    *     DIFF_MODE_CMD,
    *     TRACK_TORQUE_MODE_CMD,
    *     SENSOR_BW_MODE_CMD
    *  };
    ***************************************************************/


    /******Defination of the Chassis Mode for Control Mode
     * MODE_TRACK_LINE :     Tracking the path and follow the path
     * MODE_CONTROLLABLE:    The Arduino would use LEFT_WHEEL_TORQUE and RIGHT_WHEEL_TORQUE to effort the wheel
     * MODE_STOP:            All of the driver would stop until Mode become others.
     * **************************/

    /****** Defination of the Chassis Mode for Tracking line
     * HIGH :           Useing the higher torque Fuzzy rule
     * MED:             Useing the normal torque Fuzzy rule
     * LOW:             Useing the low torque Fuzzy rule
     * **************************/

    /******Defination of the Sensor BW Mode
     * BLACK :           Tracking the black line
     * WRITE:            Tracking the white line
     * **************************/

    // Create a publish that publish the differential wheel Mode
    ros::Publisher diff_pub =
            node.advertise<std_msgs::UInt16MultiArray>("/diff_mode_cmd", 50);

    /********** The Arm command Topic Struct for uint16_t array
    *  enum ArmTopicCMD {
    *     ARM_MODE_CMD,
    *     SLIDER_MODE_CMD,
    *     EFFORT_CATCH_LEVEL_CMD
    *  };
    ***************************************************************/


    /******Defination of the Arm Mode Command
     * ARM_HOME_CMD :           Arm go to Home position
     * ARM_BUTTON_POSE_CMD:     Arm go to push button pose
     * ARM_FREE_CONTROLL_CMD:   Arm go to motor controllable state,
     *                          let arm can be controll by ROS.
     * **************************/

    /******Defination of the Slider Command
     * SLIDER_OPEN_CMD :           Open the slider to the right position
     * SLIDER_CLOSE_CMD:           Close the slider to the home position
     * **************************/

    /******Defination of the catch level
     * 0 :               Catch full open
     * 100:              Catch full close
     * **************************/

    // Create a publish that publish the Arm mode cmd to HWModule node
    ros::Publisher arm_cmd_pub =
            node.advertise<std_msgs::UInt16MultiArray>("/arm_mode_cmd", 50);

    /******Defination of the Chassis Torque CMD
     * Linear.x :            The forward speed CMD
     * Angular.z:            The Orient CMD
     * **************************/

    // Create a publish that publish the differential wheel Torque CMD
    ros::Publisher diff_twist_pub =
            node.advertise<geometry_msgs::Twist>("/ocare/pose_fuzzy_controller/diff_cmd", 50);

    // Create a publish that publish the Stage mode to GUI panel
    ros::Publisher stage_pub =
            node.advertise<std_msgs::Int32>("/stage_mode", 50);




/*****************************************************************************/

    // Using the 10 hz for while loop
    ros::Rate r(20);



//    printf("Which stage to start?(0~20) \n");
//    scanf("%d", &stage);
//    if(stage < 0 || stage > 21) {
//        printf("Parameter error, Start at stage 0");
//        stage = 0;
//    }

    while(ros::ok()) {

#ifdef __arm__
        //fg_need_bulb    =  digitalRead(PIN_SW_NEED_BULB_TESK);
        //fg_start        =  digitalRead(PIN_SW_START);
        fg_need_bulb = true;
#endif

#ifdef _DEBUG_SENSOR

        if(laser_ready) {
            ROS_INFO("%10.3f ,%10.3f ,%10.3f",
                    get_left_distence(0),
                    get_right_distence(0),
                    get_front_distence());
        }

#else
        if(fg_start) {

            // Makesure every sensor topic is ready.
            if( sensor_ready && imu_ready && laser_ready ) {

                if( fg_mode == MODE_AUTO_START) {

                    // Do the current stage tesk
                    loop_tesk(stage, &diff_pub, &diff_twist_pub);

                    // Detect the stage change
                    if(stage == 11 && fg_need_bulb) {   // If Need Bulb tesk and stage is 11, then Change stage to Bulk tesk
                        if(stage_change_detect(stage)) stage = 110;
                    }
                    else if( stage >= 110 && stage <=115) {
                        if(stage_change_detect(stage)) stage++;
                        if(stage == 113) arm_mode = ArmModbus::ArmModeCMD::ARM_HOME_CMD;
                        if(stage == 115) stage = 12;
                    }
                    else if (stage == 17 || stage == 18) {
                        if(is_sensor_noline_all_white()) {
                            stage = 23;
                        }
                    }
                    else if (stage == 22) {
                        if(stage_change_detect(stage)) stage = 220;
                    }
                    else if( stage >= 220 && stage <=222) {
                        if(stage_change_detect(stage)) stage++;
                        if(stage == 223) stage = 23;
                    }

                    if(stage_change_detect(stage)) {
                        ROS_INFO("STAGE CHANGE");
                        stage++;

                    }
                }
                else if (fg_mode == MODE_REMOTE_START) {

                    // The chassic's Mode CMD message
                    std_msgs::UInt16MultiArray cmd_message;

                    cmd_message.data.clear();
                    cmd_message.data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
                    cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
                    cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);

                    // Publish the topic
                    diff_pub.publish(cmd_message);

                    // NOTE: the twist command will be sended by another node
                }

            }

        }
        else {
            // The chassic's Mode CMD message
            std_msgs::UInt16MultiArray cmd_message;
            // The differential wheel Torque CMD message
            geometry_msgs::Twist cmd_twist;

            cmd_message.data.clear();
            cmd_message.data.push_back((uint16_t)DiffModbus::MODE_STOP_CMD);
            cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
            cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
            cmd_twist.linear.x = 0;
            cmd_twist.angular.z = 0;

            // Publish the topic
            diff_pub.publish(cmd_message);
            diff_twist_pub.publish(cmd_twist);
        }

#endif

        // Report the current stage to GUI
        std_msgs::Int32 stage_msg;
        stage_msg.data = stage;
        stage_pub.publish(stage_msg);

        // Send the arm command to HWModule modbus interface node
        std_msgs::UInt16MultiArray arm_cmd_msg;
        arm_cmd_msg.data.clear();
        arm_cmd_msg.data.push_back(arm_mode);
        arm_cmd_msg.data.push_back(slider_mode);
        arm_cmd_msg.data.push_back(catch_level);

        arm_cmd_pub.publish(arm_cmd_msg);


        // Sleep for a while
        ros::spinOnce();
        r.sleep();
    }



    return 0;
}

void loop_tesk(int _stage, ros::Publisher* _diff_pub, ros::Publisher* _diff_twist_pub) {

    // The chassic's Mode CMD message
    std_msgs::UInt16MultiArray cmd_message;
    // The differential wheel Torque CMD message
    geometry_msgs::Twist cmd_twist;
    float ref_orient(0);

    cmd_message.data.clear();

    switch(_stage) {
    case 0:

        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 0;
        cmd_twist.angular.z = M_PI * 0.0/180.0;
        break;
    case 1:

        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 40;
        cmd_twist.angular.z = M_PI * 0.0/180.0;
        break;
    case 2:

        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 40;
        cmd_twist.angular.z = M_PI * -90/180.0;
        break;
    case 3:
    case 4:
    case 5:

        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_TRACK_LINE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 0;
        cmd_twist.angular.z = 0;
        break;
    case 6:
        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_TRACK_LINE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_HIGH_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 0;
        cmd_twist.angular.z = 0;
        break;

    case 7:

        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_TRACK_LINE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 0;
        cmd_twist.angular.z = 0;
        break;
    case 8:

        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 50;
        cmd_twist.angular.z = M_PI * 90.0/180.0;
        break;
    case 9:

        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 0;
        cmd_twist.angular.z = M_PI * 180.0/180.0;
        break;
    case 10:

        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 40;
        cmd_twist.angular.z = M_PI * 180.0/180.0 +
                ORIENT_RIGHT_KP * (TASK_10_LENGTH_RIGHT - get_right_distence(cot_angle(M_PI - orient)));
//        cmd_twist.angular.z = M_PI * 180.0/180.0;
        break;
    case 11:
        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 40;
        cmd_twist.angular.z = M_PI * 180.0/180.0 +
                ORIENT_RIGHT_KP * (TASK_10_LENGTH_RIGHT - get_right_distence(cot_angle(M_PI - orient)));
        break;
    case 110:
    case 111:
    case 112:
    case 113:
    case 114:

        do_button_tesk(_stage, &cmd_message, &cmd_twist);
        break;
    case 12:

        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_TRACK_LINE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 0;
        cmd_twist.angular.z = 0;
        break;
    case 13:
        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 50;
        ref_orient  = 0 +
                ORIENT_RIGHT_KP *
                (TASK_13_LENGTH_RIGHT - get_right_distence(-orient));
        if(ref_orient > 20.0/180.0 * M_PI)
            ref_orient = 20.0/180.0 * M_PI;
        else if(ref_orient < -20.0/180.0 * M_PI)
            ref_orient = -20.0/180.0 * M_PI;
        cmd_twist.angular.z = ref_orient;
        break;
    case 14:
        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 0;
        cmd_twist.angular.z = M_PI * 90.0/180.0;
        break;
    case 15:
        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 50;
        ref_orient = M_PI * 90.0/180.0 +
                ORIENT_RIGHT_KP *
                (TASK_15_LENGTH_RIGHT - get_right_distence(cot_angle(M_PI * 90.0/180.0-orient)));
        if(ref_orient > (90.0 + 20.0)/180.0 * M_PI)
            ref_orient = (90.0 + 20.0)/180.0 * M_PI;
        else if(ref_orient < (90.0 - 20.0)/180.0 * M_PI)
            ref_orient = (90.0 - 20.0)/180.0 * M_PI;
        cmd_twist.angular.z = ref_orient;
        break;
    case 16:
        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_TRACK_LINE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 0;
        cmd_twist.angular.z = 0;
        break;
    case 17:
        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_TRACK_LINE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_HIGH_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
//        cmd_twist.linear.x = 40;
        cmd_twist.linear.x = 0;
        cmd_twist.angular.z = 0;
//        ref_orient = M_PI * 180.0/180.0 +
//                ORIENT_RIGHT_KP * (TASK_17_LENGTH_RIGHT - get_right_distence(cot_angle(M_PI - orient)));

//        if((ref_orient < (180.0 - 45.0)/180.0 * M_PI) && (ref_orient > 0))
//            ref_orient = (180.0 - 45.0)/180.0 * M_PI;
//        else if(ref_orient < (-180.0 + 20.0)/180.0 * M_PI && (ref_orient < 0))
//            ref_orient = (-180.0 + 20.0)/180.0 * M_PI;
//        cmd_twist.angular.z = ref_orient;
        break;
    case 18:
        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_TRACK_LINE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::BLACK_CMD);
        cmd_twist.linear.x = 0;
        cmd_twist.angular.z = 0;
        break;
    case 19:
        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 40;
        ref_orient = M_PI * 180.0/180.0 +
                ORIENT_RIGHT_KP * (TASK_19_LENGTH_RIGHT - get_right_distence(cot_angle(M_PI - orient)));

        if((ref_orient < (180.0 - 45.0)/180.0 * M_PI) && (ref_orient > 0))
            ref_orient = (180.0 - 45.0)/180.0 * M_PI;
        else if(ref_orient < (-180.0 + 20.0)/180.0 * M_PI && (ref_orient < 0))
            ref_orient = (-180.0 + 20.0)/180.0 * M_PI;
        cmd_twist.angular.z = ref_orient;
        break;
    case 20:
    case 21:
        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_TRACK_LINE_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 0;
        cmd_twist.angular.z = 0;
        break;
    case 22:
    case 220:
    case 221:
    case 222:

        do_gohome_tesk(_stage, &cmd_message, &cmd_twist);
        break;
    case 23:
        cmd_message.data.push_back((uint16_t)DiffModbus::MODE_STOP_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        cmd_message.data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        cmd_twist.linear.x = 0;
        cmd_twist.angular.z = 0;
        break;
    }

#ifdef _LOOP_TESK_DEBUG

    ROS_INFO_NAMED(DT_LOOP_TESK_DEBUG,"STAGE: %4d", _stage);

#endif // _LOOP_TESK_DEBUG

    // Publish the topic
    _diff_pub->publish(cmd_message);
    _diff_twist_pub->publish(cmd_twist);
}

bool stage_change_detect(int _stage) {
    // Count the times of robot distence over minimum limit
    static int laser_distence_overlimit_conter(0);
    static bool fg_usetimer(false);
    static ros::Time last_time = ros::Time::now();

    switch(_stage) {
    case 0:
        // Detect the start flag
        if(fg_start) {
            fg_usetimer = false;
            return true;
        }
        break;
    case 1:
        // Detect the timer timeout
        if(!fg_usetimer) {
            last_time = ros::Time::now();
            fg_usetimer = true;
        }

        if(fg_usetimer) {
            if(ros::Time::now().toSec() - last_time.toSec() > TASK_1_DURATION) {
                fg_usetimer = false;
                last_time = ros::Time::now();
                return true;
            }
        } else {
            ROS_ERROR("Stage ERROR %d",_stage);
        }

        break;
    case 2:
        // Detect the timer timeout
        if(!fg_usetimer) {
            last_time = ros::Time::now();
            fg_usetimer = true;
        }

        if(fg_usetimer) {
            if(ros::Time::now().toSec() - last_time.toSec() > TASK_2_DURATION) {
                fg_usetimer = false;
                last_time = ros::Time::now();
                return true;
            }
        } else {
            ROS_ERROR("Stage ERROR %d",_stage);
        }

        break;
    case 3:
        // Detect the Orient is cross at 0 degree
        last_time = ros::Time::now();
        ROS_INFO("Orient %f",orient);
        if(fabs(orient - M_PI * 0.0/180.0) < 0.1) return true;

        break;
    case 4:
        // Detect the Orient is cross at 0 degree and Timing from last stage large than 1 sec
        if(fabs(last_time.toSec() - ros::Time::now().toSec()) > 1.0 && fabs(orient - M_PI * 0.0/180.0) < 0.1) {
            last_time = ros::Time::now();
            return true;
        }
        break;
    case 5:
        // Detect the Orient is cross at 0 degree and Timing from last stage large than 1 sec
        if(fabs(last_time.toSec() - ros::Time::now().toSec()) > 1.0 && fabs(orient - M_PI * 0.0/180.0) < 0.1) {
            last_time = ros::Time::now();
            return true;
        }
        break;
    case 6:
        // Detect the Orient is cross at 0 degree and Timing from last stage large than 1 sec
        if(fabs(last_time.toSec() - ros::Time::now().toSec()) > 1.0 && fabs(orient - M_PI * -45.0/180.0) < 0.1) {
            last_time = ros::Time::now();
            return true;
        }
        break;
    case 7:
        // Detect the Orient is stable at 90 degree and no line
        if(!stable_detector.isStarted()) {
            stable_detector.init(M_PI * 90.0/180);
            stable_detector.start();
        } else {
            stable_detector.update();
        }
        if(stable_detector.isConverged()  && is_sensor_noline_all_black() ) {
            stable_detector.stop();
            return true;
        }

        break;
    case 8:
        // Detect the Orient is stable at 90 degree and Distence is less than 0.3 m
        if(!stable_detector.isStarted()) {
            stable_detector.init(M_PI * 90.0/180);
            stable_detector.start();
        } else {
            stable_detector.update();
        }
        if(front_length < TASK_8_LENGTH_FRONT)
            laser_distence_overlimit_conter ++;

        // If there the distence less than 0.3 m too many times, we have confidence say that we need turning now
        if(stable_detector.isConverged()  && laser_distence_overlimit_conter > 3 ) {
            laser_distence_overlimit_conter = 0;
            stable_detector.stop();
            return true;
        }

        break;

    case 9:
        // Detect the Orient is stable at 180 degree
        if(!stable_detector.isStarted()) {
            stable_detector.init(M_PI * 180.0/180);
            stable_detector.start();
        } else {
            stable_detector.update();
        }
        if(stable_detector.isConverged()) {
            stable_detector.stop();
            return true;
        }
        break;
    case 10:
        // Detect all Black change to White
        if(!wb_detector.isStarted()) {
            wb_detector.init(false);
            wb_detector.start();
        } else {
            wb_detector.update();
        }
        if(wb_detector.isDetected()) {
            wb_detector.stop();
            return true;
        }
        break;
    case 11:
        if(!wb_detector.isStarted()) {
            wb_detector.init(true);
            wb_detector.start();
        } else {
            wb_detector.update();
        }
        if(wb_detector.isDetected()) {
            wb_detector.stop();
            if(fg_need_bulb) {
                fg_usetimer = true;
                last_time = ros::Time::now();
            }
            return true;
        }
        break;
    case 110:
        if(fg_usetimer) {
            if(ros::Time::now().toSec() - last_time.toSec() >  TASK_110_DURATION) {
                last_time = ros::Time::now();
                return true;
            }
        } else {
            ROS_ERROR("Stage ERROR %d",_stage);
        }
        break;
    case 111:
        if(fg_usetimer) {
            if(ros::Time::now().toSec() - last_time.toSec() > TASK_111_DURATION) {
                last_time = ros::Time::now();
                return true;
            }
        } else {
            ROS_ERROR("Stage ERROR %d",_stage);
        }
        break;
    case 112:
        if(fg_usetimer) {
            if(ros::Time::now().toSec() - last_time.toSec() > TASK_112_DURATION) {
                fg_usetimer = false;
                last_time = ros::Time::now();
                return true;
            }
        } else {
            ROS_ERROR("Stage ERROR %d",_stage);
        }
        break;
    case 113:
        // Detect all white change to black
        if(!wb_detector.isStarted()) {
            wb_detector.init(true);
            wb_detector.start();
        } else {
            wb_detector.update();
        }
        if(wb_detector.isDetected()) {
            fg_usetimer = true;
            last_time = ros::Time::now();
            wb_detector.stop();
            return true;
        }

        break;
    case 114:
        if(fg_usetimer) {
            if(ros::Time::now().toSec() - last_time.toSec() > TASK_114_DURATION) {
                fg_usetimer = false;
                last_time = ros::Time::now();
                return true;
            }
        } else {
            ROS_ERROR("Stage ERROR %d",_stage);
        }
        break;
    case 12:
        // Detect the Orient is stable at 0 degree and no line
        if(!stable_detector.isStarted()) {
            stable_detector.init(M_PI * 0.0/180);
            stable_detector.start();
        } else {
            stable_detector.update();
        }
        if(stable_detector.isConverged() && is_sensor_noline_all_black()) {
            stable_detector.stop();
            return true;
        }

        break;
    case 13:
        // Detect the Orient is stable at 0 degree and Distence is less than 0.5 m
        if(get_front_distence(-orient) < TASK_13_LENGTH_FRONT)
            laser_distence_overlimit_conter ++;

        // If there the distence less than 0.3 m too many times, we have confidence say that we need turning now
        if(laser_distence_overlimit_conter > 3 ) {
            laser_distence_overlimit_conter = 0;
            stable_detector.stop();
            return true;
        }
        break;
    case 14:
        // Detect the Orient is stable at 90 degree
        if(!stable_detector.isStarted()) {
            stable_detector.init(M_PI * 90.0/180);
            stable_detector.start();
        } else {
            stable_detector.update();
        }
        if(stable_detector.isConverged()) {
            stable_detector.stop();
            if(!fg_usetimer) last_time = ros::Time::now();
            fg_usetimer = true;
        }
        if(fg_usetimer) {
            if(ros::Time::now().toSec() - last_time.toSec() > TASK_14_1_DURATION) {
                fg_usetimer = false;
                last_time = ros::Time::now();
                return true;
            }
        } else {
            ROS_ERROR("Stage ERROR %d",_stage);
        }
        break;
    case 15:
        // Detect any white
        for(int i=0; i<SENSOR_REG_COUNT; i++) {
            if(sensor_value[i] < 30) {
                if(!fg_usetimer) last_time = ros::Time::now();
                fg_usetimer = true;
            }

        }
        if(fg_usetimer) {
            if(ros::Time::now().toSec() - last_time.toSec() > TASK_15_1_DURATION) {
                fg_usetimer = false;
                last_time = ros::Time::now();
                return true;
            }
        } else {
            ROS_ERROR("Stage ERROR %d",_stage);
        }
        break;
    case 16:
        // Detect the Orient is stable at 180 degree and Average turn to white
//        if(!stable_detector.isStarted()) {
//            stable_detector.init(M_PI * 180.0/180);
//            stable_detector.start();
//        } else {
//            stable_detector.update();
//        }
//        if(stable_detector.isConverged() && is_sensor_noline_all_black()) {
//            stable_detector.stop();
//            return true;
//        }
        if(fabs(orient - M_PI * 170.0/180.0) < 0.1) {
            return true;
        }

        break;
    case 17:
        // Detect the average is white
        if(get_sensor_average() < 30)
            return true;
        break;
    case 18:
        // Detect the Orient is stable at 180 degree and Average turn to black
        if(!stable_detector.isStarted()) {
            stable_detector.init(M_PI * 180.0/180);
            stable_detector.start();
        } else {
            stable_detector.update();
        }
        if(stable_detector.isConverged() && get_sensor_average() > 70.0) {
            stable_detector.stop();
            return true;
        }

        break;
    case 19:
        // Detect the timer timeout
        if(!fg_usetimer) {
            last_time = ros::Time::now();
            fg_usetimer = true;
        }

        if(fg_usetimer) {
            if(ros::Time::now().toSec() - last_time.toSec() > TASK_19_DURATION) {
                fg_usetimer = false;
                last_time = ros::Time::now();
                return true;
            }
        } else {
            ROS_ERROR("Stage ERROR %d",_stage);
        }
        break;
    case 20:
        // Detect the Orient is stable at -90 degree
        if(!stable_detector.isStarted()) {
            stable_detector.init(M_PI * -90.0/180);
            stable_detector.start();
        } else {
            stable_detector.update();
        }
        if(stable_detector.isConverged()) {
            stable_detector.stop();
            return true;
        }
        break;
    case 21:
        // Detect right sensor white
        if(sensor_value[12] < 50 && sensor_value[11] < 50) return true;
        break;
    case 22:
        fg_usetimer = true;
        last_time = ros::Time::now();
        return true;
    case 220:
        if(fg_usetimer) {
            if(ros::Time::now().toSec() - last_time.toSec() > TASK_220_DURATION) {
                last_time = ros::Time::now();
                return true;
            }
        } else {
            ROS_ERROR("Stage ERROR %d",_stage);
        }
        break;
    case 221:
        if(fg_usetimer) {
            if(ros::Time::now().toSec() - last_time.toSec() > TASK_221_DURATION) {
                last_time = ros::Time::now();
                return true;
            }
        } else {
            ROS_ERROR("Stage ERROR %d",_stage);
        }
        break;
    case 222:
        if(fg_usetimer) {
            if(ros::Time::now().toSec() - last_time.toSec() > TASK_222_DURATION) {
                last_time = ros::Time::now();
                return true;
            }
        } else {
            ROS_ERROR("Stage ERROR %d",_stage);
        }
        break;
    case 23:
        // Stay this stage
        break;

    }
#ifdef _STAGE_CHANGE_DETECT_DEBUG

    ROS_INFO_NAMED(DT_STAGE_CHANGE_DETECT,"Lopp T: %4d",_stage);

#endif // _STAGE_CHANGE_DETECT_DEBUG


    // Default False
    return false;
}

void do_button_tesk(int _stage, std_msgs::UInt16MultiArray* _mode_msg, geometry_msgs::Twist* _twist_msg) {
    switch(_stage) {
    case 110:
        _mode_msg->data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        _twist_msg->linear.x = -30;
        _twist_msg->angular.z = M_PI * 180.0/180.0;
        break;
    case 111:
        _mode_msg->data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        _twist_msg->linear.x = 0;
        _twist_msg->angular.z = M_PI * 150.0/180.0;
        break;
    case 112:
        _mode_msg->data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        _twist_msg->linear.x = 0;
        _twist_msg->angular.z = M_PI * 180/180.0;
        break;
    case 113:
        _mode_msg->data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        _twist_msg->linear.x = 30;
        _twist_msg->angular.z = M_PI * 180/180.0;
        break;
    case 114:
        _mode_msg->data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        _twist_msg->linear.x = 30;
        _twist_msg->angular.z = M_PI * -170/180.0;
        break;
    }
    return;

}

void do_gohome_tesk(int _stage, std_msgs::UInt16MultiArray* _mode_msg, geometry_msgs::Twist* _twist_msg) {
    switch(_stage) {
    case 22:
    case 220:
        _mode_msg->data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        _twist_msg->linear.x = 40;
        _twist_msg->angular.z = M_PI * -90.0/180.0;
        break;
    case 221:
        _mode_msg->data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        _twist_msg->linear.x = 0;
        _twist_msg->angular.z = M_PI * 0.0/180.0;
        break;
    case 222:
        _mode_msg->data.push_back((uint16_t)DiffModbus::MODE_CONTROLLABLE_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::TORQUE_MED_CMD);
        _mode_msg->data.push_back((uint16_t)DiffModbus::WHITE_CMD);
        _twist_msg->linear.x = -40;
        _twist_msg->angular.z = M_PI * 0.0/180.0;
        break;
    }
    return;
}

bool is_sensor_noline_all_black() {
    //int sum(0);
    int count_read_black(0);
    for(int i=0; i<SENSOR_REG_COUNT; i++) {
        //sum += sensor_value[i];
        if(sensor_value[i] > SENSOR_BLACK_THROSHOLD)
            count_read_black ++;
    }
    ROS_INFO("SENSOR VALUE[0] = %d", sensor_value);
    //ROS_INFO("NOLINE DEBUG sum = %d", sum);
    ROS_INFO("NOLINE SENSOR COUNT %d", count_read_black);
    //if(sum > NO_LINE_THROSHOLD && count_read_black == SENSOR_REG_COUNT)
    //if(sum > NO_LINE_THROSHOLD)
    if(count_read_black >= SENSOR_REG_COUNT-1)
        return true;
    else
        return false;
}

bool is_sensor_noline_all_white() {
    //int sum(0);
    int count_read_white(0);
    for(int i=0; i<SENSOR_REG_COUNT; i++) {
        //sum += sensor_value[i];
        if(sensor_value[i] < SENSOR_WHITE_THROSHOLD)
            count_read_white ++;
    }
    ROS_INFO("SENSOR VALUE[0] = %d", sensor_value);
    //ROS_INFO("NOLINE DEBUG sum = %d", sum);
    ROS_INFO("NOLINE SENSOR COUNT %d", count_read_white);
    //if(sum > NO_LINE_THROSHOLD && count_read_black == SENSOR_REG_COUNT)
    //if(sum > NO_LINE_THROSHOLD)
    if(count_read_white >= SENSOR_REG_COUNT)
        return true;
    else
        return false;
}

double get_sensor_average() {
    int sum(0);
    for(int i=0; i<SENSOR_REG_COUNT; i++) {
        sum += sensor_value[i];
    }
    return sum / SENSOR_REG_COUNT;
}

double cot_angle(double _degree) {

    if( _degree < 0.0) {
        double times_ = fabs(_degree) / (2*M_PI);
        return ( fmod( _degree + ( 2*M_PI * (floor(times_)+1) ) + M_PI  ,2*M_PI) - M_PI);
    } else if ( _degree > 0.0) {
        return ( fmod( _degree + M_PI ,2*M_PI) - M_PI);
    } else {
        return 0.0;
    }

}

float get_laser_distence(float _angle) {

    float _ref_orient_increment_diff = _angle - laser_angle_min;
    float _ref_count = _ref_orient_increment_diff / laser_angle_increment;

    if( _ref_count < 0)
        _ref_count = 0;
    else if( _ref_count > laser_count_max)
        _ref_count = laser_count_max;

    return laser_msg.ranges.at(_ref_count);
}

float get_right_distence(float _orient_offset) { return get_laser_distence( -M_PI/2 + _orient_offset); }

float get_left_distence(float _orient_offset) { return get_laser_distence( M_PI/2 + _orient_offset); }

float get_front_distence(float _orient_offset) { return get_laser_distence( _orient_offset ); }


