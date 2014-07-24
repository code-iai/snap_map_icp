/*
 * Copyright (c) 2010, Thomas Ruehr <ruehr@cs.tum.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <ros/ros.h>

#include <std_msgs/String.h>
#include <sensor_msgs/PointCloud.h>
#include <sensor_msgs/PointCloud2.h>
#include <nav_msgs/OccupancyGrid.h>
//for point_cloud::fromROSMsg
#include <pcl/ros/conversions.h>
#include <pcl_conversions/pcl_conversions.h>
#include <sensor_msgs/point_cloud_conversion.h>
#include <sensor_msgs/LaserScan.h>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <laser_geometry/laser_geometry.h>
#include <tf/transform_listener.h>

#include <pcl/registration/icp.h>
#include <pcl/registration/icp_nl.h>
#include <pcl/registration/transforms.h>
#include <pcl/kdtree/kdtree_flann.h>

#include <geometry_msgs/PoseWithCovarianceStamped.h>

#include <boost/thread/mutex.hpp>

boost::mutex scan_callback_mutex;

//these should be parameters
// defines how good the match has to be to create a candidate for publishing a pose
double ICP_FITNESS_THRESHOLD = 100.1;// =  0.025;
//defines how much distance deviation from amcl to icp pose is needed to make us publish a pose
double DIST_THRESHOLD = 0.05;
// same for angle
double ANGLE_THRESHOLD = 0.01;
double ANGLE_UPPER_THRESHOLD = M_PI / 6;
// accept only scans one second old or younger
double AGE_THRESHOLD = 1;
double UPDATE_AGE_THRESHOLD = 1;

double ICP_INLIER_THRESHOLD = 0.9;
double ICP_INLIER_DIST = 0.1;

double POSE_COVARIANCE_TRANS = 1.5;
double ICP_NUM_ITER = 250;

double SCAN_RATE = 2;

std::string BASE_LASER_FRAME = "/base_laser_link";
std::string ODOM_FRAME = "/odom_combined";

ros::NodeHandle *nh = 0;
ros::Publisher pub_output_;
ros::Publisher pub_output_scan;
ros::Publisher pub_output_scan_transformed;
ros::Publisher pub_info_;

ros::Publisher pub_pose;


laser_geometry::LaserProjection *projector_ = 0;
tf::TransformListener *listener_ = 0;
sensor_msgs::PointCloud2 cloud2;
sensor_msgs::PointCloud2 cloud2transformed;

typedef pcl::PointCloud<pcl::PointXYZ> PointCloudT;

boost::shared_ptr< sensor_msgs::PointCloud2> output_cloud = boost::shared_ptr<sensor_msgs::PointCloud2>(new sensor_msgs::PointCloud2());
boost::shared_ptr< sensor_msgs::PointCloud2> scan_cloud = boost::shared_ptr<sensor_msgs::PointCloud2>(new sensor_msgs::PointCloud2());

bool we_have_a_map = false;
bool we_have_a_scan = false;
bool we_have_a_scan_transformed = false;

bool use_sim_time = false;

int lastScan = 0;
int actScan = 0;

/*inline void
  pcl::transformAsMatrix (const tf::Transform& bt, Eigen::Matrix4f &out_mat)
{
  double mv[12];
  bt.getBasis ().getOpenGLSubMatrix (mv);

  tf::Vector3 origin = bt.getOrigin ();

  out_mat (0, 0) = mv[0]; out_mat (0, 1) = mv[4]; out_mat (0, 2) = mv[8];
  out_mat (1, 0) = mv[1]; out_mat (1, 1) = mv[5]; out_mat (1, 2) = mv[9];
  out_mat (2, 0) = mv[2]; out_mat (2, 1) = mv[6]; out_mat (2, 2) = mv[10];

  out_mat (3, 0) = out_mat (3, 1) = out_mat (3, 2) = 0; out_mat (3, 3) = 1;
  out_mat (0, 3) = origin.x ();
  out_mat (1, 3) = origin.y ();
  out_mat (2, 3) = origin.z ();
}*/

inline void
matrixAsTransfrom (const Eigen::Matrix4f &out_mat,  tf::Transform& bt)
{
    double mv[12];

    mv[0] = out_mat (0, 0) ;
    mv[4] = out_mat (0, 1);
    mv[8] = out_mat (0, 2);
    mv[1] = out_mat (1, 0) ;
    mv[5] = out_mat (1, 1);
    mv[9] = out_mat (1, 2);
    mv[2] = out_mat (2, 0) ;
    mv[6] = out_mat (2, 1);
    mv[10] = out_mat (2, 2);

    tf::Matrix3x3 basis;
    basis.setFromOpenGLSubMatrix(mv);
    tf::Vector3 origin(out_mat (0, 3),out_mat (1, 3),out_mat (2, 3));

    ROS_DEBUG("origin %f %f %f", origin.x(), origin.y(), origin.z());

    bt = tf::Transform(basis,origin);
}


