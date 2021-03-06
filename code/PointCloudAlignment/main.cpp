#include <iostream>
#include <string>
#include <limits>

#include <omp.h>

#include <pcl/features/normal_3d_omp.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/common/transforms.h>

#include "common.h"
#include "plane_segmentation.h"
#include "plane_merging.h"
#include "mesh_segmentation.h"
#include "registration.h"

using namespace std;

static PlaneSegmentation pc_source_segmentation;
static PlaneSegmentation pc_target_segmentation;
static PlaneMerging pc_source_merger;
static PlaneMerging pc_target_merger;
static MeshSegmentation mesh_target_segmentation;
static MeshSegmentation mesh_source_segmentation;
static Registration registration;
static pcl::visualization::PCLVisualizer::Ptr p_viewer;

bool targetIsMesh = false;
bool sourceIsMesh = false;
bool isNormalDisplayed = false;
bool isDualViewDisplayed = false;
bool pc_source_has_changed = false;
bool pc_target_has_changed = false;
bool normal_cloud_changed = false;
bool refresh_target_mesh = false;
bool refresh_source_mesh = false;
bool target_is_segmented = false;
bool source_is_segmented = false;

int plane_nb = 0;

string target_mesh_filename;
string source_mesh_filename;
string target_cloud_filename;
string source_cloud_filename;

