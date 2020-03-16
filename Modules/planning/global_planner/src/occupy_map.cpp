#include <occupy_map.h>

namespace global_planner{

void Occupy_map::setparam(ros::NodeHandle& nh){
    nh.param("astar/resolution_astar", resolution_,  0.2);
    nh.param("astar/inflate", inflate_,  0.2);
//       /* ---------- map params ---------- */
    this->inv_resolution_ = 1.0 / resolution_;
//     // initialize size of buffer
    nh.param("map/map_size_x", map_size_3d_(0), 10.0);
    nh.param("map/map_size_y", map_size_3d_(1), 10.0);
    nh.param("map/map_size_z", map_size_3d_(2), 5.0);
    
    nh.param("map/origin_x", origin_(0), -5.0);
    nh.param("map/origin_y", origin_(1), -5.0);
    nh.param("map/origin_z", origin_(2), 0.0);

    nh.param("map/ceil_height_", ceil_height_, 4.9);
    nh.param("map/floor_height_", floor_height_, 0.1);
    inflate_cloud_pub_ = nh.advertise<sensor_msgs::PointCloud2>("/planning/global_inflate_cloud", 1);
}

void Occupy_map::init(void){
    for (int i = 0; i < 3; ++i)
        grid_size_(i) = ceil(map_size_3d_(i) / resolution_);

    occupancy_buffer_.resize(grid_size_(0) * grid_size_(1) * grid_size_(2));
    fill(occupancy_buffer_.begin(), occupancy_buffer_.end(), 0.0);

    min_range_ = origin_;
    max_range_ = origin_ + map_size_3d_;   
}


void Occupy_map::setEnvironment(const sensor_msgs::PointCloud2ConstPtr & global_point){
    global_env_ = global_point;
    has_global_point = true;
    inflate_point_cloud();
}




void Occupy_map::inflate_point_cloud(void){
    if(!has_global_point){
        printf("Occupy_map [inflate point cloud]: don't have global point, can't inflate!\n");
        return;
    }
    ros::Time time_start = ros::Time::now();
    pcl::PointCloud<pcl::PointXYZ> latest_global_cloud_;
    pcl::fromROSMsg(*global_env_, latest_global_cloud_);

    if ((int)latest_global_cloud_.points.size() == 0)  return;

    pcl::PointXYZ pt, pt_inf;
    Eigen::Vector3d p3d, p3d_inf;
    
    const int ifn = ceil(inflate_ * inv_resolution_);

    pcl::PointCloud<pcl::PointXYZ> cloud_inflate_vis_;
    cloud_inflate_vis_.clear();

     printf("point size: %d, ifn_num: %d\n", latest_global_cloud_.points.size(), ifn);

    for (size_t i = 0; i < latest_global_cloud_.points.size(); ++i) {
        pt = latest_global_cloud_.points[i];
        p3d(0) = pt.x;
        p3d(1) = pt.y;
        p3d(2) = pt.z;
        
        /* inflate the point */
        for (int x = -ifn; x <= ifn; ++x)
            for (int y = -ifn; y <= ifn; ++y)
                for (int z = -ifn; z <= ifn; ++z) {
                    p3d_inf(0) = pt_inf.x = pt.x + x * resolution_;
                    p3d_inf(1) = pt_inf.y = pt.y + y * resolution_;
                    p3d_inf(2) = pt_inf.z = pt.z + 0.5 * z * resolution_;

                    if(p3d_inf(2)>ceil_height_ || p3d_inf(2)<0) break;

                    cloud_inflate_vis_.push_back(pt_inf);

                    this->setOccupancy(p3d_inf, 1);
                }
    }

    cloud_inflate_vis_.width = cloud_inflate_vis_.points.size();
    cloud_inflate_vis_.height = 1;
    cloud_inflate_vis_.is_dense = true;
    cloud_inflate_vis_.header.frame_id = "map";

    sensor_msgs::PointCloud2 map_inflate_vis;
    pcl::toROSMsg(cloud_inflate_vis_, map_inflate_vis);

    inflate_cloud_pub_.publish(map_inflate_vis);

    /* ---------- add ceil  and floor---------- */
    if (ceil_height_ > 0.0) {
        for (double cx = min_range_(0); cx <= max_range_(1); cx += resolution_)
            for (double cy =min_range_(0); cy <= max_range_(1); cy += resolution_) {
                this->setOccupancy(Eigen::Vector3d(cx, cy, ceil_height_), 1);
            }
    }

    if (floor_height_ > 0.0) {
        for (double cx = min_range_(0); cx <= max_range_(1); cx += resolution_)
            for (double cy =min_range_(0); cy <= max_range_(1); cy += resolution_) {
                this->setOccupancy(Eigen::Vector3d(cx, cy, floor_height_), 1);
            }
    }

    printf("inflate global point take %f.\n",   (ros::Time::now()-time_start).toSec());
}


void Occupy_map::setOccupancy(Eigen::Vector3d pos, int occ) {
    if (occ != 1 && occ != 0) {
        std::cout << "occ value error!" << std::endl;
        return;
    }

    if (!isInMap(pos))
        return;

    Eigen::Vector3i id;
    posToIndex(pos, id);

    // (x, y, z) -> x*ny*nz + y*nz + z
    // cout << "..."
    //      << id(0) * grid_size_(1) * grid_size_(2) + id(1) * grid_size_(2) + id(2)
    //      << endl;
    // cout << "..." << occupancy_buffer_.size() << endl;
    occupancy_buffer_[id(0) * grid_size_(1) * grid_size_(2) + id(1) * grid_size_(2) + id(2)] = occ;


}

bool Occupy_map::isInMap(Eigen::Vector3d pos) {
    if (pos(0) < min_range_(0) + 1e-4 || pos(1) < min_range_(1) + 1e-4 || pos(2) < min_range_(2) + 1e-4) {
        // cout << "less than min range!" << endl;
        return false;
    }

    if (pos(0) > max_range_(0) - 1e-4 || pos(1) > max_range_(1) - 1e-4 || pos(2) > max_range_(2) - 1e-4) {
        // cout << "larger than max range!" << endl;
        return false;
    }

    return true;
}

void Occupy_map::posToIndex(Eigen::Vector3d pos, Eigen::Vector3i &id) {
    for (int i = 0; i < 3; ++i)
        id(i) = floor((pos(i) - origin_(i)) * inv_resolution_);
}

void Occupy_map::indexToPos(Eigen::Vector3i id, Eigen::Vector3d &pos) {
    for (int i = 0; i < 3; ++i)
        pos(i) = (id(i) + 0.5) * resolution_ + origin_(i);
}

int Occupy_map::getOccupancy(Eigen::Vector3d pos) {
    if (!isInMap(pos))
        return -1;

    Eigen::Vector3i id;
    posToIndex(pos, id);

    // (x, y, z) -> x*ny*nz + y*nz + z
    return occupancy_buffer_[id(0) * grid_size_(1) * grid_size_(2) + id(1) * grid_size_(2) + id(2)];
}

int Occupy_map::getOccupancy(Eigen::Vector3i id) {
    if (id(0) < 0 || id(0) >= grid_size_(0) || id(1) < 0 || id(1) >= grid_size_(1) || id(2) < 0 ||
        id(2) >= grid_size_(2))
        return -1;

    // (x, y, z) -> x*ny*nz + y*nz + z
    return occupancy_buffer_[id(0) * grid_size_(1) * grid_size_(2) + id(1) * grid_size_(2) + id(2)];
}

}