boost::shared_ptr<pcl::PointCloud<pcl::PointXYZ> > cloud_xyz;

pcl::KdTree<pcl::PointXYZ>::Ptr mapTree;


pcl::KdTree<pcl::PointXYZ>::Ptr getTree(pcl::PointCloud<pcl::PointXYZ>::Ptr cloudb)
{
    pcl::KdTree<pcl::PointXYZ>::Ptr tree;
    tree.reset (new pcl::KdTreeFLANN<pcl::PointXYZ>);

    tree->setInputCloud (cloudb);
    return tree;
}


void mapCallback(const nav_msgs::OccupancyGrid& msg)
{
    ROS_DEBUG("I heard frame_id: [%s]", msg.header.frame_id.c_str());

    float resolution = msg.info.resolution;
    float width = msg.info.width;
    float height = msg.info.height;

    float posx = msg.info.origin.position.x;
    float posy = msg.info.origin.position.y;

    cloud_xyz = boost::shared_ptr< pcl::PointCloud<pcl::PointXYZ> >(new pcl::PointCloud<pcl::PointXYZ>());

    //cloud_xyz->width    = 100; // 100
    cloud_xyz->height   = 1;
    cloud_xyz->is_dense = false;
    std_msgs::Header header;
    header.stamp = ros::Time(0);
    header.frame_id = "/map";
    cloud_xyz->header = pcl_conversions::toPCL(header);

    pcl::PointXYZ point_xyz;

    //for (unsigned int i = 0; i < cloud_xyz->width ; i++)
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
        {
            //@TODO
            if (msg.data[x + y * width] == 100)
            {
                point_xyz.x = (.5f + x) * resolution + posx;
                point_xyz.y = (.5f + y) * resolution + posy;
                point_xyz.z = 0;
                cloud_xyz->points.push_back(point_xyz);
            }
        }
    cloud_xyz->width = cloud_xyz->points.size();

    mapTree = getTree(cloud_xyz);

    pcl::toROSMsg (*cloud_xyz, *output_cloud);
    ROS_DEBUG("Publishing PointXYZ cloud with %ld points in frame %s", cloud_xyz->points.size(),output_cloud->header.frame_id.c_str());

    we_have_a_map = true;
}


int lastTimeSent = -1000;

int count_sc_ = 0;

bool getTransform(tf::StampedTransform &trans , const std::string parent_frame, const std::string child_frame, const ros::Time stamp)
{
    bool gotTransform = false;

    ros::Time before = ros::Time::now();
    if (!listener_->waitForTransform(parent_frame, child_frame, stamp, ros::Duration(0.5)))
    {
        ROS_WARN("DIDNT GET TRANSFORM %s %s IN c at %f", parent_frame.c_str(), child_frame.c_str(), stamp.toSec());
        return false;
    }
    //ROS_INFO("waited for transform %f", (ros::Time::now() - before).toSec());

    try
    {
        gotTransform = true;
        listener_->lookupTransform(parent_frame,child_frame,stamp , trans);
    }
    catch (tf::TransformException ex)
    {
        gotTransform = false;
        ROS_WARN("DIDNT GET TRANSFORM %s %s IN B", parent_frame.c_str(), child_frame.c_str());
    }


    return gotTransform;
}

ros::Time last_processed_scan;

