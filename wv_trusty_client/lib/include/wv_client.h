#pragma once
#include <trusty/tipc.h>

__BEGIN_DECLS
enum g2d_secure_mode {
    SECURE = 1,
    NON_SECURE = 2
};

void set_g2d_secure_pipe(int enable);
enum g2d_secure_mode get_g2d_secure_pipe();

__END_DECLS
