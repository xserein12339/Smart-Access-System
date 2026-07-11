#pragma once
#include "dl_model_base.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_feat_postprocessor.hpp"

class FaceFeatureModel {
public:
    FaceFeatureModel(bool do_minimize = false);
    ~FaceFeatureModel();

    int run(const dl::image::img_t &img, float *feature, int max_dim);

private:
    dl::Model *m_model;
    dl::image::ImagePreprocessor *m_preprocessor;
    dl::feat::FeatPostprocessor *m_postprocessor;
};