void scanCallback (const sensor_msgs::LaserScan::ConstPtr& scan_in)
{
    if (!we_have_a_map)
    {
        ROS_INFO("SnapMapICP waiting for map to be published");
        return;
    }

    ros::Time scan_in_time = scan_in->header.stamp;
    ros::Time time_received = ros::Time::now();

    if ( scan_in_time - last_processed_scan < ros::Duration(1.0f / SCAN_RATE) )
    {
        ROS_DEBUG("rejected scan, last %f , this %f", last_processed_scan.toSec() ,scan_in_time.toSec());
        return;
    }


    //projector_.transformLaserScanToPointCloud("base_link",*scan_in,cloud,listener_);
    if (!scan_callback_mutex.try_lock())
        return;

    ros::Duration scan_age = ros::Time::now() - scan_in_time;

    //check if we want to accept this scan, if its older than 1 sec, drop it
    if (!use_sim_time)
        if (scan_age.toSec() > AGE_THRESHOLD)
        {
            //ROS_WARN("SCAN SEEMS TOO OLD (%f seconds, %f threshold)", scan_age.toSec(), AGE_THRESHOLD);
            ROS_WARN("SCAN SEEMS TOO OLD (%f seconds, %f threshold) scan time: %f , now %f", scan_age.toSec(), AGE_THRESHOLD, scan_in_time.toSec(),ros::Time::now().toSec() );
            scan_callback_mutex.unlock();

            return;
        }

    count_sc_++;
    //ROS_DEBUG("count_sc %i MUTEX LOCKED", count_sc_);

    //if (count_sc_ > 10)
    //if (count_sc_ > 10)
    {
        count_sc_ = 0;

        tf::StampedTransform base_at_laser;
        if (!getTransform(base_at_laser, ODOM_FRAME, "base_link", scan_in_time))
        {
            ROS_WARN("Did not get base pose at laser scan time");
            scan_callback_mutex.unlock();

            return;
        }


        sensor_msgs::PointCloud cloud;
        sensor_msgs::PointCloud cloudInMap;

        projector_->projectLaser(*scan_in,cloud);

        we_have_a_scan = false;
        bool gotTransform = false;

        if (!listener_->waitForTransform("/map", cloud.header.frame_id, cloud.header.stamp, ros::Duration(0.05)))
        {
            scan_callback_mutex.unlock();
            ROS_WARN("SnapMapICP no map to cloud transform found MUTEX UNLOCKED");
            return;
        }

        if (!listener_->waitForTransform("/map", "/base_link", cloud.header.stamp, ros::Duration(0.05)))
        {
            scan_callback_mutex.unlock();
            ROS_WARN("SnapMapICP no map to base transform found MUTEX UNLOCKED");
            return;
        }


        while (!gotTransform && (ros::ok()))
        {
            try
            {
                gotTransform = true;
                listener_->transformPointCloud ("/map",cloud,cloudInMap);
            }
            catch (...)
            {
                gotTransform = false;
                ROS_WARN("DIDNT GET TRANSFORM IN A");
            }
        }

        for (size_t k =0; k < cloudInMap.points.size(); k++)
        {
            cloudInMap.points[k].z = 0;
        }


        gotTransform = false;
        tf::StampedTransform oldPose;
        while (!gotTransform && (ros::ok()))
        {
            try
            {
                gotTransform = true;
                listener_->lookupTransform("/map", "/base_link",
                                           cloud.header.stamp , oldPose);
            }
            catch (tf::TransformException ex)
            {
                gotTransform = false;
                ROS_WARN("DIDNT GET TRANSFORM IN B");
            }
        }
        if (we_have_a_map && gotTransform)
        {
            sensor_msgs::convertPointCloudToPointCloud2(cloudInMap,cloud2);
            we_have_a_scan = true;

            actScan++;

            //pcl::IterativeClosestPointNonLinear<pcl::PointXYZ, pcl::PointXYZ> reg;
            pcl::IterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ> reg;
            reg.setTransformationEpsilon (1e-6);
            // Set the maximum distance between two correspondences (src<->tgt) to 10cm
            // Note: adjust this based on the size of your datasets
            reg.setMaxCorrespondenceDistance(0.5);
            reg.setMaximumIterations (ICP_NUM_ITER);
            // Set the point representation

            //ros::Time bef = ros::Time::now();

            PointCloudT::Ptr myMapCloud (new PointCloudT());
            PointCloudT::Ptr myScanCloud (new PointCloudT());

            pcl::fromROSMsg(*output_cloud,*myMapCloud);
            pcl::fromROSMsg(cloud2,*myScanCloud);

            reg.setInputCloud(myScanCloud);
            reg.setInputTarget(myMapCloud);

            PointCloudT unused;
            int i = 0;

            reg.align (unused);

            const Eigen::Matrix4f &transf = reg.getFinalTransformation();
            tf::Transform t;
            matrixAsTransfrom(transf,t);

            //ROS_ERROR("proc time %f", (ros::Time::now() - bef).toSec());

            we_have_a_scan_transformed = false;
            PointCloudT transformedCloud;
            pcl::transformPointCloud (*myScanCloud, transformedCloud, reg.getFinalTransformation());

            double inlier_perc = 0;
            {
                // count inliers
                std::vector<int> nn_indices (1);
                std::vector<float> nn_sqr_dists (1);

                size_t numinliers = 0;

                for (size_t k = 0; k < transformedCloud.points.size(); ++k )
                {
                    if (mapTree->radiusSearch (transformedCloud.points[k], ICP_INLIER_DIST, nn_indices,nn_sqr_dists, 1) != 0)
                        numinliers += 1;
                }
                if (transformedCloud.points.size() > 0)
                {
                    //ROS_INFO("Inliers in dist %f: %zu of %zu percentage %f (%f)", ICP_INLIER_DIST, numinliers, transformedCloud.points.size(), (double) numinliers / (double) transformedCloud.points.size(), ICP_INLIER_THRESHOLD);
                    inlier_perc = (double) numinliers / (double) transformedCloud.points.size();
                }
            }

            last_processed_scan = scan_in_time;

            pcl::toROSMsg (transformedCloud, cloud2transformed);
            we_have_a_scan_transformed = true;

            double dist = sqrt((t.getOrigin().x() * t.getOrigin().x()) + (t.getOrigin().y() * t.getOrigin().y()));
            double angleDist = t.getRotation().getAngle();
            tf::Vector3 rotAxis  = t.getRotation().getAxis();
            t =  t * oldPose;

            tf::StampedTransform base_after_icp;
            if (!getTransform(base_after_icp, ODOM_FRAME, "base_link", ros::Time(0)))
            {
                ROS_WARN("Did not get base pose at now");
                scan_callback_mutex.unlock();

                return;
            }
            else
            {
                tf::Transform rel = base_at_laser.inverseTimes(base_after_icp);
                ROS_DEBUG("relative motion of robot while doing icp: %fcm %fdeg", rel.getOrigin().length(), rel.getRotation().getAngle() * 180 / M_PI);
                t= t * rel;
            }

            //ROS_DEBUG("dist %f angleDist %f",dist, angleDist);

            //ROS_DEBUG("SCAN_AGE seems to be %f", scan_age.toSec());
            char msg_c_str[2048];
            sprintf(msg_c_str,"INLIERS %f (%f) scan_age %f (%f age_threshold) dist %f angleDist %f axis(%f %f %f) fitting %f (icp_fitness_threshold %f)",inlier_perc, ICP_INLIER_THRESHOLD, scan_age.toSec(), AGE_THRESHOLD ,dist, angleDist, rotAxis.x(), rotAxis.y(), rotAxis.z(),reg.getFitnessScore(), ICP_FITNESS_THRESHOLD );
            std_msgs::String strmsg;
            strmsg.data = msg_c_str;

            //ROS_DEBUG("%s", msg_c_str);

            double cov = POSE_COVARIANCE_TRANS;

            //if ((actScan - lastTimeSent > UPDATE_AGE_THRESHOLD) && ((dist > DIST_THRESHOLD) || (angleDist > ANGLE_THRESHOLD)) && (angleDist < ANGLE_UPPER_THRESHOLD))
            //  if ( reg.getFitnessScore()  <= ICP_FITNESS_THRESHOLD )
            if ((actScan - lastTimeSent > UPDATE_AGE_THRESHOLD) && ((dist > DIST_THRESHOLD) || (angleDist > ANGLE_THRESHOLD)) && (inlier_perc > ICP_INLIER_THRESHOLD) && (angleDist < ANGLE_UPPER_THRESHOLD))
            {
                lastTimeSent = actScan;
                geometry_msgs::PoseWithCovarianceStamped pose;
                pose.header.frame_id = "map";
                pose.pose.pose.position.x = t.getOrigin().x();
                pose.pose.pose.position.y = t.getOrigin().y();

                tf::Quaternion quat = t.getRotation();
                //quat.setRPY(0.0, 0.0, theta);
                tf::quaternionTFToMsg(quat,pose.pose.pose.orientation);
                float factorPos = 0.03;
                float factorRot = 0.1;
                pose.pose.covariance[6*0+0] = (cov * cov) * factorPos;
                pose.pose.covariance[6*1+1] = (cov * cov) * factorPos;
                pose.pose.covariance[6*3+3] = (M_PI/12.0 * M_PI/12.0) * factorRot;
                ROS_DEBUG("i %i converged %i SCORE: %f", i,  reg.hasConverged (),  reg.getFitnessScore()  );
                ROS_DEBUG("PUBLISHING A NEW INITIAL POSE dist %f angleDist %f Setting pose: %.3f %.3f  [frame=%s]",dist, angleDist , pose.pose.pose.position.x  , pose.pose.pose.position.y , pose.header.frame_id.c_str());
                pub_pose.publish(pose);
                strmsg.data += " << SENT";
            }

            //ROS_INFO("processing time : %f", (ros::Time::now() - time_received).toSec());

            pub_info_.publish(strmsg);
            //ROS_DEBUG("map width %i height %i size %i, %s", myMapCloud.width, myMapCloud.height, (int)myMapCloud.points.size(), myMapCloud.header.frame_id.c_str());
            //ROS_DEBUG("scan width %i height %i size %i, %s", myScanCloud.width, myScanCloud.height, (int)myScanCloud.points.size(), myScanCloud.header.frame_id.c_str());
        }
    }
    scan_callback_mutex.unlock();
}


