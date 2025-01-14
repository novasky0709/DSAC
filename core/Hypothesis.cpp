/*
Copyright (c) 2016, TU Dresden
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the TU Dresden nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL TU DRESDEN BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "Hypothesis.h"
#include "../core/dataset.h"
/*
多态，对不同输入，给出Hypothesis结构
*/
/*
什么都不输入的情况，给旋转矩阵置E，给位移矩阵置（0，0，0）
*/
Hypothesis::Hypothesis() 
{
    this->translation = cv::Point3d(0, 0, 0);
    this->rotation = cv::Mat::eye(3, 3, CV_64F);
    this->invRotation = cv::Mat::eye(3, 3, CV_64F);
}
/*一个正常输入的情况，对rotation、translation正常赋值*/
Hypothesis::Hypothesis(cv::Mat rot,cv::Point3d trans)
{
    this->translation = trans;
    this->rotation = rot;
    this->invRotation = this->rotation.inv();
}
/*输入的类型是jp::info_t.Whats jp::info_t*/
Hypothesis::Hypothesis(jp::info_t info)
{
    cv::Mat rot(3, 3, CV_64F);
    
    for(unsigned x = 0; x < 3; x++)
    for(unsigned y = 0; y < 3; y++)
	rot.at<double>(y, x) = info.rotation(y, x);
    
    cv::Point3d trans(info.center[0] * 1e3, info.center[1] * 1e3, info.center[2] * 1e3); // convert meter in mm
    
    this->translation = trans;
    this->rotation = rot;
    this->invRotation = this->rotation.inv();
}
/*输入类型是T，旋转矩阵，一个常规的分解*/
Hypothesis::Hypothesis(cv::Mat transform) 
{
    this->translation = cv::Point3d(0, 0, 0);
    this->rotation = cv::Mat::eye(3, 3, CV_64F);
    this->invRotation = cv::Mat::eye(3, 3, CV_64F);
  
    for(int a = 0; a < 3; a++)
    for(int b = 0; b < 3; b++)
	this->rotation.at<double>(a, b) = transform.at<double>(a, b);
    
    this->translation.x = transform.at<double>(0, 3);
    this->translation.y = transform.at<double>(1, 3);
    this->translation.z = transform.at<double>(2, 3);
    this->invRotation = this->rotation.inv();
}
/*如果输入的是一组Hypothtsis，执行refine操作*/
Hypothesis::Hypothesis(std::vector<std::pair<cv::Point3d, cv::Point3d>> points) 
{
    refine(points);
}
/*I suppose：输入是旋转向量（3d）和位移向量（3d）*/
Hypothesis::Hypothesis(std::vector<double> rodVecAndTrans ) 
{
    assert (rodVecAndTrans.size() == 6);
    this->translation = cv::Point3d(rodVecAndTrans[3], rodVecAndTrans[4], rodVecAndTrans[5]);
    
    cv::Mat rodVec(3, 1, CV_64F);
    rodVec.at<double>(0,0) = rodVecAndTrans[0];
    rodVec.at<double>(1,0) = rodVecAndTrans[1];
    rodVec.at<double>(2,0) = rodVecAndTrans[2];
    
    double length = sqrt(pow(rodVecAndTrans[0], 2) + pow(rodVecAndTrans[1], 2) + pow(rodVecAndTrans[2], 2));
    
    if(length>1e-5)
	cv::Rodrigues(rodVec, this->rotation);
    else
      this->rotation = cv::Mat::eye(3, 3, CV_64F);
    
    this->invRotation = this->rotation.inv();
}
/*
开始进入函数实现部分！
*/

void Hypothesis::setRotation(cv::Mat rot)
{
    this->rotation = rot;
    this->invRotation = this->rotation.inv();
}

void Hypothesis::setTranslation(cv::Point3d trans)
{
    this->translation = trans;
}
/*这个方法是值得学习的，在transform这个函数中加入一个参数isNormal来做标准化的逻辑*/
cv::Point3d Hypothesis::transform(cv::Point3d p, bool isNormal) 
{
    cv::Mat tpm = this->rotation * cv::Mat(p); // apply rotation
    
    cv::Point3d tp(tpm.at<double>(0, 0),
                   tpm.at<double>(1, 0),
                   tpm.at<double>(2, 0));
    if(!isNormal)
        return tp + this->translation;	// apply translation
    else
        return tp;	// apply no translation
}
/*从这个函数可以看出来，他对于mean是个必选项，但是isNormal是个非必选项，这也是个必选的trick*/
cv::Point3d Hypothesis::invTransform(cv::Point3d p) 
{
    p -= this->translation;	// apply translation
    
    cv::Mat tpm=(this->invRotation) * cv::Mat(p);	// apply rotation
    
    cv::Point3d tp(tpm.at<double>(0, 0),
                   tpm.at<double>(1, 0),
                   tpm.at<double>(2, 0));
    return tp;
}
/*从这个源码可以看到，cv其实有自己的trace，并且之前处理策略是正确的，保acos取值-1，1。超了还不管*/
double Hypothesis::calcAngularDistance(const Hypothesis& h)  const
{
    cv::Mat rotDiff = this->getRotation() * h.getInvRotation();
    double trace = cv::trace(rotDiff)[0];
    trace = std::min(3.0, std::max(-1.0, trace));
    return 180 * acos((trace - 1.0) / 2.0) / CV_PI;
}

