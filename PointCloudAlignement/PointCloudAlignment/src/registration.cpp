#include "registration.h"

void Registration::filterPlanes(int nb_planes, vector<SegmentedPointsContainer::SegmentedPlane> &planes, vector<float> &surfaces)
{
    //Build list of available indices that will be sorted in decreasing surface order
    vector<size_t> id_list;
    for(size_t i = 0; i < planes.size(); ++i)
    {
        id_list.push_back(i);
    }

    // Sort index list by decreasing surface order
    sort(id_list.begin(), id_list.end(), [&surfaces](size_t i, size_t j){
        return surfaces[i] > surfaces[j];
    });

    vector<SegmentedPointsContainer::SegmentedPlane> new_planes;
    vector<float> new_surfaces;

    for(size_t i = 0; i < nb_planes; ++i)
    {
        new_planes.push_back(planes[id_list[i]]);
        new_surfaces.push_back(surfaces[id_list[i]]);
    }

    planes.swap(new_planes);
    surfaces.swap(new_surfaces);
}

void Registration::setClouds(vector<SegmentedPointsContainer::SegmentedPlane> &source, vector<SegmentedPointsContainer::SegmentedPlane> &target, bool targetIsMesh, bool sourceIsMesh,
                             PointNormalKCloud::Ptr p_source_cloud, PointNormalKCloud::Ptr p_target_cloud)
{
    this->source = source;
    this->target = target;
    this->targetIsMesh = targetIsMesh;
    this->sourceIsMesh = sourceIsMesh;
    this->p_cloud = p_source_cloud;

    // Compute surfaces if surface vectors are empty
    if(this->source_surfaces.empty())
    {
        if(sourceIsMesh)
        {
            // Compute mesh planes surface
            for(auto s: source)
            {
                this->source_surfaces.push_back(s.plane.getNormal().norm());
            }
        }
        else
        {
            this->source_surfaces = estimatePlanesSurface(p_source_cloud, source);
        }


        // Filter out planes to keep only MIN_SURFACE biggest planes for each set
        //filterPlanes(MIN_SURFACE, this->source, source_surfaces);
    }

    if(this->target_surfaces.empty())
    {
        if(targetIsMesh)
        {
            // Compute mesh planes surface
            for(auto t: target)
            {
                target_surfaces.push_back(t.plane.getNormal().norm());
            }
        }
        else
        {
            this->target_surfaces = estimatePlanesSurface(p_target_cloud, target);
        }

        // Filter out planes to keep only MIN_SURFACE biggest planes for each set
        //filterPlanes(MIN_SURFACE, this->target, target_surfaces);
    }
}

mat3 Registration::findAlignment()
{
    if(target.empty() || source.empty()) return mat3::Identity();

    mat3 R = findRotation();
    //R *= -1;
    return R;
}

mat3 Registration::findRotation()
{
    vec3 cS, cT, nS, nT;
    vector<vec3> l_cS, l_cT, l_nS, l_nT;
    vector<float> angles_S, angles_T;
    vector<float> angles_cS, angles_cT;

    #pragma omp parallel sections
    {
        #pragma omp section
        {
            cS = computeCentersCentroid(source);
            l_cS = computeCentersDifSet(source, cS);
        }

        #pragma omp section
        {
            cT = computeCentersCentroid(target);
            l_cT = computeCentersDifSet(target, cT);
        }

        #pragma omp section
        {
            nS = computeCentroid(source, sourceIsMesh);
            l_nS = computeDifSet(source, nS, sourceIsMesh);
        }

        #pragma omp section
        {
            nT = computeCentroid(target, targetIsMesh);
            l_nT = computeDifSet(target, nT, targetIsMesh);
        }
    }

    #pragma omp parallel sections
    {
        #pragma omp section
        {
            angles_S = computeAngleDifs(l_cS, source);
        }

        #pragma omp section
        {
            angles_T = computeAngleDifs(l_cT, target);
        }

        #pragma omp section
        {
            angles_cS = computeCenterAngles(l_cS);
        }

        #pragma omp section
        {
            angles_cT = computeCenterAngles(l_cT);
        }
    }

    computeMwithCentroids(l_cS, l_cT, angles_S, angles_T, angles_cS, angles_cT);

    //mat3 H = computeHwithNormals(l_nS, l_nT);
    mat3 H = computeHwithCentroids(l_cS, l_cT);
    mat3 R = computeR(H);


    if(R.determinant() < 0.0f)
    {
        cout << "det of R is less than 0" << endl;
        R.col(2) = -1 * R.col(2);
    }

    return R;
}

mat3 Registration::findTranslation()
{
    return mat3::Zero();
}