ros::Time paramsWereUpdated;


void updateParams()
{
    paramsWereUpdated = ros::Time::now();
    // nh.param<std::string>("default_param", default_param, "default_value");
    nh->param<bool>("USE_SIM_TIME", use_sim_time, false);
    nh->param<double>("SnapMapICP/icp_fitness_threshold", ICP_FITNESS_THRESHOLD, 100 );
    nh->param<double>("SnapMapICP/age_threshold", AGE_THRESHOLD, 1);
    nh->param<double>("SnapMapICP/angle_upper_threshold", ANGLE_UPPER_THRESHOLD, 1);
    nh->param<double>("SnapMapICP/angle_threshold", ANGLE_THRESHOLD, 0.01);
    nh->param<double>("SnapMapICP/update_age_threshold", UPDATE_AGE_THRESHOLD, 1);
    nh->param<double>("SnapMapICP/dist_threshold", DIST_THRESHOLD, 0.01);
    nh->param<double>("SnapMapICP/icp_inlier_threshold", ICP_INLIER_THRESHOLD, 0.88);
    nh->param<double>("SnapMapICP/icp_inlier_dist", ICP_INLIER_DIST, 0.1);
    nh->param<double>("SnapMapICP/icp_num_iter", ICP_NUM_ITER, 250);
    nh->param<double>("SnapMapICP/pose_covariance_trans", POSE_COVARIANCE_TRANS, 0.5);
    nh->param<double>("SnapMapICP/scan_rate", SCAN_RATE, 2);
    if (SCAN_RATE < .001)
        SCAN_RATE  = .001;
    //ROS_DEBUG("PARAM UPDATE");

}

