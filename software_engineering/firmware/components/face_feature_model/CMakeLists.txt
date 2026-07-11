idf_build_get_property(component_targets __COMPONENT_TARGETS)
if ("___idf_espressif__esp-dl" IN_LIST component_targets)
    idf_component_get_property(espdl_dir espressif__esp-dl COMPONENT_DIR)
elseif("___idf_esp-dl" IN_LIST component_targets)
    idf_component_get_property(espdl_dir esp-dl COMPONENT_DIR)
endif()
set(cmake_dir ${espdl_dir}/fbs_loader/cmake)
include(${cmake_dir}/utilities.cmake)

set(packed_model ${BUILD_DIR}/espdl_models/human_face_feat_mfn.espdl)

idf_component_register(
    SRCS "face_feature_model.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp-dl
)

if (IDF_TARGET STREQUAL "esp32p4")
    set(models_dir ${COMPONENT_DIR}/models/p4)
else()
    set(models_dir ${COMPONENT_DIR}/models/p4)
endif()

file(MAKE_DIRECTORY ${BUILD_DIR}/espdl_models)
set(models ${models_dir}/human_face_feat_mfn_s8_v1.espdl)

set(pack_model_exe ${espdl_dir}/fbs_loader/pack_espdl_models.py)
add_custom_command(
    OUTPUT ${packed_model}
    COMMENT "Pack face feature model..."
    COMMAND python ${pack_model_exe} --model_path ${models} --out_file ${packed_model}
    DEPENDS ${models}
    VERBATIM)

add_custom_target(face_feat_model ALL DEPENDS ${packed_model})
add_dependencies(flash face_feat_model)
esptool_py_flash_to_partition(flash "human_face_feat" ${packed_model})
