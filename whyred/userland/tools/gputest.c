/*
 * gputest: the RedwoodGPU service (msm DRM for Mesa) without Mesa. Reads the GPU parameters, makes a buffer,
 * maps it, submits a command buffer with CP_MEM_WRITE into the same buffer and waits for the fence, then times
 * N more submissions. Run as root: gputest [N]. Needs boot-arg gpu=1 or whyredgpusvc=1 (patch 1033).
 */
#include <stdint.h>

typedef unsigned int mach_port_t;
typedef mach_port_t io_object_t, io_service_t, io_connect_t;
typedef int kern_return_t;
typedef void *CFMutableDictionaryRef;
extern mach_port_t mach_task_self_;
CFMutableDictionaryRef IOServiceMatching(const char *name);
io_service_t IOServiceGetMatchingService(mach_port_t main, CFMutableDictionaryRef matching);
kern_return_t IOServiceOpen(io_service_t service, mach_port_t task, uint32_t type, io_connect_t *connect);
kern_return_t IOServiceClose(io_connect_t connect);
kern_return_t IOConnectCallStructMethod(io_connect_t c, uint32_t sel, const void *in, unsigned long in_size,
    void *out, unsigned long *out_size);
kern_return_t IOConnectMapMemory64(io_connect_t c, uint32_t type, mach_port_t task, uint64_t *address,
    uint64_t *size, uint32_t options);
int printf(const char *, ...);
int atoi(const char *);

struct msm_param { uint32_t pipe, param; uint64_t value; uint32_t len, pad; };
struct msm_gem_new { uint64_t size; uint32_t flags, handle; };
struct msm_gem_info { uint32_t handle, info; uint64_t value; uint32_t len, pad; };
struct msm_gem_submit_cmd { uint32_t type, submit_idx, submit_offset, size, pad, nr_relocs; uint64_t relocs; };
struct msm_gem_submit_bo { uint32_t flags, handle; uint64_t presumed; };
struct msm_gem_submit {
	uint32_t flags, fence, nr_bos, nr_cmds;
	uint64_t bos, cmds;
	int32_t fence_fd;
	uint32_t queueid;
	uint64_t in_syncobjs, out_syncobjs;
	uint32_t nr_in_syncobjs, nr_out_syncobjs, syncobj_stride, pad;
};
struct msm_wait_fence { uint32_t fence, flags; int64_t seconds, nanoseconds; uint32_t queueid; };

static io_connect_t conn;

static int
call(uint32_t sel, void *arg, unsigned long size)
{
	unsigned long out = size;
	return IOConnectCallStructMethod(conn, sel, arg, size, arg, &out);
}

static int
parity(uint32_t v)
{
	v ^= v >> 16;
	v ^= v >> 8;
	v ^= v >> 4;
	v &= 0xf;
	return (~0x6996 >> v) & 1;
}

static uint32_t
pkt7(uint32_t op, uint32_t cnt)
{
	return (7u << 28) | (cnt & 0x3fff) | ((uint32_t)parity(cnt) << 15) | ((op & 0x7f) << 16) |
	       ((uint32_t)parity(op) << 23);
}

static int
submit_wait(uint32_t handle, uint32_t words, uint32_t *fence)
{
	struct msm_gem_submit_bo bo = { 3, handle, 0 };         /* read | write */
	struct msm_gem_submit_cmd cmd = { 1, 0, 0, words * 4, 0, 0, 0 };   /* MSM_SUBMIT_CMD_BUF, bytes */
	struct msm_gem_submit s = { 0 };
	s.flags = 0x10;                                         /* MSM_PIPE_3D0 */
	s.nr_bos = 1;
	s.nr_cmds = 1;
	s.bos = (uint64_t)(unsigned long)&bo;
	s.cmds = (uint64_t)(unsigned long)&cmd;
	s.fence_fd = -1;
	int r = call(0x46, &s, sizeof(s));
	if (r != 0) {
		printf("gputest: SUBMIT: %#x\n", r);
		return r;
	}
	struct msm_wait_fence w = { s.fence, 0, 1ll << 40, 0, 0 };
	r = call(0x47, &w, sizeof(w));
	if (r != 0) {
		printf("gputest: WAIT_FENCE %u: %#x\n", s.fence, r);
	}
	*fence = s.fence;
	return r;
}

int
main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 100;
	io_service_t svc = IOServiceGetMatchingService(0, IOServiceMatching("RedwoodGPU"));
	if (svc == 0 || IOServiceOpen(svc, mach_task_self_, 0, &conn) != 0) {
		printf("gputest: no RedwoodGPU service (gpu=1 or whyredgpusvc=1?)\n");
		return 1;
	}
	static const char *names[] = { "", "GPU_ID", "GMEM_SIZE", "CHIP_ID", "MAX_FREQ", "TIMESTAMP", "GMEM_BASE",
	                               "PRIORITIES" };
	for (uint32_t p = 1; p <= 7; p++) {
		struct msm_param q = { 0, p, 0, 0, 0 };
		int r = call(0x40, &q, sizeof(q));
		printf("gputest: %s = %#llx%s\n", names[p], (unsigned long long)q.value, r ? " (error)" : "");
	}
	struct msm_gem_new g = { 0x10000, 0, 0 };
	if (call(0x42, &g, sizeof(g)) != 0) {
		printf("gputest: GEM_NEW failed\n");
		return 1;
	}
	struct msm_gem_info info = { g.handle, 1, 0, 0, 0 };    /* MSM_INFO_GET_IOVA */
	call(0x43, &info, sizeof(info));
	uint64_t va = 0, size = 0;
	int r = IOConnectMapMemory64(conn, g.handle, mach_task_self_, &va, &size, 1);
	printf("gputest: buffer %u: GPU address %#llx, mapped at %#llx (%llu bytes, ret %#x)\n", g.handle,
	    (unsigned long long)info.value, (unsigned long long)va, (unsigned long long)size, r);
	if (r != 0 || va == 0) {
		return 1;
	}
	volatile uint32_t *b = (volatile uint32_t *)(unsigned long)va;
	b[0x40] = 0;
	b[0] = pkt7(0x3d, 3);                                   /* CP_MEM_WRITE */
	b[1] = (uint32_t)(info.value + 0x100);
	b[2] = (uint32_t)((info.value + 0x100) >> 32);
	b[3] = 0x5a5a1234;
	uint32_t fence = 0;
	r = submit_wait(g.handle, 4, &fence);
	printf("gputest: fence %u: word %#x (want 0x5a5a1234) %s\n", fence, b[0x40],
	    r == 0 && b[0x40] == 0x5a5a1234 ? "OK" : "FAILED");
	if (b[0x40] != 0x5a5a1234) {
		return 1;
	}
	struct msm_param t0 = { 0, 5, 0, 0, 0 }, t1 = { 0, 5, 0, 0, 0 };
	call(0x40, &t0, sizeof(t0));
	int bad = 0;
	for (int i = 0; i < n; i++) {
		b[3] = 0x10000000u + (uint32_t)i;
		if (submit_wait(g.handle, 4, &fence) != 0 || b[0x40] != 0x10000000u + (uint32_t)i) {
			bad++;
		}
	}
	call(0x40, &t1, sizeof(t1));
	uint64_t ticks = t1.value - t0.value;
	printf("gputest: %d submissions, %d bad, %llu us (%llu us each, GPU always-on counter)\n", n, bad,
	    (unsigned long long)(ticks * 10 / 192), n ? (unsigned long long)(ticks * 10 / 192 / (uint64_t)n) : 0ull);
	IOServiceClose(conn);
	return bad != 0;
}
