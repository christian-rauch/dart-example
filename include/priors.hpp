#ifndef PRIORS_HPP
#define PRIORS_HPP

#include <dart/tracker.h>

// publishing the prior gradient
#define LCM_DEBUG_GRADIENT

// filter fixed joints for shorter messages
// this will introduce a delay and should be compensated by skip publishing for
// a certain amount of iterations
#define FILTER_FIXED_JOINTS 0

// activate to print joint names and ids once at initialising the joint prior
//#define DBG_PRINT_JOINTS

namespace dart {

/**
 * @brief The NoCameraMovementPrior class
 * Prior to prevent movement of camera, e.g. set transformation of model to camera to 0.
 * This prior needs to be added last to enforce no transformation.
 */
class NoCameraMovementPrior : public Prior {
private:
    int _srcModelID;

public:
    NoCameraMovementPrior(const int srcModelID);

    void computeContribution(Eigen::SparseMatrix<float> & JTJ,
                             Eigen::VectorXf & JTe,
                             const int * modelOffsets,
                             const int priorParamOffset,
                             const std::vector<MirroredModel *> & models,
                             const std::vector<Pose> & poses,
                             const OptimizationOptions & opts);
};

class ReportedJointsPrior : public Prior {
private:
    // references to both pose sources for continuous updates
    const Pose &_reported;
    const Pose &_estimated;
    const int _modelID;
#if FILTER_FIXED_JOINTS
    unsigned int _skipped;
#endif

    /**
     * @brief computeGNParam compute parameter for Gauss-Newton
     * @param diff vector of differences in joint angles
     * @return tuple with Jacobian J and the gradient J^T*e
     */
    virtual std::tuple<Eigen::MatrixXf, Eigen::VectorXf> computeGNParam(const Eigen::VectorXf &diff) = 0;

#if FILTER_FIXED_JOINTS
    void setup();
#endif

#ifdef DBG_PRINT_JOINTS
    void printJointList();
#endif

protected:
    const double _weight;
    const Eigen::MatrixXf _Q;

public:
    /**
     * @brief ReportedJointsPrior constructor for scalar weights
     * @param modelID ID of model in DART tracker
     * @param reported reported pose
     * @param current estimated pose
     * @param weight scalar weight
     */
    explicit ReportedJointsPrior(const int modelID, const Pose &reported, const Pose &current, const double weight);

    /**
     * @brief ReportedJointsPrior constructor for weights in matrix form
     * @param modelID ID of model in DART tracker
     * @param reported reported pose
     * @param current estimated pose
     * @param Q square weight matrix with dimensions like joint vector
     */
    explicit ReportedJointsPrior(const int modelID, const Pose &reported, const Pose &current, const Eigen::MatrixXf Q);

    void computeContribution(Eigen::SparseMatrix<float> & fullJTJ,
                                 Eigen::VectorXf & fullJTe,
                                 const int * modelOffsets,
                                 const int priorParamOffset,
                                 const std::vector<MirroredModel *> & models,
                                 const std::vector<Pose> & poses,
                                 const OptimizationOptions & opts);
};

class WeightedL2NormOfError : public ReportedJointsPrior {
    using ReportedJointsPrior::ReportedJointsPrior;
private:
    std::tuple<Eigen::MatrixXf, Eigen::VectorXf> computeGNParam(const Eigen::VectorXf &diff);
};

class L2NormOfWeightedError : public ReportedJointsPrior {
    using ReportedJointsPrior::ReportedJointsPrior;
private:
    std::tuple<Eigen::MatrixXf, Eigen::VectorXf> computeGNParam(const Eigen::VectorXf &diff);
};

class QWeightedError : public ReportedJointsPrior {
    using ReportedJointsPrior::ReportedJointsPrior;
private:
    std::tuple<Eigen::MatrixXf, Eigen::VectorXf> computeGNParam(const Eigen::VectorXf &diff);
};

class SimpleWeightedError : public ReportedJointsPrior {
    using ReportedJointsPrior::ReportedJointsPrior;
private:
    std::tuple<Eigen::MatrixXf, Eigen::VectorXf> computeGNParam(const Eigen::VectorXf &diff);
};

}

#endif // PRIORS_HPP