void keyboardCallback(const pcl::visualization::KeyboardEvent &event,
                      void* viewer_void)
{
    pcl::visualization::PCLVisualizer *viewer = static_cast<pcl::visualization::PCLVisualizer *> (viewer_void);

    if(event.getKeySym() == "l" && event.keyDown())
    {
        cout << "Segmentation started!" << endl;
        if(!sourceIsMesh)
        {
            pc_source_segmentation.start_pause();
        }

        if(!targetIsMesh)
        {
            pc_target_segmentation.start_pause();
        }
    }
    else if(event.getKeySym() == "F1" && event.keyDown())
    {
        if(!sourceIsMesh)
        {
            viewer->removePointCloud("source_point_cloud");
            pcl::visualization::PointCloudColorHandlerRGBField<PointNormalK> rgb(pc_source_segmentation.getPointCloud());
            viewer->addPointCloud(pc_source_segmentation.getPointCloud(), rgb, "source_point_cloud");
            viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "source_point_cloud");
        }

        if(!targetIsMesh)
        {
            viewer->removePointCloud("target_point_cloud");
            pcl::visualization::PointCloudColorHandlerRGBField<PointNormalK> rgb_target(pc_target_segmentation.getPointCloud());
            viewer->addPointCloud(pc_target_segmentation.getPointCloud(), rgb_target, "target_point_cloud");
            viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "target_point_cloud");
        }
    }
    else if(event.getKeySym() == "F2" && event.keyDown())
    {
        if(!sourceIsMesh)
        {
            viewer->removePointCloud("source_point_cloud");
            pcl::visualization::PointCloudColorHandlerGenericField<PointNormalK> curv(pc_source_segmentation.getPointCloud(), "curvature");
            viewer->addPointCloud(pc_source_segmentation.getPointCloud(), curv, "source_point_cloud");
            viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "source_point_cloud");
        }

        if(!targetIsMesh)
        {
            viewer->removePointCloud("target_point_cloud");
            pcl::visualization::PointCloudColorHandlerGenericField<PointNormalK> curv(pc_target_segmentation.getPointCloud(), "curvature");
            viewer->addPointCloud(pc_target_segmentation.getPointCloud(), curv, "target_point_cloud");
            viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "target_point_cloud");
        }
    }
    else if(event.getKeySym() == "F3" && event.keyDown())
    {
        viewer->removePointCloud("source_point_cloud");
        pcl::visualization::PointCloudColorHandlerGenericField<PointNormalK> k(pc_source_segmentation.getPointCloud(), "k");
        viewer->addPointCloud(pc_source_segmentation.getPointCloud(), k, "source_point_cloud");
        viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "source_point_cloud");
    }
    else if(event.getKeySym() == "F4" && event.keyDown())
    {
        if(isNormalDisplayed)
        {
            isNormalDisplayed = false;

            if(!sourceIsMesh)
            {
                viewer->removePointCloud("source_normal_cloud");
            }

            if(!targetIsMesh)
            {
                viewer->removePointCloud("target_normal_cloud");
            }
        }
        else
        {
            isNormalDisplayed = true;
            viewer->addPointCloudNormals<PointNormalK, PointNormalK>(pc_source_segmentation.getPointCloud(), pc_source_segmentation.getPointCloud(), 5, 1, "source_normal_cloud");
            viewer->addPointCloudNormals<PointNormalK, PointNormalK>(pc_target_segmentation.getPointCloud(), pc_target_segmentation.getPointCloud(), 5, 1, "target_normal_cloud");
        }
    }
    else if(event.getKeySym() == "F5" && event.keyDown())
    {
        #pragma omp parallel sections
        {
            #pragma omp section
            {
                if(sourceIsMesh && mesh_source_segmentation.loadMesh(source_mesh_filename))
                {
                    // Display target mesh
                    refresh_source_mesh = true;

                    // Start plane segmentation
                    mesh_source_segmentation.segmentPlanes();
                    mesh_source_segmentation.mergePlanes();

                    source_is_segmented = true;
                    refresh_source_mesh = true;
                }
            }

            #pragma omp section
            {
                if(targetIsMesh && mesh_target_segmentation.loadMesh(target_mesh_filename))
                {
                    refresh_target_mesh = true;

                    // Start plane segmentation
                    mesh_target_segmentation.segmentPlanes();
                    mesh_target_segmentation.mergePlanes();

                    target_is_segmented = true;
                    refresh_target_mesh = true;
                }
            }
        }
    }
    else if(event.getKeySym() == "F6" && event.keyDown())
    {

        if(!sourceIsMesh)
        {
            cout << "Saving Source in ply file." << endl;
            string filename = "mySourcePC.ply";

            // Writing PC to file
            pcl::io::savePLYFile(filename, *pc_source_segmentation.getPointCloud());
        }

        if(!targetIsMesh)
        {
            cout << "Saving Target in ply file." << endl;
            string filename = "myTargetPC.ply";

            pcl::io::savePLYFile(filename, *pc_target_segmentation.getPointCloud());
        }
    }
    else if(event.getKeySym() == "F7" && event.keyDown())
    {
        // Display source in red and target in green
        if(isDualViewDisplayed)
        {
            if(!sourceIsMesh)
            {
                viewer->removePointCloud("source_point_cloud");
                pcl::visualization::PointCloudColorHandlerRGBField<PointNormalK> rgb_source(pc_source_segmentation.getPointCloud());
                p_viewer->addPointCloud(pc_source_segmentation.getPointCloud(), rgb_source, "source_point_cloud");
                p_viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "source_point_cloud");
            }

            if(!targetIsMesh)
            {
                viewer->removePointCloud("target_point_cloud");
                pcl::visualization::PointCloudColorHandlerRGBField<PointNormalK> rgb_target(pc_target_segmentation.getPointCloud());
                p_viewer->addPointCloud(pc_target_segmentation.getPointCloud(), rgb_target, "target_point_cloud");
                p_viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "target_point_cloud");
            }

            isDualViewDisplayed = false;
        }
        else
        {
            if(!sourceIsMesh)
            {
                viewer->removePointCloud("source_point_cloud");
                PointNormalKCloud::Ptr red_c = pc_source_segmentation.getPointCloud();
                pcl::visualization::PointCloudColorHandlerCustom<PointNormalK> red(red_c, 255, 15, 15);
                viewer->addPointCloud(red_c, red, "source_point_cloud");
                viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "source_point_cloud");
            }

            if(!targetIsMesh)
            {
                viewer->removePointCloud("target_point_cloud");
                PointNormalKCloud::Ptr green_c = pc_target_segmentation.getPointCloud();
                pcl::visualization::PointCloudColorHandlerCustom<PointNormalK> green(green_c, 15, 255, 15);
                viewer->addPointCloud(green_c, green, "target_point_cloud");
                viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "target_point_cloud");
            }

            isDualViewDisplayed = true;
        }
    }
    else if(event.getKeySym() == "F8" && event.keyDown())
    {
        // Filter out points with high curvature
        float max_curvature = pc_source_segmentation.getCurvBound();

        cout << "Filtering out points with curvature greater than " << max_curvature << endl;

        pc_source_segmentation.filterOutCurvature(max_curvature);

        if(!targetIsMesh)
        {
            pc_target_segmentation.filterOutCurvature(max_curvature);
        }
    }
    else if(event.getKeySym() == "F9" && event.keyDown())
    {
    	// Preprocess cloud
        #pragma omp parallel sections
        {
            #pragma omp section
            {
                if(!sourceIsMesh)
                {
                    pc_source_segmentation.preprocessCloud();
                }
            }

            #pragma omp section
            {
                if(!targetIsMesh)
                {
                    pc_target_segmentation.preprocessCloud();
                }
            }
        }
    }
    else if(event.getKeySym() == "F10" && event.keyDown())
    {
        // Resample cloud
        if(!sourceIsMesh)
        {
            pc_source_segmentation.resampleCloud();
        }

        if(!targetIsMesh)
        {
            pc_target_segmentation.resampleCloud();
        }
    }
    else if(event.getKeySym() == "F11" && event.keyDown())
    {
        if(!sourceIsMesh && pc_source_segmentation.isCloudSegmented())
        {
            vector<SegmentedPointsContainer::SegmentedPlane> planes_list = pc_source_segmentation.getSegmentedPlanes();
            // Merge similar planes
            pc_source_merger.start_merge(planes_list, pc_source_segmentation.getPointCloud());
        }
        else
        {
            cout << "Could not merge planes because the cloud is not segmented." << endl;
        }

        if(!targetIsMesh && pc_target_segmentation.isCloudSegmented())
        {
            vector<SegmentedPointsContainer::SegmentedPlane> planes_list = pc_target_segmentation.getSegmentedPlanes();
            // Merge similar planes
            pc_target_merger.start_merge(planes_list, pc_target_segmentation.getPointCloud());
        }
    }
    else if(event.getKeySym() == "a" && event.keyDown())
    {
    	// Registrer cloud/mesh
        if(!(((!sourceIsMesh && pc_source_merger.isCloudMerged()) || mesh_source_segmentation.isMeshSegmented()) &&
             ((!targetIsMesh && pc_target_merger.isCloudMerged()) || mesh_target_segmentation.isMeshSegmented()))) return;

        // Get both segmented sets of planes
        vector<SegmentedPointsContainer::SegmentedPlane> source;
        vector<SegmentedPointsContainer::SegmentedPlane> target;

        if(sourceIsMesh)
        {
            source = mesh_source_segmentation.getSegmentedPlanes();
        }
        else
        {
            source = pc_source_merger.getSegmentedPlanes();
        }

        if(targetIsMesh)
        {
            target = mesh_target_segmentation.getSegmentedPlanes();
        }
        else
        {
            target = pc_target_merger.getSegmentedPlanes();
        }

        registration.setClouds(source, target, targetIsMesh, sourceIsMesh, pc_source_segmentation.getPointCloud(), pc_target_segmentation.getPointCloud());

        mat4 firstTransform = registration.findAlignment();
        registration.applyTransform(firstTransform);

        // Try to refine the initial alignment by excluding center pairs
        mat4 realignTransform = registration.refineAlignment();
        registration.applyTransform(realignTransform);

        mat4 icpTransform = registration.finalICP();
        registration.applyTransform(icpTransform);

        mat4 finalTransform = icpTransform * realignTransform * firstTransform;

        // Apply the transformation to the source
        if(sourceIsMesh)
        {
            pcl::PointCloud<pcl::PointXYZRGB> cloud;
            pcl::PolygonMesh p_transformed_mesh(*mesh_source_segmentation.getMeshPtr());
            pcl::fromPCLPointCloud2(mesh_source_segmentation.getMeshPtr()->cloud, cloud);
            pcl::transformPointCloud(cloud, cloud, finalTransform);

            // Color whole point in blue
            ivec3 color(0, 0, 255);
            for(auto i: cloud.points)
            {
                i.rgba = static_cast<uint8_t>(color.x()) << 16 |
                         static_cast<uint8_t>(color.y()) << 8 |
                         static_cast<uint8_t>(color.z());
            }

            pcl::toPCLPointCloud2(cloud, p_transformed_mesh.cloud);

            p_viewer->addPolygonMesh(p_transformed_mesh, "transformed_mesh");
        }
        else
        {


            PointNormalKCloud::Ptr p_transformed_cloud = PointNormalKCloud().makeShared();
            pcl::transformPointCloud(*pc_source_segmentation.getPointCloud(), *p_transformed_cloud, finalTransform);

            pcl::visualization::PointCloudColorHandlerCustom<PointNormalK> color(p_transformed_cloud, 0, 0, 255);
            p_viewer->removePointCloud("transformed_cloud");
            p_viewer->addPointCloud(p_transformed_cloud, color, "transformed_cloud");
            p_viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "transformed_cloud");
        }
    }
    else if(event.getKeySym() == "k" && event.keyDown())
    {
        registration.highlightAssociatedPlanes();
    }
    else if(event.keyDown())
    {
        cout << event.getKeySym() << ", " << event.getKeyCode() << endl;
    }
}

