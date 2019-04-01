#include "plane.h"

void Plane::setCoeffs(float a, float b, float c, float d)
{
    this->a = a;
    this->b = b;
    this->c = c;
    this->d = d;
}

void Plane::setCenter(vec4 p)
{
    this->center = p;
}

vec3 Plane::getNormal()
{
    vec3 n(a, b, c);
    n.normalize();
    return n;
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

float Plane::getStdDevWith(PointNormalKCloud::Ptr cloud, boost::shared_ptr<vector<int>> indices)
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

    float dev(0);
    for(float j: distances)
    {
        dev += std::pow(j - dist_mean, 2.0f);
    }
    dev /= static_cast<float>(distances.size());

    return std::sqrt(dev);
}

float Plane::distanceTo(PointNormalK p)
{
    vec3 p_tmp(p.x, p.y, p.z);
    return this->distanceTo(p_tmp);
}

float Plane::distanceTo(vec3 p)
{
    vec3 n(0, 0, 0);
    float d(0);
    this->cartesianToNormal(n, d);

    return fabs(n.dot(p) + d);
}

void Plane::cartesianToNormal(vec3 &n, float &d)
{
    vec3 v(a, b, c);
    d = this->d / v.norm();
    n = v.normalized();
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

    //cout << "Optimisation matrix dimension: " << m.rows() << " " << m.cols() << endl;

    // Compute svd decomposition
    Eigen::JacobiSVD<Eigen::MatrixXf> svd(m.block(0,0, 3, m.cols()), Eigen::ComputeThinU);
    Eigen::MatrixXf u = svd.matrixU();

    //cout << "U matrix dimension: " << u.rows() << " " << u.cols() << endl;

    //Extract plane parameters
    float a = u(0, 2);
    float b = u(1, 2);
    float c = u(2, 2);

    // Compute plane last parameter
    float d = - a * center.x() - b * center.y() - c * center.z();

    plane.setCoeffs(a, b, c, d);
    plane.setCenter(center);
}

bool Plane::pointInPlane(PointNormalK p, float epsilon)
{
    //return this->distanceTo(p) <= (2.0f*epsilon);
    vec3 v(p.x, p.y, p.z);
    return abs(vec3(a, b, c).dot(v) + d) <= (2.0f*epsilon);
}

bool Plane::normalInPlane(PointNormalK p, float max_angle)
{
    vec3 n = getNormal();
    vec3 pn(p.normal_x, p.normal_y, p.normal_z);

    //It may be necessary to reorient the normal vector.
    n = (acos(pn.dot(n)) <= acos(pn.dot(-n)))? n : -n;

    return fabs(acos(pn.dot(n))) <= max_angle;
}
