/*
 * micrec FILE SECONDS: record raw 48 kHz 16-bit mono PCM from /dev/redwood-mic (the kernel listens while
 * it is open; a read never waits) for SECONDS, reading every 10 ms. whyred testing.
 */
extern int open(const char *path, int flags, ...);
extern long read(int fd, void *buf, unsigned long n);
extern long write(int fd, const void *buf, unsigned long n);
extern int close(int fd);
extern int usleep(unsigned int us);
extern int printf(const char *fmt, ...);
extern int atoi(const char *s);
struct timeval { long tv_sec; int tv_usec; };
extern int gettimeofday(struct timeval *tv, void *tz);

static long long
now_us(void)
{
	struct timeval tv;
	gettimeofday(&tv, 0);
	return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

int
main(int argc, char **argv)
{
	static short buf[4800];
	long total = 0, first_us = -1;
	int peak = 0;

	if (argc < 3) {
		printf("usage: micrec FILE SECONDS\n");
		return 1;
	}
	int out = open(argv[1], 0x601, 0644);           /* O_WRONLY | O_CREAT | O_TRUNC */
	int mic = open("/dev/redwood-mic", 0x4);        /* O_RDONLY | O_NONBLOCK */
	if (out < 0 || mic < 0) {
		printf("micrec: cannot open %s\n", out < 0 ? argv[1] : "/dev/redwood-mic");
		return 1;
	}
	long long t0 = now_us(), end = t0 + 1000000LL * atoi(argv[2]);
	while (now_us() < end) {
		long n = read(mic, buf, sizeof(buf));
		if (n > 0) {
			if (first_us < 0) {
				first_us = (long)(now_us() - t0);
			}
			for (long i = 0; i < n / 2; i++) {
				int v = buf[i] < 0 ? -buf[i] : buf[i];
				peak = v > peak ? v : peak;
			}
			write(out, buf, (unsigned long)n);
			total += n;
		}
		usleep(10000);
	}
	close(mic);
	close(out);
	printf("micrec: %ld bytes (%ld.%02ld s), first after %ld ms, peak %d\n", total, total / 96000,
	    total % 96000 * 100 / 96000, first_us / 1000, peak);
	return 0;
}