void display_update_callback(PointNormalKCloud::Ptr cloud, ivec3 color, vector<int> indices, bool isSource = true)
{
    for(int i: indices)
    {
        cloud->points[i].rgba = static_cast<uint8_t>(color.x()) << 16 |
                                  static_cast<uint8_t>(color.y()) << 8 |
                                  static_cast<uint8_t>(color.z());
    }

    if(isSource)
    {
        pc_source_has_changed = true;
    }
    else
    {
        pc_target_has_changed = true;
    }
}

void add_plane_callback(pcl::ModelCoefficients coeffs, float x, float y, float z)
{
    string plane_n = &"plane" [ plane_nb];
    cout << "Adding " << plane_n << endl;
    p_viewer->addPlane(coeffs, x, y, z, plane_n);
    ++plane_nb;
}

void update_normal_cloud_callback()
{
    if(isNormalDisplayed) normal_cloud_changed = true;
}

void pc_planes_callback(SegmentedPointsContainer::SegmentedPlane source_plane, SegmentedPointsContainer::SegmentedPlane target_plane, ivec3 color)
{
    if(targetIsMesh)
    {
        mesh_target_segmentation.updateColors(target_plane, color);
        refresh_target_mesh = true;
    }
    else
    {
        auto pc = pc_target_segmentation.getPointCloud();
        display_update_callback(pc, color, target_plane.indices_list, false);
    }

    if(sourceIsMesh)
    {
        mesh_source_segmentation.updateColors(source_plane, color);
        refresh_source_mesh = true;
    }
    else
    {
        auto pc = pc_source_segmentation.getPointCloud();
        display_update_callback(pc, color, source_plane.indices_list);
    }
}

