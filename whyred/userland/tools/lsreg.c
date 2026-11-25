/*
 * lsreg: register app bundles with LaunchServices as system apps, the way Procursus uicache does
 * (-[LSApplicationWorkspace registerApplicationDictionary:], with a data container from
 * MobileContainerManager). Used for the removable system apps moved from /private/var/staged_system_apps
 * into /Applications: the SystemAppMigrator that installs them never runs here (data migration is off).
 * LaunchServices for apps lives in mobile's launchd domain, which an ssh shell (system domain) cannot
 * reach ("failed lookup: com.apple.lsd.mapdb"). iOS launchctl has no asuser/bsexec, so lsreg does what
 * bsexec does: it takes SpringBoard's bootstrap port, makes it its own and execs itself again.
 * Usage: LSREG_PID=<SpringBoard pid> lsreg /Applications/X.app ...   (lsreg -ent: show own signature state)
 */
typedef struct objc_class *Class;
typedef struct objc_object *id;
typedef struct objc_selector *SEL;
extern Class objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern void objc_msgSend(void);
extern void *dlopen(const char *path, int mode);
extern int printf(const char *fmt, ...);
extern const void *CFStringCreateWithCString(const void *alloc, const char *s, unsigned int encoding);
extern const char *CFStringGetCStringPtr(const void *s, unsigned int encoding);
extern int setuid(unsigned int uid);
extern int setgid(unsigned int gid);
extern unsigned int getuid(void);
typedef unsigned int mach_port_t;
extern mach_port_t mach_task_self_;
extern int task_for_pid(mach_port_t task, int pid, mach_port_t *out);
extern int task_get_special_port(mach_port_t task, int which, mach_port_t *port);
extern int task_set_special_port(mach_port_t task, int which, mach_port_t port);
extern int execv(const char *path, char *const argv[]);
extern char *getenv(const char *name);
extern int setenv(const char *name, const char *value, int overwrite);
extern int csops(int pid, unsigned int ops, void *buf, unsigned long size);
extern int getpid(void);
extern signed char CFStringGetCString(const void *s, char *buf, long size, unsigned int encoding);

#define MSG(ret, ...) ((ret (*)(__VA_ARGS__))objc_msgSend)
#define CLS(n) ((id)objc_getClass(n))
#define S(n) sel_registerName(n)

static id
str(const char *c)
{
	return (id)CFStringCreateWithCString(0, c, 0x08000100);
}

static const char *
cstr(id s, char *buf, long n)
{
	if (s == 0 || !CFStringGetCString(s, buf, n, 0x08000100)) {
		return "(null)";
	}
	return buf;
}

static void
put(id d, const char *k, id v)
{
	if (v != 0) {
		MSG(void, id, SEL, id, id)(d, S("setObject:forKey:"), v, str(k));
	}
}

