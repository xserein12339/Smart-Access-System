#ifndef SVC_FACE_FEATURE_H
#define SVC_FACE_FEATURE_H
#include "dal_err.h"
#ifdef __cplusplus
extern "C" {
#endif
dal_err_t svc_face_feature_init(void);
dal_err_t svc_face_feature_start(void);
dal_err_t svc_face_feature_stop(void);
#ifdef __cplusplus
}
#endif
#endif
