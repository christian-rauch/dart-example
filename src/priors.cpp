#include <priors.hpp>

#include <cmath>

// filtering fixed joints delays the prior computation and hence the framerate
// raise the GRADIENT_SKIP to skip publishing for certain amount of iterations
// set GRADIENT_SKIP to 0 to obtain all filtered gradients
#define GRADIENT_SKIP 8

#ifdef LCM_DEBUG_GRADIENT
#include <dart_lcm/lcm_provider_base.hpp>
#include <lcmtypes/bot_core/joint_angles_t.hpp>
#endif

dart::NoCameraMovementPrior::NoCameraMovementPrior(const int srcModelID) : _srcModelID(srcModelID) {}

void dart::NoCameraMovementPrior::computeContribution(Eigen::SparseMatrix<float> & JTJ,
                             Eigen::VectorXf & JTe,
                             const int * modelOffsets,
                             const int priorParamOffset,
                             const std::vector<MirroredModel *> & models,
                             const std::vector<Pose> & poses,
                             const OptimizationOptions & opts)
{
    // get offsets for selected model in the full parameter space
    const Pose & srcPose = poses[_srcModelID];
    const int srcDims = srcPose.getReducedDimensions();
    const int srcOffset = modelOffsets[_srcModelID];

    // get step in parameter space at current optimization state
    Eigen::VectorXf paramUpdate = JTJ.block(srcOffset,srcOffset,srcDims,srcDims).triangularView<Eigen::Upper>().solve(JTe.segment(srcOffset,srcDims));

    // set camera to model transformation (first 6 parameters) to zero
    paramUpdate.head<6>() = Eigen::VectorXf::Zero(6);

    // compute jacobian and error to achieve no movement of camera frame
    JTe.segment(srcOffset,srcDims) = JTJ.block(srcOffset,srcOffset,srcDims,srcDims) * paramUpdate;
}

dart::ReportedJointsPrior::ReportedJointsPrior(const int modelID, const Pose &reported, const Pose &current, const double weight)
    : _modelID(modelID), _reported(reported), _estimated(current), _weight(weight), _Q(Eigen::MatrixXf::Ones(1,1)) {
#if FILTER_FIXED_JOINTS
    setup();
#endif
#ifdef DBG_PRINT_JOINTS
    printJointList();
#endif
}

dart::ReportedJointsPrior::ReportedJointsPrior(const int modelID, const Pose &reported, const Pose &current, const Eigen::MatrixXf Q)
    : _modelID(modelID), _reported(reported), _estimated(current), _weight(1.0), _Q(Q) {
#if FILTER_FIXED_JOINTS
    setup();
#endif
#ifdef DBG_PRINT_JOINTS
    printJointList();
#endif
}

#if FILTER_FIXED_JOINTS
void dart::ReportedJointsPrior::setup() {
    _skipped = 0;
}
#endif

#ifdef DBG_PRINT_JOINTS
void dart::ReportedJointsPrior::printJointList() {
    std::cout<<"(estimated) joint index | joint name"<<std::endl;
    for(unsigned int i=0; i<_estimated.getReducedArticulatedDimensions(); i++) {
        std::cout<<i<<" "<<_estimated.getReducedName(i)<<std::endl;
    }
}

#endif

void dart::ReportedJointsPrior::computeContribution(Eigen::SparseMatrix<float> & fullJTJ,
                             Eigen::VectorXf & fullJTe,
                             const int * modelOffsets,
                             const int priorParamOffset,
                             const std::vector<MirroredModel *> & models,
                             const std::vector<Pose> & poses,
                             const OptimizationOptions & opts)
{
    // get mapping of reported joint names and values
    std::map<std::string, float> rep_map;
    for(unsigned int i=0; i<_reported.getReducedArticulatedDimensions(); i++) {
        // apply lower and upper joint limits
        rep_map[_reported.getReducedName(i)] =
                std::min(std::max(_reported.getReducedArticulation()[i], _reported.getReducedMin(i)), _reported.getReducedMax(i));
    }

#ifdef LCM_DEBUG_GRADIENT
    std::vector<std::string> names;
#if FILTER_FIXED_JOINTS
    const bool pub_grad = (_skipped==GRADIENT_SKIP);
#endif
#endif

    // compute difference of reported to estimated joint value
    Eigen::VectorXf diff = Eigen::VectorXf::Zero(_estimated.getReducedArticulatedDimensions());
    for(unsigned int i=0; i<_estimated.getReducedArticulatedDimensions(); i++) {
        const std::string jname = _estimated.getReducedName(i);
#ifdef LCM_DEBUG_GRADIENT
#if FILTER_FIXED_JOINTS
        if(pub_grad)
            if( !(_estimated.getReducedMin(i)==0 && _estimated.getReducedMin(i)==0) )
#endif
                names.push_back(jname);
#endif
        float rep = rep_map.at(jname);
        float est = _estimated.getReducedArticulation()[i];
        diff[i] = rep_map.at(jname) - _estimated.getReducedArticulation()[i];
    }

    // set nan values to 0, e.g. comparison of nan values always yields false
    diff = (diff.array()!=diff.array()).select(0,diff);

    // get Gauss-Newton parameter for specific objective function
    Eigen::MatrixXf J = Eigen::MatrixXf::Zero(_estimated.getReducedArticulatedDimensions(), 1);
    Eigen::VectorXf JTe = Eigen::VectorXf::Zero(_estimated.getReducedArticulatedDimensions());
    std::tie(J,JTe) = computeGNParam(diff);

    const Eigen::MatrixXf JTJ = J.transpose()*J;

#ifdef LCM_DEBUG_GRADIENT
#if FILTER_FIXED_JOINTS
    if(pub_grad) {
#endif
        // publish gradient (JTe)
        bot_core::joint_angles_t grad;
        grad.num_joints = names.size();
        grad.joint_name = names;
        for(unsigned int i = 0; i<JTe.size(); i++) {
#if FILTER_FIXED_JOINTS
            if(!(_estimated.getReducedMin(i)==0 && _estimated.getReducedMin(i)==0))
#endif
                grad.joint_position.push_back(JTe[i]);
        }
        LCM_CommonBase::publish("DART_GRADIENT", &grad);
#if FILTER_FIXED_JOINTS
        _skipped=0;
    }
    else {
        _skipped++;
    }
#endif
#endif // LCM_DEBUG_GRADIENT

    for(unsigned int r=0; r<JTJ.rows(); r++)
        for(unsigned int c=0; c<JTJ.cols(); c++)
            if(JTJ(r,c)!=0)
                fullJTJ.coeffRef(modelOffsets[_modelID]+6+r, modelOffsets[_modelID]+6+c) += JTJ(r,c);

    for(unsigned int r=0; r<JTe.rows(); r++)
            if(JTe[r]!=0)
                fullJTe[modelOffsets[_modelID]+6+r] += JTe[r];
}

