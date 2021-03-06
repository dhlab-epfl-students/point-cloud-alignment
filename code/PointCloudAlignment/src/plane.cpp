#include "plane.h"

void Plane::setCoeffs(float a, float b, float c, float d)
{
    this->a = a;
    this->b = b;
    this->c = c;
    this->d = d;
    this->setNormal(vec3(a, b, c));
}

void Plane::setCenter(vec3 p)
{
    this->center = p;
}

void Plane::setNormal(vec3 n)
{
    this->n = n;
}

vec3 Plane::getNormal()
{
    return n;
}

vec3 Plane::getNormalizedN()
{
    return n.normalized();
}

pcl::ModelCoefficients Plane::getModelCoefficients()
{
    pcl::ModelCoefficients coeffs;
    coeffs.values.push_back(a);
    coeffs.values.push_back(b);
    coeffs.values.push_back(c);
    coeffs.values.push_back(d);

    return coeffs;
}

float Plane::getPlaneTolerance(PointNormalKCloud::Ptr cloud, boost::shared_ptr<vector<int>> indices)
{
    vector<float> distances(indices->size());
    float dist_mean(0);

    #pragma omp parallel for shared(distances, dist_mean)
    for(size_t i = 0; i < indices->size(); ++i)
    {
        float d = this->distanceTo(cloud->points[indices->at(i)]);
        distances[i] = d;

        #pragma omp critical
        dist_mean += d;
    }

    dist_mean /= static_cast<float>(indices->size());

    float dev(0), max_d(0);
    for(float j: distances)
    {
        dev += std::pow(j - dist_mean, 2.0f);
        max_d = j > max_d ? j : max_d;
    }
    dev /= static_cast<float>(distances.size());

    return dist_mean + (3.0f * std::sqrt(dev));
}

float Plane::distanceTo(PointNormalK p)
{
    vec3 p_tmp(p.x, p.y, p.z);
    return this->distanceTo(p_tmp);
}

float Plane::distanceTo(vec3 p)
{
    vec3 ni(0, 0, 0);
    float di(0);
    this->cartesianToNormal(ni, di);

    return fabs(ni.dot(p) + di);
}

void Plane::cartesianToNormal(vec3 &ni, float &di)
{
    vec3 v(a, b, c);
    di = this->d / v.norm();
    ni = v.normalized();
}

void Plane::estimatePlane(PointNormalKCloud::Ptr cloud_in, boost::shared_ptr<vector<int>> indices_in, Plane &plane)
{
    // Get list of points
    PointNormalKCloud::Ptr cloud_f(new PointNormalKCloud);
    pcl::ExtractIndices<PointNormalK> iFilter(false);
    iFilter.setInputCloud(cloud_in);
    iFilter.setIndices(indices_in);
    iFilter.filter(*cloud_f);

    vec4 center;
    pcl::compute3DCentroid(*cloud_f, center);

    // Compose optimisation matrix
    Eigen::MatrixXf m;
    pcl::demeanPointCloud(*cloud_f, center, m);

    // Compute svd decomposition
    Eigen::JacobiSVD<Eigen::MatrixXf> svd(m.block(0,0, 3, m.cols()), Eigen::ComputeThinU);
    Eigen::MatrixXf u = svd.matrixU();

    //Extract plane parameters
    float a = u(0, 2);
    float b = u(1, 2);
    float c = u(2, 2);

    // Compute plane last parameter
    float d = - a * center.x() - b * center.y() - c * center.z();

    plane.setCoeffs(a, b, c, d);
    plane.setCenter(vec3(center.x(), center.y(), center.z()));
}

bool Plane::pointInPlane(PointNormalK p, float epsilon)
{
    vec3 v(p.x, p.y, p.z);
    vec3 n = getNormal().normalized();
    float dist = abs(n.dot(v) + d);

    return dist <= epsilon;
}

bool Plane::normalInPlane(PointNormalK p, float max_angle)
{
    vec3 n = getNormal().normalized();
    vec3 pn(p.normal_x, p.normal_y, p.normal_z);
    pn.normalize();

    //It may be necessary to reorient the normal vector.
    n = pn.dot(n) >= pn.dot(-n) ? n : -n;

    return fabs(acos(pn.dot(n))) <= max_angle;
}

vec3 Plane::getCenter()
{
    return this->center;
}

pcl::PointXYZ Plane::getCenterPCL()
{
    return pcl::PointXYZ(this->center.x(), this->center.y(), this->center.z());
}

pcl::PointNormal Plane::getPointNormal()
{
    pcl::PointNormal pn;
    pn.x = this->center.x();
    pn.y = this->center.y();
    pn.z = this->center.z();

    vec3 nNormalized = this->getNormalizedN();
    pn.normal_x = nNormalized.x();
    pn.normal_y = nNormalized.y();
    pn.normal_z = nNormalized.z();
    pn.curvature = 0;
    return pn;
}