std::pair<cv::Mat, cv::Point3d> Hypothesis::calcRigidBodyTransform(std::vector<std::pair<cv::Point3d, cv::Point3d>> points) 
{
    cv::Point3d cA(0, 0, 0);
    cv::Point3d cB(0, 0, 0);
    cv::Mat pointsA(3, points.size(), CV_64F);
    cv::Mat pointsB(3, points.size(), CV_64F);

    for(auto it = points.begin(); it != points.end(); ++it) 
    {
        cA += (it->first);
        cB += (it->second);
    }
    cA *= (1.0 / (double) points.size());
    cB *= (1.0 / (double) points.size());

    int i = 0;
    for(auto it = points.begin(); it != points.end(); ++it) 
    {
        pointsA.at<double>(0, i) = it->first.x - cA.x;
        pointsB.at<double>(0, i) = it->second.x - cB.x;
        pointsA.at<double>(1, i) = it->first.y - cA.y;
        pointsB.at<double>(1, i) = it->second.y - cB.y;
        pointsA.at<double>(2, i) = it->first.z - cA.z;
        pointsB.at<double>(2, i) = it->second.z - cB.z;
        i++;
    }
    cv::Mat a = pointsA * (pointsB.t());

    return calcRigidBodyTransform(a, cA, cB);
}

std::pair<cv::Mat, cv::Point3d> Hypothesis::calcRigidBodyTransform(cv::Mat& coV, cv::Point3d cA, cv::Point3d cB)
{
    cv::SVD svd(coV);
    cv::Mat u = svd.u;
    cv::Mat vt = svd.vt;
    cv::Mat v = vt.t();

    // need to flip rotation?
    double sign = 1;
    if(cv::determinant((v*u.t())) < 0)
        sign = -1;
    
    cv::Mat dm(3, 3, CV_64F, 0.0);
    dm.at<double>(0, 0) = 1;
    dm.at<double>(1, 1) = 1;
    dm.at<double>(2, 2) = sign;
    
    cv::Mat resultRot = v * dm * u.t();
    cv::Mat temp = ((-resultRot) * cv::Mat(cA)) + cv::Mat(cB);
    cv::Point3d resultTrans(temp.at<double>(0, 0),
                            temp.at<double>(1, 0),
                            temp.at<double>(2, 0));
    
    return std::pair<cv::Mat, cv::Point3d>(resultRot, resultTrans);
}

void Hypothesis::refine(std::vector<std::pair<cv::Point3d, cv::Point3d>> points) 
{
    this->points.insert(this->points.end(), points.begin(), points.end());
    std::pair<cv::Mat, cv::Point3d> estimates = calcRigidBodyTransform(points);
    this->rotation = estimates.first;
    this->translation = estimates.second;
    this->invRotation = this->rotation.inv();
}

void Hypothesis::refine(cv::Mat& coV, cv::Point3d pointsA, cv::Point3d pointsB)
{
    std::pair<cv::Mat,cv::Point3d> estimates = calcRigidBodyTransform(coV, pointsA, pointsB);
    this->rotation = estimates.first;
    this->translation = estimates.second;
    this->invRotation = this->rotation.inv();
}

cv::Point3d Hypothesis::getTranslation() const 
{
    return this->translation;
}

cv::Mat Hypothesis::getRotation() const 
{
    return this->rotation;
}

cv::Mat Hypothesis::getInvRotation() const 
{
    return this->invRotation;
}
/*转换成一个4D矩阵*/
cv::Mat Hypothesis::getTransformation() const 
{
    cv::Mat result(4, 4, CV_64F, 0.0);
    
    for(int a = 0; a < 3; a++)
    for(int b = 0; b < 3; b++)
	result.at<double>(a, b) = this->rotation.at<double>(a, b);
    
    result.at<double>(0, 3) = this->translation.x;
    result.at<double>(1, 3) = this->translation.y;
    result.at<double>(2, 3) = this->translation.z;
    result.at<double>(3, 3) = 1.0;
    return result;
}

Hypothesis Hypothesis::getInv() 
{
    Hypothesis h((this->getTransformation()).inv());
    return h;
}

Hypothesis Hypothesis::operator*(const Hypothesis& other) const 
{
    Hypothesis h(this->getTransformation() * other.getTransformation());
    return h;
}

Hypothesis Hypothesis::operator/(const Hypothesis& other) const 
{
    Hypothesis h(this->getTransformation() * other.getTransformation().inv());
    return h;
}

cv::Mat Hypothesis::getRodriguesVector() const 
{
    cv::Mat result;
    cv::Rodrigues(this->rotation, result);
    return result;
}

std::vector<double> Hypothesis::getRodVecAndTrans() const 
{
  std::vector<double> result;
  result.resize(6);
  cv::Mat rv = getRodriguesVector();
  
  result[0] = rv.at<double>(0,0); 
  result[1] = rv.at<double>(0,1); 
  result[2] = rv.at<double>(0,2);
  
  result[3] = this->translation.x; 
  result[4] = this->translation.y; 
  result[5] = this->translation.z;
  
  return result;
}

Hypothesis::~Hypothesis() {}
