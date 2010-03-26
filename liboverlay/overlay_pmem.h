#ifndef __OVERLAY_H__
#define __OVERLAY_H__

#include "overlay_utils.h"
#define DEFAULT_PMEM_ALIGN (4096)
#define PMEM_DEV "/dev/pmem_adsp"
#define MAX_SLOT 64

class PmemAllocator: public OverlayAllocator
{
public:
    PmemAllocator(int bufCount,int bufSize);
    virtual ~PmemAllocator();
    virtual int allocate(OVERLAY_BUFFER *overlay_buf, int size);
    virtual int deAllocate(OVERLAY_BUFFER *overlay_buf);
    virtual int getHeapID(){  return mFD;  }
private:
    int mFD;
    unsigned long mTotalSize;
    int mBufCount;
    int mBufSize;
    void *mVirBase;
    unsigned int mPhyBase;
    bool mSlotAllocated[MAX_SLOT];
};

#endif
