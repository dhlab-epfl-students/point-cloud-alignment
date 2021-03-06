#include "registration.h"

vector<tuple<SegmentedPointsContainer::SegmentedPlane, SegmentedPointsContainer::SegmentedPlane>> Registration::getSelectedPlanes()
{
    vector<tuple<SegmentedPointsContainer::SegmentedPlane, SegmentedPointsContainer::SegmentedPlane>> planes;

    for(auto pair: this->selected_planes)
    {
        size_t s_id = get<0>(pair);
        size_t t_id = get<1>(pair);

        planes.push_back(make_tuple(this->source[s_id], this->target[t_id]));
    }

    return planes;
}

void Registration::getCenterClouds(pcl::PointCloud<pcl::PointXYZ>::Ptr p_source_centers, pcl::PointCloud<pcl::PointXYZ>::Ptr p_target_centers)
{
    for(auto s: this->source)
    {
        p_source_centers->push_back(s.plane.getCenterPCL());
    }

    for(auto t: this->target)
    {
        p_target_centers->push_back(t.plane.getCenterPCL());
    }
}

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
            this->source_surfaces = computeDelaunaySurfaces(p_source_cloud, source);//estimatePlanesSurface(p_source_cloud, source);
        }
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
            this->target_surfaces = computeDelaunaySurfaces(p_target_cloud, target);//estimatePlanesSurface(p_target_cloud, target);
        }
    }
}

void Registration::highlightAssociatedPlanes()
{
    // First reset colors if previous plane was highlighted
    if(curr_highlighted_plane != -1)
    {
        size_t source_id = std::get<0>(selected_planes[curr_highlighted_plane]);
        size_t target_id = std::get<1>(selected_planes[curr_highlighted_plane]);

        display_update_callable(source[source_id], target[target_id], source[source_id].color);
    }

    // Increment for next plane
    curr_highlighted_plane = curr_highlighted_plane < selected_planes.size()-1 ? curr_highlighted_plane + 1 : 0;

    size_t source_id = std::get<0>(selected_planes[curr_highlighted_plane]);
    size_t target_id = std::get<1>(selected_planes[curr_highlighted_plane]);
    float error = get<2>(selected_planes[curr_highlighted_plane]);

    display_update_callable(source[source_id], target[target_id], ivec3(255, 255, 255));
    cout << "Associated source " << source_id << " : " << source_surfaces[source_id] << " with target " << target_id << " : " << target_surfaces[target_id] << " with error: " << error << endl;
}

mat4 Registration::findAlignment()
{
    if(target.empty() || source.empty()) return mat4::Identity();

    selected_planes = planeTuplesWithFPFH();
    M = buildM(selected_planes);
    R = findRotation(M);

    float init_error = getAlignmentError();

    T = findAndAddTranslation(R);

    cout << "R: " << endl << R << endl;
    cout << "Complete transformation: " << endl << T << endl;

    return T;
}

mat4 Registration::findRotation(matX aggregation_matrix)
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
            nS = computeNormalsCentroid(source, sourceIsMesh);
            l_nS = computeNormalsDifSet(source, nS, sourceIsMesh);
        }

        #pragma omp section
        {
            nT = computeNormalsCentroid(target, targetIsMesh);
            l_nT = computeNormalsDifSet(target, nT, targetIsMesh);
        }
    }

    mat3 H = computeHwithCentroids(aggregation_matrix, l_cS, l_cT);
    mat3 R3 = computeR(H);

    mat4 R = mat4::Identity();
    R.block(0, 0, 3, 3) << R3;

    return R;
}

mat4 Registration::findAndAddTranslation(mat4 &R)
{
    // Compute the mean of centers from selected corresponding source and target planes
    vec3 source_cmean(0, 0, 0);
    vec3 target_cmean(0, 0, 0);

    for(auto t: this->selected_planes)
    {
        auto i = get<0>(t);
        auto j = get<1>(t);

        source_cmean += source[i].plane.getCenter();
        target_cmean += target[j].plane.getCenter();
    }

    source_cmean /= selected_planes.size();
    target_cmean /= selected_planes.size();

    // Rotate the source centroid
    vec4 source_rotated = R * vec4(source_cmean.x(), source_cmean.y(), source_cmean.z(), 1);

    // Compute translation vector from source center mean to target center mean.
    vec3 t_vec = target_cmean - vec3(source_rotated.x(), source_rotated.y(), source_rotated.z());

    // Compute translation matrix
    mat4 T = Eigen::Affine3f(Eigen::Translation3f(t_vec)).matrix();

    cout << "T:" << endl << T << endl;

    return T * R;
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
        }
    }
}

