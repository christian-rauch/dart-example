#include "SegmentationPrior.hpp"
#include <dart/optimization/kernels/obsToMod.h>

//#define DBG_DA
#include <fstream>
#define PRNT_DA

#include <set>

#define SDF_DA
#define CLASSIF_DA

SegmentationPrior::SegmentationPrior(Tracker &tracker) : tracker(tracker), lcm() {
    if(lcm.good())
        lcm.subscribe("SEGMENTED_IMAGE", &SegmentationPrior::onImgClass, this);
    else
        throw std::runtime_error("LCM error in SegmentationPrior");
}

void SegmentationPrior::computeContribution(
        Eigen::SparseMatrix<float> & JTJ,
        Eigen::VectorXf & JTe,
        const int * modelOffsets,
        const int priorParamOffset,
        const std::vector<MirroredModel *> & models,
        const std::vector<Pose> & poses,
        const OptimizationOptions & opts)
{
    CheckCudaDieOnError();

    // check for new classified images, ignore prior otherwise
    if(lcm.handleTimeout(10)<=0)
        return;

    if(models.size()!=poses.size()) {
        throw std::runtime_error("models!=poses");
    }

    const uint h = tracker.getPointCloudSource().getDepthHeight();
    const uint w = tracker.getPointCloudSource().getDepthWidth();
    const uint ndata = w*h;

    mutex.lock();
    for(uint i(0); i<models.size(); i++) {

#ifdef SDF_DA
        int * dbg_da=NULL;
#ifdef DBG_DA // debug data association
        cudaMalloc(&dbg_da,ndata*sizeof(int));
        cudaMemset(dbg_da,0,ndata*sizeof(int));
#endif
        errorAndDataAssociation(
                    tracker.getPointCloudSource().getDeviceVertMap(),
                    tracker.getPointCloudSource().getDeviceNormMap(),
                    int(tracker.getPointCloudSource().getDepthWidth()),
                    int(tracker.getPointCloudSource().getDepthHeight()),
                    *models[i],
                    opts,
                    tracker.getOptimizer()->_dPts->hostPtr()[i],
                    tracker.getOptimizer()->_lastElements->devicePtr(),
                    tracker.getOptimizer()->_lastElements->hostPtr(),
                    dbg_da, NULL, NULL);

#ifdef DBG_DA // debug data association
        std::cout << ">>######## dbg DA " << i << std::endl;
        int *dbg_da_host = new int[ndata]();
        cudaMemcpy(dbg_da_host, dbg_da, ndata*sizeof(int),cudaMemcpyDeviceToHost);
//        for(uint id(0); id<ndata; id++) {
//            // index = x + y*width;
//            if(dbg_da_host[id]!=-1) {
//                std::cout << id << " d: " << dbg_da+id << std::endl;
//                std::cout << id << " h: " << &dbg_da_host[id] << std::endl;
//                std::cout << id << " " << dbg_da_host[id] << std::endl;
//            }
//        }
        Eigen::Matrix<int, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> da_mat(h,w);
        std::memcpy(da_mat.data(), dbg_da_host, ndata*sizeof(int));
        delete[] dbg_da_host;
        std::cout << "<<########" << std::endl;
        const static Eigen::IOFormat CSVFormat(Eigen::StreamPrecision, Eigen::DontAlignCols, " ", "\n");
        const std::string fname = "dbg_da_host.csv";
        std::ofstream csvfile(fname.c_str());
        csvfile << da_mat.format(CSVFormat);
        csvfile.close();
#endif
#endif
#ifdef CLASSIF_DA
        // reset _lastElements counter on host and device
        cudaMemset(tracker.getOptimizer()->_lastElements->devicePtr(),0,sizeof(int));
        tracker.getOptimizer()->_lastElements->syncDeviceToHost();
        CheckCudaDieOnError();

        // host copy of observed points
        std::vector<float4> points(ndata);
        cudaMemcpy(points.data(), tracker.getPointCloudSource().getDeviceVertMap(), ndata*sizeof(float4), cudaMemcpyDefault);
//        uint nverts_valid = 0;
//        for(const float4 &v : points) {
//            if(v.w>0) nverts_valid++;
//        }
//        std::cout << "valid nverts: " << nverts_valid << std::endl;
        CheckCudaDieOnError();

        //std::set<uint> valid_index;
        std::vector<uint> valid_index;
        std::vector<DataAssociatedPoint> dpoints;
        for(uint iw(0); iw<w; iw++) {
            for(uint ih(0); ih<h; ih++) {
                const uint index = iw + ih*w;
                //if(points[index].w>0) { valid_index.insert(index); }
                if(points[index].w>0) { valid_index.push_back(index); }
                if(points[index].w>0) {
                    DataAssociatedPoint da;
                    // get predicted class
                    da.index = index;
                    da.dataAssociation = img_class(ih,iw);
                    da.error = 0.05; // TODO: this is interpreted as the SDF
                    dpoints.push_back(da);
                }
            }
        }
        std::cout << "valid nverts: " << valid_index.size() << std::endl;

        CheckCudaDieOnError();

        // index = x + y*width;
//        for(uint h(0); h<img_class.rows(); h++) {
//            for(uint w(0); w<img_class.cols(); w++) {
//                if(img_class(h,w)!=0) {
//                    DataAssociatedPoint da;
//                    da.index = w + h*img_class.cols();
//                    da.dataAssociation = img_class(h,w);
//                    da.error = 0.5; // > huberDelta: 0.02
//                    dpoints.push_back(da);
//                }
//            }
//        }

        // set number of points, only used at host
        //(*tracker.getOptimizer()->_lastElements)[i] = dpoints.size();
        //(*tracker.getOptimizer()->_lastElements)[i] = 10; // nElements > 10
//        (*tracker.getOptimizer()->_lastElements)[i] = 44;
        //tracker.getOptimizer()->_lastElements->syncHostToDevice();
//        cudaMemcpy(tracker.getOptimizer()->_lastElements->devicePtr(),
//                   tracker.getOptimizer()->_lastElements->hostPtr(),
//                   sizeof(int), cudaMemcpyHostToDevice);
//        cudaMemcpy(tracker.getOptimizer()->_lastElements->hostPtr(),
//                   tracker.getOptimizer()->_lastElements->devicePtr(),
//                   sizeof(int), cudaMemcpyDeviceToHost);
        // sync data
//        cudaMemcpy((*tracker.getOptimizer()->_dPts)[i],
//                   dpoints.data(),
//                   dpoints.size()*sizeof(DataAssociatedPoint),
//                   cudaMemcpyHostToDevice);
        CheckCudaDieOnError();


//        std::cout << i << " model ID: " << models[i]->getModelID() << std::endl;
//        std::cout << "sdfs: " << models[i]->getNumSdfs() << std::endl;
//        //models[i]->getDeviceSdfFrames();
//        for(uint s(0); s<models[i]->getNumSdfs(); s++) {
//            std::cout << s << " " << models[i]->getSdfFrameNumber(s) << std::endl;
//        }

//        cudaMemset(tracker.getOptimizer()->_lastElements->devicePtr(),0,sizeof(int));
//        tracker.getOptimizer()->_lastElements->syncDeviceToHost();

        // ammend available data
        int le_h, le_d;
        cudaMemcpy(&le_d, tracker.getOptimizer()->_lastElements->devicePtr(),
                   sizeof(int),cudaMemcpyDeviceToHost);
        cudaMemcpy(&le_h, tracker.getOptimizer()->_lastElements->hostPtr(),
                   sizeof(int),cudaMemcpyHostToHost);
        std::cout << "le (h,d) " << le_h << ", " << le_d << std::endl;
        CheckCudaDieOnError();

        // test, copy forth and back
        DataAssociatedPoint *da;
        cudaMallocHost(&da, ndata*sizeof(DataAssociatedPoint));
        // dev to host
        cudaMemcpy(da, tracker.getOptimizer()->_dPts->hostPtr()[i], ndata*sizeof(DataAssociatedPoint), cudaMemcpyDeviceToHost);
        CheckCudaDieOnError();

        std::cout << tracker.getOptimizer()->_lastElements->hostPtr()[i] << std::endl;
        const int orgle = tracker.getOptimizer()->_lastElements->hostPtr()[i];
        tracker.getOptimizer()->_lastElements->hostPtr()[i] = 24000;
//        tracker.getOptimizer()->_lastElements->hostPtr()[i] = dpoints.size();

        // add some points
        //tracker.getOptimizer()->_lastElements->hostPtr()[i] += 10;

        tracker.getOptimizer()->_lastElements->syncHostToDevice();

        //for(uint ida(0); ida<ndata; ida++) {
        for(uint ida(0); ida<valid_index.size(); ida++) {
//            if(ida>=orgle) {
//                //da[ida].index = 263886; // seems to work partially for fake data (if w>0)
//                da[ida].index = 1000;
//                //da[ida].index = valid_index[0];
//            }
            //da[ida].index = valid_index[ida];
            da[ida].index = valid_index.at(ida);
            da[ida].dataAssociation = 5;
            //da[ida].error = 0.0005;
            //da[ida].error = 0.00475171;
            da[ida].error = 0.05;
        }

        //tracker.getOptimizer()->_lastElements->hostPtr()[i] = valid_index.size()-1;
        tracker.getOptimizer()->_lastElements->hostPtr()[i] = 25000;
        //tracker.getOptimizer()->_lastElements->hostPtr()[i] = 50000;
        //tracker.getOptimizer()->_lastElements->hostPtr()[i] = 100000;
        tracker.getOptimizer()->_lastElements->syncHostToDevice();

//        uint valid_ida = 0;
//        for(uint ida(0); ida<ndata; ida++) {
//            if(points[ida].w>0) {
//                da[valid_ida].index = ida;
//                da[valid_ida].dataAssociation = 5;
//                //da[ida].error = 0.0005;
//                da[valid_ida].error = 0.00475171;
//                //das.row(ida) << da[ida].index, da[ida].dataAssociation, da[ida].error;
//                valid_ida++;
//            }
//        }
//        tracker.getOptimizer()->_lastElements->hostPtr()[i] = valid_ida;
//        tracker.getOptimizer()->_lastElements->syncHostToDevice();

//        uint ida = 0;
//        for(uint iw(0); iw<w; iw++) { for(uint ih(0); ih<h; ih++) {
//                const uint index = iw + ih*w;
//                if(points[index].w>0) {
////                    DataAssociatedPoint da;
////                    // get predicted class
////                    da.index = index;
////                    da.dataAssociation = img_class(ih,iw);
////                    da.error = 0.05; // TODO: this is interpreted as the SDF
//                    da[ida].index = index;
//                    da[ida].dataAssociation = 5; // replace by img_class(ih,iw)
//                    //da[ida].error = 0.05;
//                    da[ida].error = 0;
//                    ida++;
//                }
//        } } // w,h
//        tracker.getOptimizer()->_lastElements->hostPtr()[i] = ida;
//        tracker.getOptimizer()->_lastElements->syncHostToDevice();

        // host to dev
        cudaMemcpy(tracker.getOptimizer()->_dPts->hostPtr()[i], da, ndata*sizeof(DataAssociatedPoint),cudaMemcpyHostToDevice);
        CheckCudaDieOnError();

//        delete [] da;
        cudaFreeHost(da);


        CheckCudaDieOnError();
        std::cout << ">> CLASSIF DA DONE" << std::endl;

#endif

        // compute gradient / Hessian
        float obsToModError = 0;
        Observation observation(tracker.getPointCloudSource().getDeviceVertMap(),
                                tracker.getPointCloudSource().getDeviceNormMap(),
                                int(tracker.getPointCloudSource().getDepthWidth()),
                                int(tracker.getPointCloudSource().getDepthHeight()));

        // enforce lambdaObsToMod = 1.0 for the unpack in 'computeObsToModContribution'
        // 'computeObsToModContribution' is private anyway
        OptimizationOptions fake_opts(opts);
        fake_opts.lambdaObsToMod = 1.0;

        DataAssociatedPoint *da2;
        cudaMallocHost(&da2, ndata*sizeof(DataAssociatedPoint));
        cudaMemcpy(da2, tracker.getOptimizer()->_dPts->hostPtr()[i], ndata*sizeof(DataAssociatedPoint), cudaMemcpyDeviceToHost);
        CheckCudaDieOnError();
        float4 *verts;
        cudaMallocHost(&verts, ndata*sizeof(float4));
        cudaMemcpy(verts, observation.dVertMap, ndata*sizeof(float4), cudaMemcpyDeviceToHost);
        CheckCudaDieOnError();
        for(uint ida(0); ida<tracker.getOptimizer()->_lastElements->hostPtr()[i]; ida++) {
            std::cout << ida << " " << da2[ida].index << " " << da2[ida].dataAssociation << " " << da2[ida].error << std::endl;
            //const float4 vert = observation.dVertMap[tracker.getOptimizer()->_dPts->hostPtr()[i][ida].index];
            //const float4 vert = observation.dVertMap[da2[ida].index];
            const float4 vert = verts[da2[ida].index];
            std::cout << vert.x << " " << vert.y << " " << vert.z << " " << vert.w << std::endl;
        }

        uint nverts = 0;
        for(uint iv(0); iv<ndata; iv++) {
            if(verts[iv].w > 0)
                nverts++ ;
        }
        std::cout << "verts: " << nverts << std::endl;

        cudaFreeHost(da2);
        cudaFreeHost(verts);
        CheckCudaDieOnError();

        Eigen::MatrixXf denseJTJ(JTJ);
        tracker.getOptimizer()->computeObsToModContribution(JTe,denseJTJ,obsToModError,*models[i],poses[i],fake_opts,observation);
        JTJ = denseJTJ.sparseView();
        CheckCudaDieOnError();
    }
    mutex.unlock();

    CheckCudaDieOnError();

#ifdef PRNT_DA
    // DBG
    //for(uint imod(0); imod<tracker.getOptimizer()->_maxModels; imod++) {
    for(uint imod(0); imod<models.size(); imod++) {
        //auto le = (*tracker.getOptimizer()->_lastElements)[imod];
        auto le = tracker.getOptimizer()->_lastElements->hostPtr()[imod];
        std::cout << "le: " << le << std::endl;
        //std::cout << imod << " d: " << (*tracker.getOptimizer()->_dPts)[imod] << std::endl;

        int lala;
        cudaMemcpy(&lala, &tracker.getOptimizer()->_lastElements->hostPtr()[imod],
                   sizeof(int),cudaMemcpyHostToHost);
        std::cout << "le (h) " << lala << std::endl;
        CheckCudaDieOnError();

        int le_h2, le_d2;
        cudaMemcpy(&le_d2, tracker.getOptimizer()->_lastElements->devicePtr(),
                   sizeof(int),cudaMemcpyDeviceToHost);
        cudaMemcpy(&le_h2, tracker.getOptimizer()->_lastElements->hostPtr(),
                   sizeof(int),cudaMemcpyHostToHost);
        std::cout << "le (h,d) " << le_h2 << ", " << le_d2 << std::endl;
        CheckCudaDieOnError();

        std::cout << "######## dPts DA " << imod << std::endl;
        //DataAssociatedPoint *da = new DataAssociatedPoint[ndata]();
        DataAssociatedPoint *da;
        cudaMallocHost(&da, ndata*sizeof(DataAssociatedPoint));
        CheckCudaDieOnError();
        //cudaMemcpy(da, (*tracker.getOptimizer()->_dPts)[imod], ndata*sizeof(DataAssociatedPoint),cudaMemcpyDeviceToHost);
        //const cudaError_t ret = cudaMemcpy(da, tracker.getOptimizer()->_dPts->hostPtr()[imod], ndata*sizeof(DataAssociatedPoint),cudaMemcpyDeviceToHost);
        const cudaError_t ret = cudaMemcpy(da, tracker.getOptimizer()->_dPts->hostPtr()[imod], 1*sizeof(DataAssociatedPoint),cudaMemcpyDeviceToHost);
        switch(ret) {
        case cudaSuccess:
            std::cout << "cudaSuccess" << std::endl; break;
        case cudaErrorInvalidValue:
            std::cout << "cudaErrorInvalidValue" << std::endl; break;
        case cudaErrorInvalidDevicePointer:
            std::cout << "cudaErrorInvalidDevicePointer" << std::endl; break;
        case cudaErrorInvalidMemcpyDirection:
            std::cout << "cudaErrorInvalidMemcpyDirection" << std::endl; break;
        default:
            std::cout << "unknown error (" << ret <<")" << std::endl; break;
        }

        if(ret != cudaSuccess) {
            //throw std::runtime_error("something went wrong during copying");
            std::cerr << "something went wrong during copying" << std::endl;
        }
        // CUDA error: an illegal memory access was encountered
        CheckCudaDieOnError();
        Eigen::Matrix<int, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> da_mat(h,w);
        da_mat.setConstant(-1);
        // index = x + y*width;
        for(uint j(0); j<le; j++) {
//            std::cout << da[j].index << " " << da[j].dataAssociation << " " << da[j].error << std::endl;
            if(da[j].index!=0) {
                std::cout << da[j].index << " " << da[j].dataAssociation << " " << da[j].error << std::endl;
            }
            const uint x = da[j].index%w;
//            CheckCudaDieOnError();
            const uint y = (x==0) ? da[j].index : da[j].index/w;
//            CheckCudaDieOnError();
            da_mat(y,x) = da[j].dataAssociation;
//            CheckCudaDieOnError();
        }
        cudaFreeHost(da);
        //delete [] da;
        const static Eigen::IOFormat CSVFormat(Eigen::StreamPrecision, Eigen::DontAlignCols, " ", "\n");
        const std::string fname = "dPts.csv";
        std::ofstream csvfile(fname.c_str());
        csvfile << da_mat.format(CSVFormat);
        csvfile.close();
    }
//    CheckCudaDieOnError();
#endif
}

void SegmentationPrior::onImgClass(const lcm::ReceiveBuffer* /*rbuf*/, const std::string& /*chan*/,  const img_classif_msgs::image_class_t* img_class_msg) {
    mutex.lock();
    img_class.resize(img_class_msg->height, img_class_msg->width);
    // copy row by row
    for(uint r(0); r<img_class.rows(); r++) {
        std::memcpy(img_class.row(r).data(), img_class_msg->class_id[r].data(), img_class_msg->width*sizeof(decltype(img_class)::Scalar));
    }
    mutex.unlock();
}