void Registration::computeMwithNormals()
{
    if(target.empty() || source.empty()) return;

    M.resize(source.size(), target.size());

    #pragma omp parallel for
    for(size_t i = 0; i < source.size(); ++i)
    {
        vec3 ni = source[i].plane.getNormalizedN() * source[i].indices_list.size();

        for(size_t j = 0; j < target.size(); ++j)
        {
            vec3 nj = target[j].plane.getNormal();
            if(!targetIsMesh)
            {
                nj = nj.normalized() * target[j].indices_list.size();
            }

            float sqd = pow(ni.norm() - nj.norm(), 2);

            if(sqd == 0.0f)
            {
                M(i, j) = 10000.0f;
            }
            else
            {
                // Works of for small rotations but can get stuck in local minimas
                M(i, j) = 1.0f / sqd;
            }
            //M(i, j) = exp(-0.5*sqd);

        }
    }

    cout << "M:" << endl << M.block(0, 0, 5, 5).matrix() << endl;
}

void Registration::computeMwithCentroids(vector<vec3> &l_cS, vector<vec3> &l_cT, vector<float> &l_aS, vector<float> &l_aT, vector<float> &angles_cS, vector<float> &angles_cT)
{
    if(l_cT.empty() || l_cS.empty() || l_aS.empty() || l_aT.empty() || angles_cS.empty() || angles_cT.empty()) return;

    M.resize(l_cS.size(), l_cT.size());

    //#pragma omp parallel for
    for(size_t i = 0; i < l_cS.size(); ++i)
    {
        float normCSi = l_cS[i].norm();

        for(size_t j = 0; j < l_cT.size(); ++j)
        {
            M(i, j) = exp(-abs((normCSi - l_cT[j].norm()) /*+ (l_aS[i] - l_aT[j])*/ + (source_surfaces[i] - target_surfaces[j]) /*+ (angles_cS[i] - angles_cT[j])*/));
            /*float m = abs((normCSi - l_cT[j].norm()) * (l_aS[i] - l_aT[j]) * (source_surfaces[i] - target_surfaces[j]));
            if(m == 0)
            {
                M(i, j) = 1000000.0f;
            }
            else
            {
                M(i, j) = 1.0f / m;
            }*/
        }
    }

    //M.normalize();
    /*
    Eigen::MatrixXf M_binary = Eigen::MatrixXf::Zero(M.rows(), M.cols());

    for (int i = 0; i < M_binary.rows(); ++i) {
        Eigen::MatrixXf::Index id;
        M.row(i).maxCoeff(&id);

        M_binary(i, id) = 1;
    }

    M = M_binary;
    */
    //cout << "M" << endl << M << endl;

    // TEST: Change color of second plane of source and corresponding plane in target to black
    //Eigen::MatrixXf::Index id;
    //M.row(1).maxCoeff(&id);
    //display_update_callable(source[1], target[id], ivec3(0, 0, 0));

    // Write M to file
    string filename("myMatrixMFile.txt");
    ofstream myFile;
    myFile.open(filename);
    myFile << M << endl;
    myFile.close();
}

mat3 Registration::computeHwithNormals(vector<vec3> qs, vector<vec3> qt)
{
    mat3 H = mat3::Zero();

    for(int i = 0; i < qs.size(); ++i)
    {
        for(int j = 0; j < qt.size(); ++j)
        {
            H += M(i, j) * qs[i] * qt[j].transpose();
        }
    }

    return H;
}

mat3 Registration::computeHwithCentroids(vector<vec3> &l_cS, vector<vec3> &l_cT)
{
    mat3 H = mat3::Zero();

    for(size_t i = 0; i < l_cS.size(); ++i)
    {
        for(size_t j = 0; j < l_cT.size(); ++j)
        {
            H += M(i, j) * l_cS[i].normalized() * l_cT[j].normalized().transpose();
        }
    }

    return H;
}

mat3 Registration::computeR(mat3 H)
{
    Eigen::JacobiSVD<mat3> svd(H, Eigen::ComputeFullU | Eigen::ComputeFullV);
    mat3 u = svd.matrixU();
    mat3 v = svd.matrixV();
    return v * u.transpose();
}

vec3 Registration::computeCentroid(vector<SegmentedPointsContainer::SegmentedPlane> &list, bool isMesh)
{
    vec3 c = vec3::Zero();

    for(auto plane: list)
    {
        vec3 n = plane.plane.getNormalizedN();
        /*if(!isMesh)
        {
            n = n.normalized() * plane.indices_list.size();
        }*/
        c += n;
    }

    c /= list.size();
    return c;
}

vector<float> Registration::computeCenterAngles(vector<vec3> &l_shifted_centroids)
{
    vector<float> angles;

    // Compute mean
    vec3 center_mean(0, 0, 0);
    for(vec3 a: l_shifted_centroids)
    {
        center_mean += a;
    }
    center_mean.normalize();

    // Compute angles
    for(vec3 ci: l_shifted_centroids)
    {
        ci.normalize();
        float cos_a = ci.dot(center_mean);
        angles.push_back(cos_a);
    }

    return angles;
}

