/*
 * amp_common.h（转发头文件）
 *
 * 共享定义只在工作空间根目录维护一份： ..\..\amp_common.h
 * 这里保留同名转发文件，使应用源码里的 #include "amp_common.h" 无需改路径，
 * 同时避免两份副本不同步。Vitis 编译时 -MMD 会把根目录头文件记为依赖，
 * 修改后重新 Build 即可生效。
 */
#ifndef AMP_COMMON_FORWARD_H
#define AMP_COMMON_FORWARD_H

#include "../../amp_common.h"

#endif /* AMP_COMMON_FORWARD_H */