int main(int argc, char** argv)
{

// Init the ROS node
    ros::init(argc, argv, "snapmapicp");
    ros::NodeHandle nh_;
    nh = &nh_;

    nh->param<std::string>("SnapMapICP/odom_frame", ODOM_FRAME, "/odom_combined");
    nh->param<std::string>("SnapMapICP/base_laser_frame", BASE_LASER_FRAME, "/base_laser_link");

    last_processed_scan = ros::Time::now();

    projector_ = new laser_geometry::LaserProjection();
    tf::TransformListener listener;
    listener_ = &listener;

    pub_info_ =  nh->advertise<std_msgs::String> ("SnapMapICP", 1);
    pub_output_ = nh->advertise<sensor_msgs::PointCloud2> ("map_points", 1);
    pub_output_scan = nh->advertise<sensor_msgs::PointCloud2> ("scan_points", 1);
    pub_output_scan_transformed = nh->advertise<sensor_msgs::PointCloud2> ("scan_points_transformed", 1);
    pub_pose = nh->advertise<geometry_msgs::PoseWithCovarianceStamped>("initialpose", 1);

    ros::Subscriber subMap = nh_.subscribe("map", 1, mapCallback);
    ros::Subscriber subScan = nh_.subscribe("base_scan", 1, scanCallback);

    ros::Rate loop_rate(5);

    listener_->waitForTransform("/base_link", "/map",
                                ros::Time(0), ros::Duration(30.0));

    listener_->waitForTransform(BASE_LASER_FRAME, "/map",
                                ros::Time(0), ros::Duration(30.0));

    ros::AsyncSpinner spinner(1);
    spinner.start();

    updateParams();

    ROS_INFO("SnapMapICP running.");


    while (ros::ok())
    {
        if (actScan > lastScan)
        {
            lastScan = actScan;
            // publish map as a pointcloud2
            //if (we_have_a_map)
            //   pub_output_.publish(Ŕ);
            // publish scan as seen as a pointcloud2
            //if (we_have_a_scan)
            //   pub_output_scan.publish(cloud2);
            // publish icp transformed scan
            if (we_have_a_scan_transformed)
                pub_output_scan_transformed.publish(cloud2transformed);
        }
        loop_rate.sleep();
        ros::spinOnce();

        if (ros::Time::now() - paramsWereUpdated > ros::Duration(1))
            updateParams();
    }

}
