#include "face_feature_model.hpp"
#include "esp_log.h"

#define TAG "FEAT_MODEL"
#define MODEL_PARTITION  "human_face_feat"
#define MODEL_FILENAME   "human_face_feat_mfn_s8_v1.espdl"

FaceFeatureModel::FaceFeatureModel(bool do_minimize) :
    m_model(NULL), m_preprocessor(NULL), m_postprocessor(NULL)
{
    m_model = new dl::Model(MODEL_PARTITION, MODEL_FILENAME,
                            fbs::MODEL_LOCATION_IN_FLASH_PARTITION);
    if (do_minimize) m_model->minimize();

    m_preprocessor = new dl::image::ImagePreprocessor(
        m_model, {127.5f, 127.5f, 127.5f}, {127.5f, 127.5f, 127.5f}, true);
    m_postprocessor = new dl::feat::FeatPostprocessor(m_model);
}

FaceFeatureModel::~FaceFeatureModel()
{
    delete m_postprocessor;
    delete m_preprocessor;
    delete m_model;
}

int FaceFeatureModel::run(const dl::image::img_t &img, float *feature, int max_dim)
{
    if (m_model == NULL || m_preprocessor == NULL || m_postprocessor == NULL)
        return 0;

    m_preprocessor->preprocess(img);
    m_model->run();

    dl::TensorBase *out = m_postprocessor->postprocess();
    if (out == NULL) return 0;

    float *data = static_cast<float *>(out->get_element_ptr());
    int dim = out->get_size();
    if (dim <= 0) return 0;
    if (dim > max_dim) dim = max_dim;

    ESP_LOGI(TAG, "feat[0..3]=%.4f %.4f %.4f %.4f dim=%d",
             data[0], data[1], data[2], data[3], dim);

    memcpy(feature, data, (size_t)dim * sizeof(float));
    return dim;
}