vector<vec3> Registration::computeDifSet(vector<SegmentedPointsContainer::SegmentedPlane> &list, vec3 centroid, bool isMesh)
{
    vector<vec3> demeaned(list.size());

    #pragma omp parallel for
    for(size_t i = 0; i < list.size(); ++i)
    {
        vec3 n = list[i].plane.getNormalizedN();
        /*if(!isMesh)
        {
            n = n.normalized() * list[i].indices_list.size();
        }*/
        demeaned[i] = n - centroid;
    }

    return demeaned;
}

vec3 Registration::computeCentersCentroid(vector<SegmentedPointsContainer::SegmentedPlane> &list)
{
    vec3 c(0, 0, 0);

    for(auto p: list)
    {
        c += p.plane.getCenter();
    }

    c /= list.size();
    return c;
}

vector<vec3> Registration::computeCentersDifSet(vector<SegmentedPointsContainer::SegmentedPlane> &list, vec3 centroid)
{
    vector<vec3> q(list.size());

    #pragma omp parallel for shared(q)
    for(size_t i = 0; i < list.size(); ++i)
    {
        q[i] = list[i].plane.getCenter() - centroid;
    }

    return q;
}

vector<float> Registration::computeAngleDifs(vector<vec3> &l_shifted_centroids, vector<SegmentedPointsContainer::SegmentedPlane> &l_planes)
{
    vector<float> angles;

    if(l_shifted_centroids.size() != l_planes.size())
    {
        cout << "Computing Angle Diffs: Not same size of vectors... stopping" << endl;
        return angles;
    }

    for(size_t i = 0; i < l_shifted_centroids.size(); ++i)
    {
        // Cosine of angle between ci and ni
        float angle = l_shifted_centroids[i].normalized().dot(l_planes[i].plane.getNormalizedN());
        angles.push_back(angle);
    }

    return angles;
}

vector<float> Registration::estimatePlanesSurface(PointNormalKCloud::Ptr p_cloud, vector<SegmentedPointsContainer::SegmentedPlane> &l_planes)
{
    vector<float> surfaces(l_planes.size());

    #pragma omp parallel for
    for(size_t i = 0; i < l_planes.size(); ++i)
    {
        surfaces[i] = estimatePlaneSurface(p_cloud, l_planes[i]);
    }

    return surfaces;
}

float Registration::estimatePlaneSurface(PointNormalKCloud::Ptr p_cloud, SegmentedPointsContainer::SegmentedPlane &plane)
{
    vec3 e1, e2;
    computePlaneBase(plane, e1, e2);

    vector<vec2> l_2dPoints = pointsTo2D(p_cloud, plane, e1, e2);

    // Estimating plane surface by finding principal directions by doing svd decomposition on congruence matrix formed by points in 2d
    // Compute centroid 2D
    vec2 center = compute2dCentroid(l_2dPoints);

    // Compute covariance matrix
    mat2 cov = mat2::Zero();

    float alpha = 1.0f / (l_2dPoints.size() - 1);

    for(size_t i = 0; i < l_2dPoints.size(); ++i)
    {
        cov += alpha * (l_2dPoints[i] - center) * (l_2dPoints[i] - center).transpose();
    }

    // SVD Decomposition
    Eigen::JacobiSVD<mat2> svd(cov/*, Eigen::ComputeFullU*/);
    //mat2 u = svd.matrixU();
    vec2 s = svd.singularValues();

    // Non correlated variances
    s = s.array().abs().sqrt().matrix();

    // compute surface estimation
    return s.x() * s.y() * 4 * 3; // ???
}

vector<vec2> Registration::pointsTo2D(PointNormalKCloud::Ptr p_cloud, SegmentedPointsContainer::SegmentedPlane &plane, vec3 e1, vec3 e2)
{
    vec3 center = plane.plane.getCenter();

    vector<vec2> t;

    for(auto i: plane.indices_list)
    {
        vec2 ti;
        ti.x() = e1.dot(pclToVec3(p_cloud->points[i]) - center);
        ti.y() = e2.dot(pclToVec3(p_cloud->points[i]) - center);
        t.push_back(ti);
    }

    return t;
}

void Registration::computePlaneBase(SegmentedPointsContainer::SegmentedPlane &plane, vec3 &e1, vec3 &e2)
{
    vec3 n = plane.plane.getNormalizedN();

    // Build e1 by rotating plane normal by 90 degrees
    e1 = vec3(n.y(), -n.x(), n.z()).normalized();

    // Base should be orthogoal
    e2 = n.cross(e1).normalized();

    //cout << "Plane " << plane.id << " : e1: " << e1.transpose() << " e2: " << e2.transpose() << endl;
}

vec2 Registration::compute2dCentroid(vector<vec2> l_points)
{
    vec2 c(0, 0);
    for_each(l_points.begin(), l_points.end(), [&c](vec2 i){
        c += i;
    });
    c /= l_points.size();
    return c;
}
