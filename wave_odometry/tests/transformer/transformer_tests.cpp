#include "wave/wave_test.hpp"
#include "wave/odometry/transformer.hpp"
#include "wave/matching/pointcloud_display.hpp"

namespace wave{

TEST(Transformer, Constructor) {
    TransformerParams params;
    Transformer transformer(params);
}

/// Just test no segfaults when interpolating transform
TEST(Transformer, update) {
    std::vector<PoseVel, Eigen::aligned_allocator<PoseVel>> trajectory;
    std::vector<float> stamps;

    TransformerParams params;
    params.n_scans = 4;
    params.traj_resolution = 3;
    uint32_t cnt = params.n_scans * (params.traj_resolution - 1) + 1;

    Vec6 acc;
    acc << 0, 0, 1.5, 3, 0, 0;

    stamps.resize(cnt);
    trajectory.resize(cnt);

    for(uint32_t i = 0; i < cnt; i++) {
        stamps.at(i) = static_cast<float>(i) * 0.1f;
        if (i != 0) {
            trajectory.at(i).pose = trajectory.at(i - 1).pose;
            trajectory.at(i).pose.manifoldPlus(trajectory.at(i-1).vel * 0.1);
        }
        trajectory.at(i).vel = static_cast<double>(i) * 0.1 * acc;
    }
    Transformer transformer(params);
    transformer.update(trajectory, stamps);
}

/// Each scan consists of 100 points sampled from a circle
TEST(Transformer, transformToStart) {
    std::vector<PoseVel, Eigen::aligned_allocator<PoseVel>> trajectory;
    std::vector<float> stamps;

    TransformerParams params;
    params.n_scans = 4;
    params.traj_resolution = 3;
    uint32_t cnt = params.n_scans * (params.traj_resolution - 1) + 1;

    Vec6 acc;
    acc << 0, 0, 1.5, 3, 0, 0;

    stamps.resize(cnt);
    trajectory.resize(cnt);

    for(uint32_t i = 0; i < cnt; i++) {
        stamps.at(i) = static_cast<float>(i) * 0.1f;
        if (i != 0) {
            trajectory.at(i).pose = trajectory.at(i - 1).pose;
            trajectory.at(i).pose.manifoldPlus(trajectory.at(i-1).vel * 0.1);
        }
        trajectory.at(i).vel = static_cast<double>(i) * 0.1 * acc;
    }
    Transformer transformer(params);
    transformer.update(trajectory, stamps);

    long n_pts = 100;
    Eigen::Tensor<float, 2> scan(4, n_pts);
    MatXf tscan;
    for (long i = 0; i < n_pts; i++) {
        scan(0,i) = static_cast<float>(std::cos(2*M_PI * static_cast<double>(i)/100.0));
        scan(1,i) = static_cast<float>(std::sin(2*M_PI * static_cast<double>(i)/100.0));
        scan(2,i) = 0.0f;
        scan(3,i) = 0.4f + 0.2f * (static_cast<float>(i)/100.0f);
    }
    EXPECT_NO_THROW(transformer.transformToStart(scan, tscan, 0));
}

/// Given a set of points in a circle, this should stretch/transform it smoothly across trajectory
/// Sanity check
TEST(Transformer, transformViz) {
    std::vector<PoseVel, Eigen::aligned_allocator<PoseVel>> trajectory;
    std::vector<float> stamps;

    TransformerParams params;
    params.n_scans = 4;
    params.traj_resolution = 3;
//    params.delRTol = 1e-3;
    uint32_t cnt = params.n_scans * (params.traj_resolution - 1) + 1;

    Vec6 vel;
    vel << 0, 0, 0, 3, 3, 0;

    stamps.resize(cnt);
    trajectory.resize(cnt);

    for(uint32_t i = 0; i < cnt; i++) {
        stamps.at(i) = static_cast<float>(i) * 0.1f;
        if (i != 0) {
//            trajectory.at(i).pose = trajectory.at(i - 1).pose;
//            trajectory.at(i).pose.manifoldPlus(vel * 0.1);
            trajectory.at(i).pose.setIdentity();
        }
        if (i % 2 == 0) {
            vel << 0, 0, 0, 3, 3, 0;
        } else {
            vel << 0, 0, 0, 3, -3, 0;
        }
        trajectory.at(i).vel = vel;
    }
    Transformer transformer(params);
    transformer.update(trajectory, stamps);

    long n_pts = 100;
    Eigen::Tensor<float, 2> scan(4, params.n_scans * n_pts);
    MatXf tscan, bscan;

    for (long j = 0; j < params.n_scans; j++) {
        for (long i = 0; i < n_pts; i++) {
            scan(0,j*n_pts + i) = static_cast<float>(2.0);
            scan(1,j*n_pts + i) = static_cast<float>(2.0);
            scan(2,j*n_pts + i) = 0.1f * (float)j + 0.1f * (static_cast<float>(i)/100.0f);
            scan(3,j*n_pts + i) = 0.2f * (static_cast<float>(i)/100.0f);
        }
    }

    transformer.transformToStart(scan, tscan, 0);
    transformer.transformToEnd(scan, bscan, 0);

    pcl::PointCloud<pcl::PointXYZI> transformed, original, btransformed;
    for (long i = 0; i < params.n_scans * n_pts; i++) {
        pcl::PointXYZI tpt, orpt, bpt;
        tpt.x = tscan(0,i);
        tpt.y = tscan(1,i);
        tpt.z = tscan(2,i);
        tpt.intensity = scan(3,i);

        bpt.x = bscan(0,i);
        bpt.y = bscan(1,i);
        bpt.z = bscan(2,i);
        bpt.intensity = scan(3,i);

        orpt.x = scan(0,i);
        orpt.y = scan(1,i);
        orpt.z = scan(2,i);
        orpt.intensity = scan(3,i);

        transformed.push_back(tpt);
        btransformed.push_back(bpt);
        original.push_back(orpt);
    }

    /// The "undistorted" scans should only be a rigid transform apart. Test with constant transform function
    // Transforming points in the frame at the start to the frame at the end
    MatXf btscan;

    transformer.constantTransform(0, params.n_scans - 1, tscan, btscan);

    MatXf error = btscan - bscan;

    EXPECT_NEAR(error.cwiseAbs().maxCoeff(), 0.0f, 1e-6);

    PointCloudDisplay display("warped");
    display.startSpin();
    display.addPointcloud(transformed.makeShared(), 0);
    display.addPointcloud(original.makeShared(), 1);
    display.addPointcloud(btransformed.makeShared(), 2);

    std::this_thread::sleep_for(std::chrono::seconds(10));
}

}
