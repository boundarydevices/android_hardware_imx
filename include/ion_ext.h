/*
 *   Copyright 2017 NXP
 */

#ifndef _ION_EXT_H
#define _ION_EXT_H

static inline unsigned long ion_phys(int fd, ion_user_handle_t handle) {
    struct ion_phys_data local_ion_phys_data = {
        .handle = handle, .phys = 0,
    };

    struct ion_custom_data local_ion_custom_data = {
        .cmd = ION_IOC_PHYS, .arg = (unsigned long)&local_ion_phys_data,
    };

    return ioctl(fd, ION_IOC_CUSTOM, &local_ion_custom_data) ? 0 : local_ion_phys_data.phys;
}

#endif