int
main(int argc, char **argv)
{
	if (argc == 2 && argv[1][0] == '-') {
		/* -ent: what the kernel reports for this process (status flags, XML and DER entitlement blobs) */
		static unsigned char blob[65536];
		unsigned int st = 0;
		int r0 = csops(getpid(), 0, &st, sizeof(st));
		int r1 = csops(getpid(), 7, blob, sizeof(blob));
		unsigned int len1 = (unsigned)blob[4] << 24 | (unsigned)blob[5] << 16 | (unsigned)blob[6] << 8 | blob[7];
		blob[4] = blob[5] = blob[6] = blob[7] = 0;
		int r2 = csops(getpid(), 16, blob, sizeof(blob));
		unsigned int len2 = (unsigned)blob[4] << 24 | (unsigned)blob[5] << 16 | (unsigned)blob[6] << 8 | blob[7];
		printf("status %d %#x, xml blob %d len %u, der blob %d len %u\n", r0, st, r1, len1, r2, len2);
		return 0;
	}
	if (getenv("LSREG_IN_USER") == 0) {
		/* SpringBoard's pid comes from the caller (LSREG_PID) */
		char *pidstr = getenv("LSREG_PID");
		int pid = 0;
		for (char *c = pidstr; c && *c >= '0' && *c <= '9'; c++) {
			pid = pid * 10 + (*c - '0');
		}
		mach_port_t t = 0, bp = 0;
		int kr = pid ? task_for_pid(mach_task_self_, pid, &t) : -1;
		int kr2 = kr == 0 ? task_get_special_port(t, 4, &bp) : -1;      /* TASK_BOOTSTRAP_PORT */
		int kr3 = kr2 == 0 ? task_set_special_port(mach_task_self_, 4, bp) : -1;
		if (kr3 != 0) {
			printf("bootstrap of pid %d: task_for_pid %d, get %d, set %d\n", pid, kr, kr2, kr3);
			return 1;
		}
		setenv("LSREG_IN_USER", "1", 1);
		execv(argv[0], argv);
		printf("exec failed\n");
		return 1;
	}
	/* app data containers and the app database are mobile's (501) */
	if (getuid() == 0 && (setgid(501) != 0 || setuid(501) != 0)) {
		printf("could not become mobile\n");
		return 1;
	}
	dlopen("/System/Library/Frameworks/CoreServices.framework/CoreServices", 1);
	dlopen("/System/Library/PrivateFrameworks/MobileContainerManager.framework/MobileContainerManager", 1);
	id ws = MSG(id, id, SEL)(CLS("LSApplicationWorkspace"), S("defaultWorkspace"));
	int bad = 0;
	for (int i = 1; i < argc; i++) {
		char b1[256], b2[512];
		id path = str(argv[i]);
		id info = MSG(id, id, SEL, id)(CLS("NSDictionary"), S("dictionaryWithContentsOfFile:"),
		    MSG(id, id, SEL, id)(path, S("stringByAppendingPathComponent:"), str("Info.plist")));
		id bid = info ? MSG(id, id, SEL, id)(info, S("objectForKey:"), str("CFBundleIdentifier")) : 0;
		if (bid == 0) {
			printf("%s: no CFBundleIdentifier\n", argv[i]);
			bad++;
			continue;
		}
		id d = MSG(id, id, SEL)(CLS("NSMutableDictionary"), S("dictionary"));
		put(d, "ApplicationType", str("System"));
		put(d, "CFBundleIdentifier", bid);
		put(d, "CodeInfoIdentifier", bid);
		put(d, "CompatibilityState", MSG(id, id, SEL, int)(CLS("NSNumber"), S("numberWithInt:"), 0));
		put(d, "IsDeletable", MSG(id, id, SEL, signed char)(CLS("NSNumber"), S("numberWithBool:"), 0));
		put(d, "Path", path);
		put(d, "SignerOrganization", str("Apple Inc."));
		put(d, "_LSBundlePlugins", MSG(id, id, SEL)(CLS("NSDictionary"), S("dictionary")));
		/* data container, as installd would make for it */
		id err = 0;
		id c = MSG(id, id, SEL, id, signed char, signed char *, id *)(CLS("MCMAppDataContainer"),
		    S("containerWithIdentifier:createIfNecessary:existed:error:"), bid, 1, 0, &err);
		id curl = c ? MSG(id, id, SEL)(c, S("url")) : 0;
		id cpath = curl ? MSG(id, id, SEL)(curl, S("path")) : 0;
		if (cpath != 0) {
			put(d, "Container", cpath);
			id env = MSG(id, id, SEL)(CLS("NSMutableDictionary"), S("dictionary"));
			put(env, "CFFIXED_USER_HOME", cpath);
			put(env, "HOME", cpath);
			put(env, "TMPDIR", MSG(id, id, SEL, id)(cpath, S("stringByAppendingPathComponent:"), str("tmp")));
			put(d, "EnvironmentVariables", env);
			put(d, "IsContainerized", MSG(id, id, SEL, signed char)(CLS("NSNumber"), S("numberWithBool:"), 1));
		}
		signed char ok = MSG(signed char, id, SEL, id)(ws, S("registerApplicationDictionary:"), d);
		printf("%s (%s): %s, container %s%s%s\n", argv[i], cstr(bid, b1, sizeof(b1)), ok ? "registered" : "FAILED",
		    cstr(cpath, b2, sizeof(b2)), err ? ", container error " : "",
		    err ? cstr(MSG(id, id, SEL)(err, S("description")), b2, sizeof(b2)) : "");
		bad += !ok;
	}
	return bad;
}
