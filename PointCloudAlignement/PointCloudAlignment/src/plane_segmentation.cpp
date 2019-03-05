#include "plane_segmentation.h"

int PlaneSegmentation::init(string cloud_file)
{
    p_cloud = PointCloud::Ptr(new PointCloud);

    int r = pcl::io::loadPCDFile(cloud_file, *p_cloud);

    if(r == -1 && pcl::io::loadPLYFile(cloud_file, *p_cloud) == -1)
    {
        PCL_ERROR("Could not read given file\n");
        return EXIT_FAILURE;
    }

    cout << "Pointcloud containing " << p_cloud->points.size() << " points loaded." << endl;

    // Fill kdtree search strucuture


    cout << "Starting to compute normal cloud..." << endl;

    p_normals = NormalCloud::Ptr(new NormalCloud);

    NormalComputation nc;
    nc.computeNormalCloud(p_cloud, p_kdtree, p_normals);

    is_ready = true;
}

bool PlaneSegmentation::isReady()
{
    return this->is_ready;
}

void PlaneSegmentation::start_pause()
{
    if(!is_ready) {
        cout << "Can't start algorithm, not initialized properly" << endl;
        return;
    }

    is_started = !is_started;
}

void PlaneSegmentation::stop()
{
    is_started = false;
}
