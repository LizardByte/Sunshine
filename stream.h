//
// Created by loki on 6/5/19.
//

#ifndef SUNSHINE_STREAM_H
#define SUNSHINE_STREAM_H

#include "crypto.h"

namespace stream {

extern crypto::aes_t gcm_key;
extern crypto::aes_t iv;

void rtpThread();

}

#endif //SUNSHINE_STREAM_H