std::tuple<Eigen::MatrixXf, Eigen::VectorXf> dart::WeightedL2NormOfError::computeGNParam(const Eigen::VectorXf &diff) {
    // compute error from joint deviation
    // error: weighted L2 norm of joint angle difference
    const float e = _weight * diff.norm();

    // Jacobian of error, e.g. the partial derivation of the error w.r.t. to each joint value
    // For an error of zero, its partial derivative is not defined. Therefore we set its derivative to 0.
    const Eigen::MatrixXf J = (diff.array()==0).select(0, -_weight * diff.array()/diff.norm());

    const Eigen::VectorXf JTe = J.array()*e;
    //const Eigen::VectorXf JTe = - _weight*_weight*diff.transpose();

    return std::make_tuple(J, JTe);
}

std::tuple<Eigen::MatrixXf, Eigen::VectorXf> dart::L2NormOfWeightedError::computeGNParam(const Eigen::VectorXf &diff) {
    // compute error from joint deviation
    // error: L2 norm of weighted joint angle difference
    const float e = (_weight * diff).norm();

    // Jacobian of error, e.g. the partial derivation of the error w.r.t. to each joint value
    // For an error of zero, its partial derivative is not defined. Therefore we set its derivative to 0.
    const Eigen::MatrixXf J = (diff.array()==0).select(0, - pow(_weight, 2) * diff.array()/diff.norm());

    const Eigen::VectorXf JTe = J.array()*e;

    return std::make_tuple(J, JTe);
}

std::tuple<Eigen::MatrixXf, Eigen::VectorXf> dart::QWeightedError::computeGNParam(const Eigen::VectorXf &diff) {
    // compute error from joint deviation
    const float e = diff.transpose()*_Q*diff;

    Eigen::MatrixXf deriv = Eigen::MatrixXf::Zero(diff.size(), 1);

    for(unsigned int i=0; i<diff.size(); i++) {
        // original derivation
        //deriv(i) = diff.dot(_Q.row(i)) + diff.dot(_Q.col(i)) - (diff[i]*_Q(i,i));
        // negative direction, this works
        //deriv(i) = - diff.dot(_Q.row(i)) + diff.dot(_Q.col(i)) - (diff[i]*_Q(i,i));
        deriv(i) = - ( diff.dot(_Q.row(i) + _Q.col(i).transpose()) - (diff[i]*_Q(i,i)) );
    }

    // Jacobian of error, e.g. the partial derivation of the error w.r.t. to each joint value
    // For an error of zero, its partial derivative is not defined. Therefore we set its derivative to 0.
    const Eigen::MatrixXf J = (diff.array()==0).select(0, deriv);

    const Eigen::VectorXf JTe = J.array()*e;

    return std::make_tuple(J, JTe);
}

std::tuple<Eigen::MatrixXf, Eigen::VectorXf> dart::SimpleWeightedError::computeGNParam(const Eigen::VectorXf &diff) {
    // compute error from joint deviation
    const double e = (_weight*diff).norm();

    // Jacobian of error, e.g. the partial derivation of the error w.r.t. to each joint value
    // For an error of zero, its partial derivative is not defined. Therefore we set its derivative to 0.
    const Eigen::MatrixXf J = (diff.array()==0).select(0, -diff.array()/e);

    const Eigen::VectorXf JTe = - diff.transpose();

    return std::make_tuple(J, JTe);
}
