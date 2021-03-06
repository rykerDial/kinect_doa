#include "KinectDOA.h"
#include <algorithm>
#include <std_msgs/Float32.h>

KinectDOA::KinectDOA(ros::NodeHandle nh) : m_nh(nh), m_sample_freq(16000), m_last_angle_estimate(0) {
	// Approximate linear coordinates of the Kinect's built in microphones relative to the center of the device (in meters).
	//     Source: http://giampierosalvi.blogspot.com/2013/12/ms-kinect-microphone-array-geometry.html
	m_mic_positions[0] = 0.113;
	m_mic_positions[1] = -0.036;
	m_mic_positions[2] = -0.076;
	m_mic_positions[3] = -0.113;

	// Retrieve speed of sound if specified
	ros::NodeHandle pr_nh("~");
	pr_nh.param<double>("sound_speed", m_sound_speed, 340);
	pr_nh.param<double>("white_noise_ratio", m_white_noise_ratio, 0.65);
	pr_nh.param<int>("numsamples_xcor", m_numsamples_xcor, 8192);

	m_servo_angle_pub = m_nh.advertise<std_msgs::Float32>("servo_angle", 1);

	// Compute the maximum width of our cross-correlation as limited by array
	//     geometry, the speed of sound, and our sampling frequency
	m_max_lag = fabs(m_mic_positions[0] - m_mic_positions[3])/m_sound_speed*m_sample_freq;

	// Initialize containers for microphone data
	for(size_t i=0; i<4; ++i) {
		m_xcor_data.push_back(new double[m_numsamples_xcor]);
		m_sumd0.push_back(0);
		m_sumd1.push_back(0);
	}
}

KinectDOA::~KinectDOA() {
	for(size_t i=0; i<m_xcor_data.size(); ++i) {
		delete [] m_xcor_data[i];
	}
}

bool KinectDOA::isNoise() {
	for(int i=0; i<4; ++i) {
		if(((double)m_sumd1[i])/m_sumd0[i] < m_white_noise_ratio) {
			return false;
		}
	}
	return true;
}

double KinectDOA::findAngle() {
	// Compute angle if significantly different than noise.
	if(!isNoise()) {
		std::vector<std::pair<int, double>> delays_and_x;

		// Compute cross correlation of mic 1 data with data from mics 2, 3, and 4.
		//     It doesn't make sense to correlate other pairs since they're so physically close.
		for(int i=1; i<4; ++i) {
			int delay = MD_get_delay(m_xcor_data[0], m_xcor_data[i], m_numsamples_xcor, NULL, m_max_lag, PHAT);
			delays_and_x.push_back(std::make_pair(delay, fabs(m_mic_positions[0]-m_mic_positions[i])));
		}

		// Sort delays.
		std::sort(delays_and_x.begin(), delays_and_x.end(),
			[](const std::pair<int, double> & a, const std::pair<int, double> & b) -> bool {
				return b.first < a.first;
			}
		);

		// Determine the angle with the median lag.
		double sin_angle = delays_and_x[1].first*m_sound_speed/(m_sample_freq*delays_and_x[1].second);	
		// Clamp the angle.
		if(sin_angle > 1) m_last_angle_estimate =  90.0;
		else if(sin_angle < -1) m_last_angle_estimate = -90.0;
		else m_last_angle_estimate = -asin(sin_angle)*180/M_PI;
		ROS_INFO("New Angle Estimate: %lf\n", m_last_angle_estimate);

		std_msgs::Float32 servo_angle_msg;
		servo_angle_msg.data = m_last_angle_estimate;
		m_servo_angle_pub.publish(servo_angle_msg);
	}

	return m_last_angle_estimate;
}
