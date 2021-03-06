#ifndef __CBN_DATAPATH_H__
#define __CBN_DATAPATH_H__

struct sockets {
	struct socket *rx;
	struct socket *tx;
};

struct addresses {
	struct sockaddr_in dest;
	struct sockaddr_in src;
	int mark;
};

#define UINT_SHIFT	32
static inline void* uint2void(uint32_t a, uint32_t b)
{
	return (void *)((((uint64_t)a)<<UINT_SHIFT)|b);
}

static inline void void2uint(void *ptr, uint32_t *a, uint32_t *b)
{
	uint64_t concat = (uint64_t)ptr;
	*b = ((concat << UINT_SHIFT) >> UINT_SHIFT);
	*a = (concat >> UINT_SHIFT);
}

#endif /*__CBN_DATAPATH_H__*/
