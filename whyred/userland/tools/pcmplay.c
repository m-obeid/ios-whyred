/*
 * pcmplay FILE [DEVICE]: play raw 48 kHz 16-bit stereo PCM through the kernel audio device (default
 * /dev/topaz-audio), 20 ms at a time at the real-time pace (the device never waits). whyred testing.
 */
extern int open(const char *path, int flags, ...);
extern long read(int fd, void *buf, unsigned long n);
extern long write(int fd, const void *buf, unsigned long n);
extern int close(int fd);
extern int usleep(unsigned int us);
extern int printf(const char *fmt, ...);
struct timeval { long tv_sec; int tv_usec; };
extern int gettimeofday(struct timeval *tv, void *tz);

static long long
now_us(void)
{
	struct timeval tv;
	gettimeofday(&tv, 0);
	return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

#define CHUNK (48 * 4 * 20)                     /* 20 ms */

int
main(int argc, char **argv)
{
	static char buf[CHUNK];
	long n, total = 0;

	if (argc < 2) {
		printf("usage: pcmplay FILE [DEVICE]\n");
		return 2;
	}
	int in = open(argv[1], 0);
	int out = open(argc > 2 ? argv[2] : "/dev/topaz-audio", 1);
	if (in < 0 || out < 0) {
		printf("pcmplay: cannot open %s\n", in < 0 ? argv[1] : (argc > 2 ? argv[2] : "/dev/topaz-audio"));
		return 1;
	}
	long long next = now_us();
	while ((n = read(in, buf, CHUNK)) > 0) {
		if (write(out, buf, (unsigned long)(n & ~3L)) < 0) {
			printf("pcmplay: write failed\n");
			return 1;
		}
		total += n;
		next += 20000;                  /* on the clock: no drift behind real time */
		long long wait = next - now_us();
		if (wait > 0) {
			usleep((unsigned int)wait);
		}
	}
	printf("pcmplay: %ld bytes (%ld ms)\n", total, total / (48 * 4));
	close(out);
	close(in);
	return 0;
}