function<void(PointNormalKCloud::Ptr, ivec3 color, vector<int> indices, bool isSource)> display_update_callable = &display_update_callback;
function<void(pcl::ModelCoefficients, float, float, float)> add_plane_callable = &add_plane_callback;
function<void(void)> update_normal_cloud_callable = &update_normal_cloud_callback;
function<void(SegmentedPointsContainer::SegmentedPlane, SegmentedPointsContainer::SegmentedPlane, ivec3)> pc_planes_callable = &pc_planes_callback;

// Start and setup viewer
pcl::visualization::PCLVisualizer::Ptr setupViewer()
{
    pcl::visualization::PCLVisualizer::Ptr viewer(new pcl::visualization::PCLVisualizer("3D Viewer"));
    viewer->setBackgroundColor(0.3, 0.3, 0.3);
    viewer->initCameraParameters();
    viewer->registerKeyboardCallback(keyboardCallback, static_cast<void*>(viewer.get()));

    return viewer;
}

int main(int argc, char *argv[])
{   
    if(argc != 5)
    {
        cout << "Usage: pointCloudAlignment [c/m] [source_file] [c/m] [target_file]" << endl;
        exit(1);
    }

    // Parsing arguments
    if(strcmp(argv[1], "m") == 0)
    {
        sourceIsMesh = true;
        source_mesh_filename = argv[2];
    }
    else
    {
        sourceIsMesh = false;
        source_cloud_filename = argv[2];
    }

    if(strcmp(argv[3], "m") == 0)
    {
        targetIsMesh = true;
        target_mesh_filename = argv[4];
    }
    else
    {
        targetIsMesh = false;
        target_cloud_filename = argv[4];
    }

    p_viewer = setupViewer();

    if(!sourceIsMesh)
    {
        pc_source_segmentation.init(source_cloud_filename, true);
        pc_source_segmentation.setViewerUpdateCallback(display_update_callable);
        pc_source_segmentation.setAddPlaneCallback(add_plane_callable);
        pc_source_segmentation.setUpdateNormalCloudCallback(update_normal_cloud_callable);

        pc_source_merger.init(display_update_callable, true);

        pcl::visualization::PointCloudColorHandlerRGBField<PointNormalK> rgb(pc_source_segmentation.getPointCloud());
        p_viewer->addPointCloud(pc_source_segmentation.getPointCloud(), rgb, "source_point_cloud");
        p_viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "source_point_cloud");
    }

    if(!targetIsMesh)
    {
        pc_target_segmentation.init(target_cloud_filename, false);
        pc_target_segmentation.setViewerUpdateCallback(display_update_callable);

        pc_target_merger.init(display_update_callable, false);

        pcl::visualization::PointCloudColorHandlerRGBField<PointNormalK> rgb_target(pc_target_segmentation.getPointCloud());
        p_viewer->addPointCloud(pc_target_segmentation.getPointCloud(), rgb_target, "target_point_cloud");
        p_viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "target_point_cloud");
    }

    registration.setCallback(pc_planes_callable);

    p_viewer->resetCamera();

    // Start plane segmentation thread and viewer thread
    #pragma omp parallel sections
    {
        #pragma omp section
        {
            cout << "Viewer loop executed by thread " << omp_get_thread_num() << endl;

            // Drawing loop
            while(!p_viewer->wasStopped())
            {
                if(pc_source_has_changed)
                {
                    pc_source_has_changed = false;

                    // The function updatePointCloud doesn't work, thus it is necessary to remove and add the cloud
                    p_viewer->removePointCloud("source_point_cloud");
                    pcl::visualization::PointCloudColorHandlerRGBField<PointNormalK> rgb(pc_source_segmentation.getPointCloud());
                    p_viewer->addPointCloud(pc_source_segmentation.getPointCloud(), rgb, "source_point_cloud");
                    p_viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "source_point_cloud");
                }

                if(pc_target_has_changed)
                {
                    pc_target_has_changed = false;

                    p_viewer->removePointCloud("target_point_cloud");
                    pcl::visualization::PointCloudColorHandlerRGBField<PointNormalK> rgb_target(pc_target_segmentation.getPointCloud());
                    p_viewer->addPointCloud(pc_target_segmentation.getPointCloud(), rgb_target, "target_point_cloud");
                    p_viewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3, "target_point_cloud");
                }

                if(isNormalDisplayed && normal_cloud_changed)
                {
                    normal_cloud_changed = false;
                    p_viewer->removePointCloud("source_normal_cloud");
                    p_viewer->addPointCloudNormals<PointNormalK, PointNormalK>(pc_source_segmentation.getPointCloud(), pc_source_segmentation.getPointCloud(), 5, 1, "source_normal_cloud");
                }

                if(refresh_target_mesh)
                {
                    refresh_target_mesh = false;
                    p_viewer->removePolygonMesh("target_mesh");
                    p_viewer->addPolygonMesh(*mesh_target_segmentation.getMeshPtr(), "target_mesh");
                }

                if(refresh_source_mesh)
                {
                    refresh_source_mesh = false;
                    p_viewer->removePolygonMesh("source_mesh");
                    p_viewer->addPolygonMesh(*mesh_source_segmentation.getMeshPtr(), "source_mesh");
                }

                p_viewer->spinOnce(100);
            }

            cout << "Viewer was stopped" << endl;

            pc_source_segmentation.stop();
            pc_target_segmentation.stop();
        }

        #pragma omp section
        {
            cout << "Source segmentation loop started by thread " << omp_get_thread_num() << endl;
            pc_source_segmentation.runMainLoop();
        }

        #pragma omp section
        {
            cout << "Target segmentation loop started by thread " << omp_get_thread_num() << endl;
            pc_target_segmentation.runMainLoop();
        }
    }

    p_viewer = setupViewer();

    pcl::PointCloud<pcl::PointXYZ>::Ptr p_source_centers(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::PointCloud<pcl::PointXYZ>::Ptr p_target_centers(new pcl::PointCloud<pcl::PointXYZ>);

    registration.getCenterClouds(p_source_centers, p_target_centers);
    auto selected_planes = registration.getSelectedPlanes();

    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> red(p_source_centers, 255, 0, 0);
    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> green(p_target_centers, 0, 255, 0);

    p_viewer->addPointCloud(p_target_centers, green, "target_centers");
    p_viewer->addPointCloud(p_source_centers, red, "source_centers");

    int i = 0;
    for(auto p: selected_planes) {
        stringstream ss;
        ss << "line" << i;
        p_viewer->addLine(get<0>(p).plane.getCenterPCL(), get<1>(p).plane.getCenterPCL(), 0, 0, 255, ss.str());
        ++i;
    }

    while(!p_viewer->wasStopped())
    {
        p_viewer->spinOnce(100);
    }

    return 0;
}