void Registration::computeMwithCentroids(vector<vec3> &l_cS, vector<vec3> &l_cT, vector<float> &l_aS, vector<float> &l_aT, vector<float> &angles_cS, vector<float> &angles_cT)
{
    if(l_cT.empty() || l_cS.empty() || l_aS.empty() || l_aT.empty() || angles_cS.empty() || angles_cT.empty()) return;

    M.resize(l_cS.size(), l_cT.size());

    for(size_t i = 0; i < l_cS.size(); ++i)
    {
        float normCSi = l_cS[i].norm();

        for(size_t j = 0; j < l_cT.size(); ++j)
        {
            M(i, j) = exp(-abs(((normCSi - l_cT[j].norm()) + /*(l_aS[i] - l_aT[j]) +*/ (source_surfaces[i] - target_surfaces[j])) /* (angles_cS[i] - angles_cT[j])*/));
        }
    }
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

mat3 Registration::computeHwithCentroids(matX aggregation_m, vector<vec3> &l_cS, vector<vec3> &l_cT)
{
    mat3 H = mat3::Zero();

    for(size_t i = 0; i < l_cS.size(); ++i)
    {
        for(size_t j = 0; j < l_cT.size(); ++j)
        {
            H += aggregation_m(i, j) * l_cS[i].normalized() * l_cT[j].normalized().transpose();
        }
    }

    return H;
}

mat3 Registration::computeR(mat3 H)
{
    Eigen::JacobiSVD<mat3> svd(H, Eigen::ComputeFullU | Eigen::ComputeFullV);
    mat3 u = svd.matrixU();
    mat3 v = svd.matrixV();

    mat3 R = v * u.transpose();

    if(R.determinant() < 0.0f)
    {
        cout << "det of R is less than 0" << endl;
        cout << "Initial R: " << endl << R << endl;

        v.col(2) = -1 * v.col(2);
        R = v * u.transpose();

        cout << "Corrected R: " << endl << R << endl;
    }

    return R;
}

vec3 Registration::computeNormalsCentroid(vector<SegmentedPointsContainer::SegmentedPlane> &list, bool isMesh)
{
    vec3 c = vec3::Zero();

    for(auto plane: list)
    {
        vec3 n = plane.plane.getNormalizedN();
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

vector<vec3> Registration::computeNormalsDifSet(vector<SegmentedPointsContainer::SegmentedPlane> &list, vec3 centroid, bool isMesh)
{
    vector<vec3> demeaned(list.size());

    #pragma omp parallel for
    for(size_t i = 0; i < list.size(); ++i)
    {
        vec3 n = list[i].plane.getNormalizedN();
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

// ========================================= Plane Surface Estimation =================================================== //

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
    Eigen::JacobiSVD<mat2> svd(cov);
    vec2 s = svd.singularValues();

    // Non correlated variances
    s = s.array().abs().sqrt().matrix();

    // compute surface estimation
    return s.x() * s.y() * 4 * 3;
}

vector<float> Registration::computeDelaunaySurfaces(PointNormalKCloud::Ptr p_cloud, vector<SegmentedPointsContainer::SegmentedPlane> &l_planes)
{
    vector<float> surfaces;

    for(auto plane: l_planes)
    {
        surfaces.push_back(computeDelaunaySurface(p_cloud, plane));
    }

    return surfaces;
}

float Registration::computeDelaunaySurface(PointNormalKCloud::Ptr p_cloud, SegmentedPointsContainer::SegmentedPlane &plane)
{
    // Find Plane base
    vec3 e1, e2;
    computePlaneBase(plane, e1, e2);

    // Convert 3D points to 2D
    vector<vec2> list_2d = pointsTo2D(p_cloud, plane, e1, e2);

    // Compute Rectangle formed by points and fill vector of Point2f
    vec2 min(0, 0), max(0, 0);
    vector<cv::Point2f> l_p2f;
    for(vec2 i: list_2d)
    {
        min.x() = min.x() < i.x() ? min.x() : i.x();
        min.y() = min.y() < i.y() ? min.y() : i.y();
        max.x() = max.x() > i.x() ? max.x() : i.x();
        max.y() = max.y() > i.y() ? max.y() : i.y();

        l_p2f.push_back(cv::Point2f(i.x(), i.y()));
    }

    // Compute delauney triangle
    cv::Rect rect(min.x() - 1, min.y() - 1, max.x()-min.x() + 2, max.y()-min.y() + 2);
    cv::Subdiv2D sub(rect);
    sub.insert(l_p2f);

    vector<cv::Vec6f> triangles;
    sub.getTriangleList(triangles);

    // Sum up every triangles' surface

    float surface = 0;
    vec2 x1, x2, x3;
    for(size_t i = 0; i < triangles.size(); ++i)
    {
        // Triangles vertices in 2D
        cv::Vec6f t = triangles[i];
        x1 = vec2(t[0], t[1]);
        x2 = vec2(t[2], t[3]);
        x3 = vec2(t[4], t[5]);

        if(rect.contains(cv::Point2f(x1.x(), x1.y())) &&
           rect.contains(cv::Point2f(x2.x(), x2.y())) &&
           rect.contains(cv::Point2f(x3.x(), x3.y())))
        {
            vec2 v1 = x2 - x1;
            vec2 v2 = x3 - x1;

            float surf = 0.5f * fabs(crossProduct(v1, v2));
            surface += surf;
        }
    }

    return surface;
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
    if(n.x() != 0)
    {
        e1 = vec3(n.y(), -n.x(), 0).normalized();
    }
    else if(n.y() != 0)
    {
        e1 = vec3(-n.y(), n.x(), 0).normalized();
    }
    else
    {
        e1 = vec3(-n.z(), 0, n.x()).normalized();
    }

    // Base should be orthogonal
    e2 = n.cross(e1).normalized();
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

/////======================== PFH Based computation of M =====================================================================

Eigen::MatrixXf Registration::planeTuplesWithPFH(int source_nb)
{
    PFHCloud source_signs = PFHEvaluation::computePFHSignatures(source);
    PFHCloud target_signs = PFHEvaluation::computePFHSignatures(target);

    // Construct indice list of planes
    vector<size_t> source_indices;
    size_t i = 0;
    for(auto p: source)
    {
        source_indices.push_back(i);
        i++;
    }

    // Sort index list by decreasing surface order
    sort(source_indices.begin(), source_indices.end(), [this](size_t i, size_t j){
        return this->source_surfaces[i] > this->source_surfaces[j];
    });

    Eigen::MatrixXf M_pfh = Eigen::MatrixXf::Zero(source.size(), target.size());

    // Fill M by associating each source to one target based on pfh histograms errors
    for (i = 0; i < (int)source_indices.size() / 2.0f/*std::min((int)source_indices.size(), source_nb)*/; ++i)
    {
        float error;
        size_t j = PFHEvaluation::getMinTarget(source_indices[i], source_signs, target_signs, error);

        remove_if(selected_planes.begin(), selected_planes.end(), [&j, &error](tuple<size_t, size_t, float> t){
            return (get<1>(t) == j) && (get<2>(t) > error);
        });

        M_pfh(source_indices[i], j) += 1;

        //List of selected plane tuples (source, target) for translation computation
        selected_planes.push_back(make_tuple(source_indices[i], j, error));
    }

    return M_pfh;
}

vector<PlaneTuples> Registration::planeTuplesWithFPFH()
{
    // Compute apfh forall planes' centers
    auto source_signs = PFHEvaluation::computeAPFHSignature(source, this->source_surfaces);
    auto target_signs = PFHEvaluation::computeAPFHSignature(target, this->target_surfaces);

    // Construct indice list of planes
    vector<size_t> source_indices = getSortedIndicesGiven(source_surfaces);

    vector<PlaneTuples> plane_tuples;

    // Fill M by associating each source to one target based on fpfh histograms errors
    for (size_t i = 0; i < source_indices.size()/*std::min((int)source_indices.size(), source_nb)*/; ++i)
    {
        float error;
        int j = PFHEvaluation::getMinTarget(source_indices[i], source_surfaces[source_indices[i]], target_surfaces, source_signs, target_signs, error);

        if(j > -1)
        {
            auto end_it = remove_if(plane_tuples.begin(), plane_tuples.end(), [this, &source_indices, &i, &j, &error](tuple<size_t, size_t, float> t){
                float curr_s_surf = source_surfaces[source_indices[i]];
                float prev_s_surf = source_surfaces[get<0>(t)];
                float t_surf = target_surfaces[j];
                size_t t_id = get<1>(t);
                float t_err = get<2>(t);
                return (t_id == j) && ((t_err > error) && (abs(prev_s_surf - t_surf) > abs(curr_s_surf - t_surf)));
            });

            //List of selected plane tuples (source, target) for translation computation
            if(end_it != plane_tuples.end())
            {
                plane_tuples.erase(end_it, plane_tuples.end());
                plane_tuples.push_back(make_tuple(source_indices[i], j, error));
            } else if(find_if(plane_tuples.begin(), plane_tuples.end(), [&j](tuple<size_t, size_t, float> t){
                return get<1>(t) == j;
            }) == plane_tuples.end())
            {
                plane_tuples.push_back(make_tuple(source_indices[i], j, error));
            }
        }
    }

    return plane_tuples;
}

Eigen::MatrixXf Registration::buildM(vector<PlaneTuples> &l_tuples)
{
    Eigen::MatrixXf M_pfh = Eigen::MatrixXf::Zero(source.size(), target.size());

    // Go through selected planes and fill M
    for(auto tuple: l_tuples)
    {
        M_pfh(get<0>(tuple), get<1>(tuple)) += 1;
    }

    return M_pfh;
}

vector<size_t> Registration::getSortedIndicesGiven(vector<float> &l_surfaces)
{
    // Construct indice list of planes
    vector<size_t> indices_list;

    for(size_t i = 0; i < l_surfaces.size(); ++i)
    {
        indices_list.push_back(i);
    }

    // Sort index list by decreasing order given by l_surfaces
    sort(indices_list.begin(), indices_list.end(), [l_surfaces](size_t i, size_t j){
        return l_surfaces[i] > l_surfaces[j];
    });

    return indices_list;
}

float Registration::computePFHError(size_t i, size_t j, PFHCloud &source, PFHCloud &target)
{
    float err(0);

    auto hist_source = source[i].histogram;
    auto hist_target = target[j].histogram;

    for(size_t k = 0; k < source[i].descriptorSize(); ++k)
    {
        err += abs(hist_source[k] - hist_target[k]);
    }

    return exp(-0.01*err);
}

float Registration::getAlignmentError()
{
    if(selected_planes.empty()) return -1;

    // Compute the error between selected center associations
    float err(0);

    for(auto t: selected_planes)
    {
        size_t i = get<0>(t);
        size_t j = get<1>(t);

        err += (source[i].plane.getCenter() - target[j].plane.getCenter()).norm();
    }

    return err;
}

//====================================== REALIGNEMENT =================================================================//

mat4 Registration::refineAlignment()
{
    // Recompute distance errors after the transformation
    vector<float> new_distances = computeDistanceErrors();

    // compute std deviation on new_distances
    float stdDev = getStdDeviation(new_distances);

    // Exclude tuples based on new_distances
    // Copy vector in case all are removed
    auto tmp_selected_planes = this->selected_planes;
    size_t i = 0;
    auto it = remove_if(tmp_selected_planes.begin(), tmp_selected_planes.end(), [&i, &new_distances, &stdDev](tuple<size_t, size_t, float> t){
        bool ret = (new_distances[i] > STD_DEV_MULT * stdDev);
        i++;
        return ret;
    });

    // If tuples have been excluded that means that the alignement can possibly be enhanced
    if(it == tmp_selected_planes.begin())
    {
        cout << "Error: every plane has been removed from selected tuples list, not realigning" << endl;
    }
    else if(it != tmp_selected_planes.end())
    {
        tmp_selected_planes.erase(it, tmp_selected_planes.end());
        this->selected_planes.swap(tmp_selected_planes);

        //recompute M with updated tuples
        M = buildM(selected_planes);
        R = findRotation(M);
        return findAndAddTranslation(R);
    }

    return mat4::Identity();
}

void Registration::applyTransform(mat4 &M)
{
    for(size_t i = 0; i < source.size(); ++i)
    {
        vec3 c = source[i].plane.getCenter();
        vec4 c2 = M * vec4(c.x(), c.y(), c.z(), 1);
        source[i].plane.setCenter(vec3(c2.x(), c2.y(), c2.z()));
    }

    rotateSourceNormals();
}

void Registration::rotateSourceNormals()
{
    for(size_t i = 0; i < source.size(); ++i)
    {
        vec3 n = source[i].plane.getNormal();
        vec4 n2 = R * vec4(n.x(), n.y(), n.z(), 1);
        source[i].plane.setNormal(vec3(n2.x(), n2.y(), n2.z()));
    }
}

vector<float> Registration::computeDistanceErrors()
{
    vector<float> errors;

    for(auto i: selected_planes)
    {
        size_t s_id = get<0>(i);
        size_t t_id = get<1>(i);

        errors.push_back(distance(source[s_id].plane.getCenter(), target[t_id].plane.getCenter()));
    }

    return errors;
}

//===================================== FINAL ICP ALIGNMENT ====================================================//

mat4 Registration::finalICP()
{
    // Build PC for source and target
    pcl::PointCloud<pcl::PointXYZ>::Ptr p_source(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::PointCloud<pcl::PointXYZ>::Ptr p_target(new pcl::PointCloud<pcl::PointXYZ>);

    for(size_t i = 0; i < this->source.size(); ++i)
    {
        p_source->push_back(this->source[i].plane.getCenterPCL());
    }

    for(size_t i = 0; i < this->target.size(); ++i)
    {
        p_target->push_back(this->target[i].plane.getCenterPCL());
    }

    // Setup icp object
    pcl::IterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ> icp;
    icp.setInputSource(p_source);
    icp.setInputTarget(p_target);

    // Compute alignement
    pcl::PointCloud<pcl::PointXYZ> final;
    icp.align(final);

    cout << "Has converged: " << icp.hasConverged() << " score: " << icp.getFitnessScore() << endl;

    return icp.getFinalTransformation();
}
