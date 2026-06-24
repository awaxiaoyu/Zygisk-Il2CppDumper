//
// Created by Perfare on 2020/7/4.
//

#ifndef ZYGISK_IL2CPPDUMPER_HACK_H
#define ZYGISK_IL2CPPDUMPER_HACK_H

#include <jni.h>
#include <stddef.h>

void hack_prepare(JavaVM *vm, const char *game_data_dir, void *data, size_t length);

#endif //ZYGISK_IL2CPPDUMPER_HACK_H
