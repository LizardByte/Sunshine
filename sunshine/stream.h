//
// Created by loki on 6/5/19.
//

#ifndef SUNSHINE_STREAM_H
#define SUNSHINE_STREAM_H

#include "crypto.h"

namespace stream {

//FIXME: Make thread safe
extern crypto::aes_t gcm_key;
extern crypto::aes_t iv;
extern std::string app_name;

void rtpThread();

}

#endif //SUNSHINE_STREAM_H
