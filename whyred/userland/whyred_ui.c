/*
 * whyred_ui.dylib: UIKit fixes for the 1080x2160 panel, loaded into UIKit processes by the
 * kernel's kern.exec_inject (boot-arg topaz_inject, patch 0919).
 *
 * Status bar: +[_UIStatusBarVisualProvider_iOS visualProviderSubclassForScreen:visualProviderInfo:]
 * picks a layout from the screen's exclusion area and size. 360x720 points matches no iPhone, so
 * _UIStatusBarVisualProvider_Split returns nil (measured) and UIKit draws the pink "rdar" placeholder.
 * Then this returns _UIStatusBarVisualProvider_LegacyPhone (the iPhone 8 bar) instead. The choice is wrapped
 * on _iOS, _Phone, _Split and _DynamicSplit (callers can start at any of them). Any screen UIKit
 * knows keeps its own choice. Same idea as danstronics/ios-s9plus-exynos display_native.c.
 *
 * Keyboard: UIKit's keyboard size providers answer 320 pt wide (iPhone SE) on the 360-pt screen.
 * Each keyplane change then sets a keyboard frame of another size, UIKit takes that as a layout
 * change, reloads the keyboard and goes back to the first keyplane: 123 / abc did nothing. The
 * providers are wrapped to answer the screen width (as danstronics keyboard_native.inc). They are
 * found with class_copyMethodList: class_getClassMethod runs +initialize inside this constructor,
 * which recursed until the stack ran out.
 *
 * Built without an iOS SDK: the few runtime functions are declared here and linked against
 * text stubs (whyred/userland/stubs).
 */
#include <stddef.h>

typedef struct objc_class *Class;
typedef struct objc_object *id;
typedef struct objc_selector *SEL;
typedef struct objc_method *Method;
typedef void (*IMP)(void);

extern Class objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern Method class_getClassMethod(Class cls, SEL sel);
extern IMP method_getImplementation(Method m);
extern IMP method_setImplementation(Method m, IMP imp);
extern Method class_getInstanceMethod(Class cls, SEL sel);
extern const char *object_getClassName(id obj);
extern void objc_msgSend(void);
extern const char *class_getName(Class cls);
extern const char *sel_getName(SEL sel);
extern const char *method_getTypeEncoding(Method m);
extern Method *class_copyMethodList(Class cls, unsigned int *count);
extern SEL method_getName(Method m);
extern Class object_getClass(id obj);
extern void free(void *p);
extern const char *getprogname(void);
extern int open(const char *path, int flags, ...);
extern long write(int fd, const void *buf, unsigned long n);
extern int close(int fd);

static void
say(const char *a, const char *b)
{
	int fd = open("/dev/console", 1);       /* O_WRONLY */
	if (fd < 0) {
		return;
	}
	const char *parts[] = { "whyred_ui: ", getprogname(), ": ", a, b, "\n" };
	for (unsigned i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
		size_t n = 0;
		while (parts[i][n]) {
			n++;
		}
		(void)write(fd, parts[i], n);
	}
	close(fd);
}

/*
 * The layout choice runs through +visualProviderSubclassForScreen:visualProviderInfo: on several
 * classes (_iOS -> _Phone -> _Split / _DynamicSplit, via _UIStatusBarGetVisualProviderSubclass...),
 * and callers may start at any of them. Each one is wrapped: Fallback becomes LegacyPhone.
 */
#define HOOKS 4
static const char *const hook_names[HOOKS] = {
	"_UIStatusBarVisualProvider_iOS", "_UIStatusBarVisualProvider_Phone",
	"_UIStatusBarVisualProvider_Split", "_UIStatusBarVisualProvider_DynamicSplit",
};
typedef Class (*provider_fn)(Class self, SEL cmd, id screen, id info);
static provider_fn orig[HOOKS];
static Class fallback_class, legacy_class;
static int calls[HOOKS], swaps;

static Class
wrap(int i, Class self, SEL cmd, id screen, id info)
{
	Class c = orig[i](self, cmd, screen, info);
	if (calls[i]++ < 2) {
		say(hook_names[i], c ? class_getName(c) : " -> nil");
	}
	/* Split / DynamicSplit return nil when no size class matches (360x720 pt); the caller then
	 * draws the placeholder. Fallback itself is swapped wherever it shows up. */
	if ((c == fallback_class || (c == NULL && i >= 2)) && legacy_class != NULL) {
		if (swaps++ == 0) {
			say("status bar -> LegacyPhone in ", hook_names[i]);
		}
		return legacy_class;
	}
	return c;
}

static Class w0(Class s, SEL c, id a, id b) { return wrap(0, s, c, a, b); }
static Class w1(Class s, SEL c, id a, id b) { return wrap(1, s, c, a, b); }
static Class w2(Class s, SEL c, id a, id b) { return wrap(2, s, c, a, b); }
static Class w3(Class s, SEL c, id a, id b) { return wrap(3, s, c, a, b); }
static const provider_fn wrappers[HOOKS] = { w0, w1, w2, w3 };

/*
 * Keyboard trace (as danstronics kbtrace): the 123 / abc keys (keyplane switches) do nothing,
 * and the first keyboard shows an iPad-style ".?123" key. Log every keyplane change and what
 * -[UIKeyboardLayoutStar keyHitTest:] returns for taps in the lower part of the keyboard.
 */
typedef struct { double x, y; } pt_t;
static void (*orig_set_plane)(id, SEL, id);
static id (*orig_hit)(id, SEL, pt_t);
static int plane_logs, hit_logs;

static const char *
utf8(id str)
{
	if (str == NULL) {
		return "nil";
	}
	const char *r = ((const char *(*)(id, SEL))objc_msgSend)(str, sel_registerName("UTF8String"));
	return r ? r : "?";
}

static const char *
num(char *buf, double v)
{
	long n = (long)v;
	char t[24];
	int i = 0, j = 0;
	if (n < 0) {
		buf[j++] = '-';
		n = -n;
	}
	do {
		t[i++] = (char)('0' + n % 10);
		n /= 10;
	} while (n && i < 20);
	while (i) {
		buf[j++] = t[--i];
	}
	buf[j] = 0;
	return buf;
}

static int
same_text(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static const char *
current_plane(id self)
{
	SEL get = sel_registerName("keyplaneName");
	return utf8(((id (*)(id, SEL))objc_msgSend)(self, get));
}

/* Trace only: the requested plane and the one the layout reports afterwards. */
static void
kb_set_plane(id self, SEL cmd, id name)
{
	const char *before = current_plane(self);
	char b[120];
	size_t k = 0;
	for (; before[k] && k < sizeof(b) - 1; k++) {
		b[k] = before[k];
	}
	b[k] = 0;
	orig_set_plane(self, cmd, name);
	if (plane_logs++ < 8) {
		say("kb from ", b);
		say("kb   ask ", utf8(name));
		say("kb   now ", current_plane(self));
	}
}

static id
kb_hit(id self, SEL cmd, pt_t p)
{
	id key = orig_hit(self, cmd, p);
	if (p.y > 100 && hit_logs++ < 8) {
		char bx[24], by[24], line[200];
		const char *name = key ? utf8(((id (*)(id, SEL))objc_msgSend)(key, sel_registerName("name"))) : "nil";
		const char *parts[] = { "x ", num(bx, p.x), " y ", num(by, p.y), " -> ", name };
		size_t o = 0;
		for (unsigned i = 0; i < 6; i++) {
			for (size_t n = 0; parts[i][n] && o < sizeof(line) - 1; n++) {
				line[o++] = parts[i][n];
			}
		}
		line[o] = 0;
		say("kb hit ", line);
	}
	return key;
}


/*
 * Keyboard width (danstronics keyboard_native.inc): a keyplane change makes UIKit set the keyboard
 * frame from the size providers below; their width (an iPhone width) differs from our 360-pt
 * screen, UIKit takes that as a layout change, reloads and goes back to the first keyplane (the
 * 123 key never shows numbers). Report the screen width instead.
 */
typedef struct { double w, h; } size_t2;
#define KB_SIZE_HOOKS 8
static struct { Method m; IMP orig; } kbs[KB_SIZE_HOOKS];
static int kbs_count, kbs_logs;
static const double screen_w = 360.0;

static size_t2
kbs_fix(SEL cmd, size_t2 r)
{
	if (r.w >= 320 && r.w <= 430 && r.w != screen_w && r.h > 100 && r.h < 500) {
		if (kbs_logs++ < 12) {
			char a[24];
			say("kb size ", num(a, r.w));
			say("kb   by ", sel_getName(cmd));
		}
		r.w = screen_w;
	}
	return r;
}

/* one trampoline per hook slot and arity, each calling its own original */
#define KBS_SLOT(n) \
static size_t2 kbs0_##n(id s, SEL c) { return kbs_fix(c, ((size_t2 (*)(id, SEL))kbs[n].orig)(s, c)); } \
static size_t2 kbs1_##n(id s, SEL c, long o) { return kbs_fix(c, ((size_t2 (*)(id, SEL, long))kbs[n].orig)(s, c, o)); } \
static size_t2 kbs2_##n(id s, SEL c, long o, _Bool a) \
{ return kbs_fix(c, ((size_t2 (*)(id, SEL, long, _Bool))kbs[n].orig)(s, c, o, a)); } \
static size_t2 kbs3_##n(id s, SEL c, long o, _Bool a, _Bool b) \
{ return kbs_fix(c, ((size_t2 (*)(id, SEL, long, _Bool, _Bool))kbs[n].orig)(s, c, o, a, b)); }
KBS_SLOT(0) KBS_SLOT(1) KBS_SLOT(2) KBS_SLOT(3) KBS_SLOT(4) KBS_SLOT(5) KBS_SLOT(6) KBS_SLOT(7)
#define KBS_ROW(n) { (IMP)kbs0_##n, (IMP)kbs1_##n, (IMP)kbs2_##n, (IMP)kbs3_##n }
static const IMP kbs_tramp[KB_SIZE_HOOKS][4] = {
	KBS_ROW(0), KBS_ROW(1), KBS_ROW(2), KBS_ROW(3), KBS_ROW(4), KBS_ROW(5), KBS_ROW(6), KBS_ROW(7),
};

/* phase 1: find every method (this runs the classes' +initialize) before any is replaced */
static int kbs_arity[KB_SIZE_HOOKS];

static void
kbs_add(Class c, const char *name, int arity, int class_method)
{
	static const char *const abi[4] = { "{CGSize=dd}16@0:8", "{CGSize=dd}24@0:8q16",
		"{CGSize=dd}28@0:8q16B24", "{CGSize=dd}32@0:8q16B24B28" };
	if (c == NULL || kbs_count >= KB_SIZE_HOOKS) {
		return;
	}
	/*
	 * class_getClassMethod would run the class's +initialize here, inside our constructor, far too
	 * early in the app's start (it recursed until the stack ran out). class_copyMethodList does not
	 * initialize anything; it lists the class's own methods only.
	 */
	SEL sel = sel_registerName(name);
	Class holder = class_method ? object_getClass((id)c) : c;
	unsigned int count = 0;
	Method *list = class_copyMethodList(holder, &count), m = NULL;
	for (unsigned int i = 0; list != NULL && i < count; i++) {
		if (method_getName(list[i]) == sel) {
			m = list[i];
		}
	}
	free(list);
	if (m == NULL || !same_text(method_getTypeEncoding(m), abi[arity])) {
		return;
	}
	for (int i = 0; i < kbs_count; i++) {
		if (kbs[i].m == m) {
			return;                         /* inherited: listed already */
		}
	}
	kbs[kbs_count].m = m;
	kbs[kbs_count].orig = method_getImplementation(m);
	kbs_arity[kbs_count] = arity;
	kbs_count++;
}

static void
kb_size_hooks(void)
{
	const char *owners[2] = { "UIKeyboard", "UIKeyboardImpl" };
	for (int i = 0; i < 2; i++) {
		Class c = objc_getClass(owners[i]);
		kbs_add(c, "defaultSize", 0, 1);
		kbs_add(c, "defaultSizeForInterfaceOrientation:", 1, 1);
		kbs_add(c, "keyboardSizeForInterfaceOrientation:", 1, 1);
		kbs_add(c, "sizeForInterfaceOrientation:", 1, 1);
		kbs_add(c, "sizeForInterfaceOrientation:ignoreInputView:", 2, 1);
		kbs_add(c, "sizeForInterfaceOrientation:includingAssistantBar:ignoreInputView:", 3, 1);
	}
	kbs_add(objc_getClass("UIKeyboardImpl"), "defaultSizeForInterfaceOrientation:", 1, 0);
	/* phase 2: replace them */
	for (int i = 0; i < kbs_count; i++) {
		method_setImplementation(kbs[i].m, kbs_tramp[i][kbs_arity[i]]);
	}
	char n[24];
	say("kb size providers hooked: ", num(n, kbs_count));
}

static void
kb_hooks(void)
{
	Class star = objc_getClass("UIKeyboardLayoutStar");
	if (star == NULL) {
		return;
	}
	Method m = class_getInstanceMethod(star, sel_registerName("setKeyplaneName:"));
	if (m != NULL) {
		orig_set_plane = (void (*)(id, SEL, id))method_getImplementation(m);
		method_setImplementation(m, (IMP)kb_set_plane);
	}
	m = class_getInstanceMethod(star, sel_registerName("keyHitTest:"));
	if (m != NULL) {
		orig_hit = (id (*)(id, SEL, pt_t))method_getImplementation(m);
		method_setImplementation(m, (IMP)kb_hit);
	}
	say("kb trace ", orig_hit != NULL && orig_set_plane != NULL ? "installed" : "partial");
	kb_size_hooks();
}

/*
 * Core Image: there is no GPU. With no Metal device CIContext takes an OpenGL ES context and calls
 * a NULL GL function (Safari crashed on a long press on an image: the QR code detector makes a
 * context). The "software_renderer" option does not help on iOS (it only lowers the priority).
 * Answer nil, as CIContext itself does when neither Metal nor GL is usable.
 */
static id (*orig_ci_init)(id, SEL, id);
static int ci_logs;

static id
ci_init(id self, SEL cmd, id options)
{
	(void)cmd;
	(void)options;
	if (ci_logs++ < 2) {
		say("CIContext -> nil (no GPU)", "");
	}
	((void (*)(id, SEL))objc_msgSend)(self, sel_registerName("release"));
	return NULL;
}

static void
ci_hooks(void)
{
	Class c = objc_getClass("CIContext");
	if (c == NULL) {
		return;
	}
	/* class_copyMethodList: no +initialize from inside the constructor (see kbs_add) */
	SEL sel = sel_registerName("initWithOptions:");
	unsigned int count = 0;
	Method *list = class_copyMethodList(c, &count), m = NULL;
	for (unsigned int i = 0; list != NULL && i < count; i++) {
		if (method_getName(list[i]) == sel) {
			m = list[i];
		}
	}
	free(list);
	if (m != NULL && same_text(method_getTypeEncoding(m), "@24@0:8@16")) {
		orig_ci_init = (id (*)(id, SEL, id))method_getImplementation(m);
		method_setImplementation(m, (IMP)ci_init);
		say("CIContext initWithOptions: hooked", "");
	} else {
		say("CIContext initWithOptions: not hooked, type ", m ? method_getTypeEncoding(m) : "missing");
	}
}

/*
 * Wi-Fi bars probe (SpringBoard only): the status bar icon stays grey while Settings shows bars.
 * SBWiFiManager takes the bars from wifid's SCALED_RSSI and the dBm from RSSI; log both for a while.
 */
extern int pthread_create(void *thread, const void *attr, void *(*start)(void *), void *arg);
extern unsigned int sleep(unsigned int seconds);

static void *
wifi_probe(void *unused)
{
	(void)unused;
	sleep(20);
	for (int i = 0; i < 30; i++) {
		id mgr = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("SBWiFiManager"), sel_registerName("sharedInstance"));
		if (mgr != NULL) {
			char a[24], b[24], c[24], line[100];
			int bars = ((int (*)(id, SEL))objc_msgSend)(mgr, sel_registerName("signalStrengthBars"));
			int rssi = ((int (*)(id, SEL))objc_msgSend)(mgr, sel_registerName("signalStrengthRSSI"));
			_Bool assoc = ((_Bool (*)(id, SEL))objc_msgSend)(mgr, sel_registerName("isAssociated"));
			_Bool primary = ((_Bool (*)(id, SEL))objc_msgSend)(mgr, sel_registerName("isPrimaryInterface"));
			const char *parts[] = { "bars ", num(a, bars), " rssi ", num(b, rssi), " associated+2*primary ", num(c, assoc + 2 * primary) };
			size_t o = 0;
			for (unsigned k = 0; k < 6; k++) {
				for (size_t n = 0; parts[k][n] && o < sizeof(line) - 1; n++) {
					line[o++] = parts[k][n];
				}
			}
			line[o] = 0;
			say("wifi ", line);
		}
		sleep(10);
	}
	return NULL;
}

/*
 * assetsd throws "assetsd requires TCCPhotosService access" when -[PLPrivacy
 * photosAccessAllowedWithScope:] answers nonzero (2026-11-11). Probe: log what it answers.
 */
static long (*orig_pl_allowed)(id, SEL, long);
static int pl_logs;

static long
pl_allowed(id self, SEL cmd, long scope)
{
	long r = orig_pl_allowed(self, cmd, scope);
	if (pl_logs++ < 6) {
		char a[24], b[24];
		say("PLPrivacy photosAccessAllowedWithScope: ", num(a, scope));
		say("PLPrivacy   -> ", num(b, r));
	}
	return r;
}

static void
pl_hooks(void)
{
	Class c = objc_getClass("PLPrivacy");
	if (c == NULL) {
		say("PLPrivacy not loaded", "");
		return;
	}
	SEL sel = sel_registerName("photosAccessAllowedWithScope:");
	unsigned int count = 0;
	Method *list = class_copyMethodList(c, &count), m = NULL;
	for (unsigned int i = 0; list != NULL && i < count; i++) {
		if (method_getName(list[i]) == sel) {
			m = list[i];
		}
	}
	free(list);
	if (m == NULL) {
		say("PLPrivacy: no photosAccessAllowedWithScope:", "");
		return;
	}
	say("PLPrivacy photosAccessAllowedWithScope: type ", method_getTypeEncoding(m));
	orig_pl_allowed = (long (*)(id, SEL, long))method_getImplementation(m);
	method_setImplementation(m, (IMP)pl_allowed);
}

/*
 * Typing for unattended tests (Preferences only): text written to /var/mobile/whyred_type is
 * inserted into the first responder on the main thread (Wi-Fi passwords without tapping keys).
 */
extern long read(int fd, void *buf, unsigned long n);
extern int unlink(const char *path);
extern void dispatch_async_f(void *queue, void *context, void (*work)(void *));
extern char _dispatch_main_q;
static char type_buf[256];

static void
type_now(void *unused)
{
	(void)unused;
	id app = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("UIApplication"), sel_registerName("sharedApplication"));
	id win = app ? ((id (*)(id, SEL))objc_msgSend)(app, sel_registerName("keyWindow")) : NULL;
	id fr = win ? ((id (*)(id, SEL))objc_msgSend)(win, sel_registerName("firstResponder")) : NULL;
	if (fr == NULL) {
		say("type: no first responder", "");
		return;
	}
	id str = ((id (*)(id, SEL, const char *))objc_msgSend)((id)objc_getClass("NSString"),
	    sel_registerName("stringWithUTF8String:"), type_buf);
	((void (*)(id, SEL, id))objc_msgSend)(fr, sel_registerName("insertText:"), str);
	say("type: inserted into ", object_getClassName(fr));
}

static void *
typer(void *unused)
{
	(void)unused;
	for (;;) {
		sleep(1);
		int fd = open("/var/mobile/whyred_type", 0);
		if (fd < 0) {
			continue;
		}
		long n = read(fd, type_buf, sizeof(type_buf) - 1);
		close(fd);
		unlink("/var/mobile/whyred_type");
		while (n > 0 && (type_buf[n - 1] == '\n' || type_buf[n - 1] == '\r')) {
			n--;
		}
		type_buf[n > 0 ? n : 0] = 0;
		if (n > 0) {
			dispatch_async_f(&_dispatch_main_q, NULL, type_now);
		}
	}
	return NULL;
}

/*
 * SpringBoard telephony retry loop. CommCenter is not in launchd.plist (Setup hung on it, image v13), so
 * SpringBoard's CTServerConnection fails and -[SBTelephonyManager _handleTelephonyDaemonRestart] reschedules
 * itself about once a second. Each run goes handleCallControlFailure: -> updateCalls -> updateSpringBoard,
 * which resets the backlight idle timer: the lock screen never darkened by itself (2026-11-23, traced with
 * backtraces from -[SBIdleTimerGlobalCoordinator resetIdleTimer]). The first run goes through, later ones
 * do nothing.
 */
static void (*orig_tel_restart)(id, SEL);
static int tel_restarts;

static void
hk_tel_restart(id s, SEL c)
{
	if (tel_restarts++ == 0) {
		orig_tel_restart(s, c);
		return;
	}
	if (tel_restarts == 2) {
		say("telephony: ", "daemon restart retries ignored (no CommCenter)");
	}
}

static void
telephony_hooks(void)
{
	Class cls = objc_getClass("SBTelephonyManager");
	Method m = cls ? class_getInstanceMethod(cls, sel_registerName("_handleTelephonyDaemonRestart")) : NULL;
	if (m == NULL || !same_text(method_getTypeEncoding(m), "v16@0:8")) {
		say("telephony: ", "no -[SBTelephonyManager _handleTelephonyDaemonRestart] with type v16@0:8");
		return;
	}
	orig_tel_restart = (void (*)(id, SEL))method_setImplementation(m, (IMP)hk_tel_restart);
	say("telephony: ", "hooked _handleTelephonyDaemonRestart");
}

/*
 * Brightness slider. Control Center (SpringBoard) and Settings (Preferences) set "DisplayBrightness"
 * (0.0 .. 1.0) on CoreBrightness's BrightnessSystemClient. CoreBrightness has no display module here
 * (no AppleARMBacklight, sweetbl=0), so the value went nowhere. Here it becomes IOMFBBrightnessLevel on
 * AppleCLCD (nits in 16.16, 0 .. 500; SweetFramebuffer.cpp sends it to the PM660L WLED), nits =
 * 500 * v^2 (at least 2). Reading "DisplayBrightness" answers from that property, so the kernel holds
 * the one value every process sees. As libhakios-brightness.dylib (oosexgang/hakios-xnu-kernel, MIT)
 * does it for its panels. Other keys go to CoreBrightness as before; the first ones are logged.
 */
typedef const void *CFTypeRef;
typedef const struct __CFString *CFStringRef;
typedef const struct __CFNumber *CFNumberRef;
typedef unsigned int io_object_t;
extern CFStringRef CFStringCreateWithCString(const void *alloc, const char *s, unsigned int encoding);
extern CFNumberRef CFNumberCreate(const void *alloc, long type, const void *value);
extern unsigned char CFNumberGetValue(CFNumberRef n, long type, void *value);
extern void CFRelease(CFTypeRef cf);
extern unsigned long CFGetTypeID(CFTypeRef cf);
extern unsigned long CFNumberGetTypeID(void);
extern void *IOServiceMatching(const char *name);
extern io_object_t IOServiceGetMatchingService(unsigned int mainPort, void *matching);
extern int IORegistryEntrySetCFProperty(io_object_t entry, CFStringRef key, CFTypeRef value);
extern CFTypeRef IORegistryEntryCreateCFProperty(io_object_t entry, CFStringRef key, const void *alloc, unsigned int options);
extern int IOObjectRelease(io_object_t o);

#define BL_MAX_NITS 500.0
#define BL_MIN_NITS 2.0
static int (*orig_bl_set)(id, SEL, id, id);
static id (*orig_bl_copy)(id, SEL, id);
static io_object_t bl_fb;
static CFStringRef bl_key;
static int bl_logs;

static const char *
hex(char *buf, unsigned long v)
{
	char t[20];
	int i = 0, j = 0;
	do {
		t[i++] = "0123456789abcdef"[v & 15];
		v >>= 4;
	} while (v && i < 16);
	buf[j++] = '0';
	buf[j++] = 'x';
	while (i) {
		buf[j++] = t[--i];
	}
	buf[j] = 0;
	return buf;
}

static io_object_t
bl_service(void)
{
	if (bl_fb == 0) {
		bl_fb = IOServiceGetMatchingService(0, IOServiceMatching("AppleCLCD"));
		bl_key = CFStringCreateWithCString(NULL, "IOMFBBrightnessLevel", 0x08000100);
	}
	return bl_fb;
}

static void
bl_log(const char *what, id key)
{
	if (bl_logs < 16) {
		bl_logs++;
		say(what, utf8(key));
	}
}

/* slider value 0..1 -> IOMFBBrightnessLevel (nits 16.16) on AppleCLCD */
static void
bl_write(double v)
{
	if (bl_service() == 0) {
		return;
	}
	v = v < 0 ? 0 : v > 1 ? 1 : v;
	double nits = BL_MAX_NITS * v * v;
	long long fixed = (long long)((nits < BL_MIN_NITS ? BL_MIN_NITS : nits) * 65536.0);
	CFNumberRef n = CFNumberCreate(NULL, 4, &fixed);       /* kCFNumberSInt64Type */
	int r = IORegistryEntrySetCFProperty(bl_fb, bl_key, n);
	CFRelease(n);
	if (r != 0) {
		char b[24];
		say("brightness: IOMFBBrightnessLevel refused ", hex(b, (unsigned)r));
	}
}

/* the kernel's level back as a slider value, or -1 */
static double
bl_read(void)
{
	if (bl_service() == 0) {
		return -1;
	}
	CFTypeRef p = IORegistryEntryCreateCFProperty(bl_fb, bl_key, NULL, 0);
	long long fixed = -1;
	if (p != NULL) {
		if (CFGetTypeID(p) == CFNumberGetTypeID()) {
			CFNumberGetValue((CFNumberRef)p, 4, &fixed);
		}
		CFRelease(p);
	}
	if (fixed < 0) {
		return -1;
	}
	double x = fixed / 65536.0 / BL_MAX_NITS, r = x;
	for (int i = 0; i < 20 && r > 0; i++) {
		r = 0.5 * (r + x / r);                 /* sqrt(x) */
	}
	return r > 1 ? 1 : r;
}

static int
bl_set(id self, SEL cmd, id value, id key)
{
	bl_log("brightness set ", key);
	if (value != NULL && same_text(utf8(key), "DisplayBrightness")) {
		bl_write(((float (*)(id, SEL))objc_msgSend)(value, sel_registerName("floatValue")));
	}
	return orig_bl_set(self, cmd, value, key);
}

static id
bl_copy(id self, SEL cmd, id key)
{
	bl_log("brightness copy ", key);
	double v;
	if (same_text(utf8(key), "DisplayBrightness") && (v = bl_read()) >= 0) {
		id num = ((id (*)(Class, SEL, float))objc_msgSend)(objc_getClass("NSNumber"),
		    sel_registerName("numberWithFloat:"), (float)v);
		return ((id (*)(id, SEL))objc_msgSend)(num, sel_registerName("retain"));   /* copy: +1 */
	}
	return orig_bl_copy(self, cmd, key);
}

/*
 * backboardd: the sliders call BKSDisplayBrightnessSet (Settings: -[DBSBrightnessManager
 * setBacklightValue:specifier:]), which lands in -[BKDisplayBrightnessController
 * setBrightnessLevel:reason:options:]. kern.exec_inject must name backboardd (it is not in an .app).
 */
static void (*orig_bk_set)(id, SEL, float, id, unsigned long long);
static float (*orig_bk_level)(id, SEL);
static signed char (*orig_bk_avail)(id, SEL);
static int bk_logs;

static void
bk_set(id self, SEL cmd, float level, id reason, unsigned long long options)
{
	if (bk_logs < 12) {
		char b[24];
		bk_logs++;
		say("brightness level set, reason ", utf8(reason));
		say("  level x1000 ", hex(b, (unsigned long)(level * 1000)));
	}
	orig_bk_set(self, cmd, level, reason, options);
	bl_write(level);
}

static float
bk_level(id self, SEL cmd)
{
	double v = bl_read();
	return v >= 0 ? (float)v : orig_bk_level(self, cmd);
}

static signed char
bk_avail(id self, SEL cmd)
{
	static int said;
	signed char r = orig_bk_avail(self, cmd);
	if (!said++) {
		say("brightness: level control available was ", r ? "YES" : "NO");
	}
	return 1;
}

static void
bk_hook(Class cls, const char *name, IMP imp, void **orig)
{
	Method m = class_getInstanceMethod(cls, sel_registerName(name));
	if (m == NULL) {
		say("brightness: missing ", name);
		return;
	}
	say(name, method_getTypeEncoding(m));
	*orig = (void *)method_setImplementation(m, imp);
}

static void *
bk_hook_thread(void *arg)
{
	(void)arg;
	for (int i = 0; i < 60; i++) {
		Class cls = objc_getClass("BKDisplayBrightnessController");
		if (cls != NULL) {
			bk_hook(cls, "setBrightnessLevel:reason:options:", (IMP)bk_set, (void **)&orig_bk_set);
			bk_hook(cls, "brightnessLevel", (IMP)bk_level, (void **)&orig_bk_level);
			bk_hook(cls, "isBrightnessLevelControlAvailable", (IMP)bk_avail, (void **)&orig_bk_avail);
			return NULL;
		}
		sleep(1);
	}
	say("brightness: ", "no BKDisplayBrightnessController");
	return NULL;
}

static void *
bl_hook_thread(void *arg)
{
	(void)arg;
	for (int i = 0; i < 60; i++) {
		Class cls = objc_getClass("BrightnessSystemClient");
		if (cls != NULL) {
			Method ms = class_getInstanceMethod(cls, sel_registerName("setProperty:forKey:"));
			Method mc = class_getInstanceMethod(cls, sel_registerName("copyPropertyForKey:"));
			if (ms == NULL || mc == NULL || !same_text(method_getTypeEncoding(ms), "B32@0:8@16@24") ||
			    !same_text(method_getTypeEncoding(mc), "@24@0:8@16")) {
				say("brightness: ", "BrightnessSystemClient methods not as expected, left alone");
				return NULL;
			}
			orig_bl_set = (int (*)(id, SEL, id, id))method_setImplementation(ms, (IMP)bl_set);
			orig_bl_copy = (id (*)(id, SEL, id))method_setImplementation(mc, (IMP)bl_copy);
			say("brightness: ", "hooked BrightnessSystemClient");
			return NULL;
		}
		sleep(1);
	}
	return NULL;
}

/*
 * Vibration. iOS 16 renders vibrations in mediaserverd's AVHapticServer to a haptic audio output this
 * phone lacks: "Failed to allocate HapticSynth", the server bails out and nothing plays. So the requests
 * are caught where apps and SpringBoard make them (AudioToolbox's AudioServicesPlaySystemSound calls,
 * interposed below) and drive the PM660 ERM motor through sysctl kern.whyred_vibrate (patch 1000).
 */
extern int sysctlbyname(const char *name, void *oldp, unsigned long *oldlenp, void *newp, unsigned long newlen);
extern int usleep(unsigned int us);
static volatile int vibe_gen;
#define VIBE_MAX_STEPS 64

struct vibe_job {
	int gen, n;
	int on[VIBE_MAX_STEPS], ms[VIBE_MAX_STEPS];
};
static struct vibe_job vibe_jobs[4];
static int vibe_slot;

static void
motor(int ms)
{
	(void)sysctlbyname("kern.whyred_vibrate", NULL, NULL, &ms, sizeof(ms));
}

static void *
vibe_thread(void *arg)
{
	struct vibe_job *j = arg;
	for (int i = 0; i < j->n && j->gen == vibe_gen; i++) {
		int ms = j->ms[i];
		if (j->on[i] && ms > 0) {
			motor(ms);
		}
		for (int t = 0; t < ms && j->gen == vibe_gen; t += 10) {
			usleep(10000);
		}
	}
	if (j->gen == vibe_gen) {
		motor(0);
	}
	return NULL;
}

static id
nsstr(const char *c)
{
	return (id)CFStringCreateWithCString(NULL, c, 0x08000100);
}

/* Play a VibePattern array (pairs: on, milliseconds) on the motor. 0 if it held no pattern. */
static int
vibe_array(id arr)
{
	unsigned long n = arr ? ((unsigned long (*)(id, SEL))objc_msgSend)(arr, sel_registerName("count")) : 0;
	if (n < 2) {
		return 0;
	}
	struct vibe_job *j = &vibe_jobs[vibe_slot++ & 3];
	j->gen = ++vibe_gen;
	j->n = 0;
	for (unsigned long i = 0; i + 1 < n && j->n < VIBE_MAX_STEPS; i += 2) {
		id a = ((id (*)(id, SEL, unsigned long))objc_msgSend)(arr, sel_registerName("objectAtIndex:"), i);
		id b = ((id (*)(id, SEL, unsigned long))objc_msgSend)(arr, sel_registerName("objectAtIndex:"), i + 1);
		j->on[j->n] = ((int (*)(id, SEL))objc_msgSend)(a, sel_registerName("intValue")) != 0;
		j->ms[j->n] = ((int (*)(id, SEL))objc_msgSend)(b, sel_registerName("intValue"));
		j->n++;
	}
	long t;
	pthread_create(&t, NULL, vibe_thread, j);
	return 1;
}

/* A dictionary that holds "VibePattern" (directly or under a vibration-pattern option): play it. */
static int
vibe_dict(id dict)
{
	if (dict == NULL) {
		return 0;
	}
	static const char *const keys[] = { "VibePattern", "VibrationPattern", "AudioServicesPlaySystemSoundOptionVibrationPatternKey" };
	for (unsigned k = 0; k < 3; k++) {
		id key = nsstr(keys[k]);
		id v = ((id (*)(id, SEL, id))objc_msgSend)(dict, sel_registerName("objectForKey:"), key);
		CFRelease(key);
		if (v == NULL) {
			continue;
		}
		if (((signed char (*)(id, SEL, Class))objc_msgSend)(v, sel_registerName("isKindOfClass:"), objc_getClass("NSArray"))) {
			return vibe_array(v);
		}
		if (((signed char (*)(id, SEL, Class))objc_msgSend)(v, sel_registerName("isKindOfClass:"), objc_getClass("NSDictionary"))) {
			return vibe_dict(v);
		}
	}
	return 0;
}

static void
vibe_once(int ms)
{
	vibe_gen++;
	motor(ms);
}

/*
 * AudioToolbox's client calls, in every process with this library (dyld interposing). 4095 =
 * kSystemSoundID_Vibrate.
 */
#define SSID_VIBRATE 4095u
extern void AudioServicesPlaySystemSound(unsigned int ssid);
extern void AudioServicesPlaySystemSoundWithCompletion(unsigned int ssid, id done);
extern void AudioServicesPlaySystemSoundWithOptions(unsigned int ssid, id options, id done);
extern void AudioServicesPlaySystemSoundWithVibration(unsigned int ssid, id unknown, id pattern);
extern void AudioServicesStopSystemSound(unsigned int ssid, long a1, long a2);
static int ss_logs;

static void
ss_log(const char *what, unsigned int ssid, id dict)
{
	if (ss_logs >= 12) {
		return;
	}
	ss_logs++;
	char b[24];
	say(what, hex(b, ssid));
	if (dict != NULL) {
		say("  ", utf8(((id (*)(id, SEL))objc_msgSend)(dict, sel_registerName("description"))));
	}
}

/*
 * System sounds that vibrate on an iPhone, whose vibration mediaserverd adds by sound ID (not in the
 * call). An ERM motor needs about 80 ms to be felt at all, so the short taps are 80 ms.
 */
static int
ss_buzz(unsigned int ssid)
{
	static const struct { unsigned int id; int ms; } table[] = {
		{ SSID_VIBRATE, 400 },  /* kSystemSoundID_Vibrate */
		{ 1011, 400 },          /* SMS received, vibrate */
		{ 1106, 150 },          /* power connected */
		{ 1350, 400 },          /* ringer switched to vibrate */
		{ 1351, 400 },          /* silent, vibrate */
		{ 1519, 80 },           /* peek */
		{ 1520, 80 },           /* pop */
	};
	for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		if (table[i].id == ssid) {
			vibe_once(table[i].ms);
			return 1;
		}
	}
	if (ssid == 1521) {                     /* "nope": three short */
		struct vibe_job *j = &vibe_jobs[vibe_slot++ & 3];
		j->gen = ++vibe_gen;
		j->n = 6;
		for (int i = 0; i < 6; i++) {
			j->on[i] = (i % 2) == 0;
			j->ms[i] = 80;
		}
		long t;
		pthread_create(&t, NULL, vibe_thread, j);
		return 1;
	}
	return 0;
}

static void
my_play(unsigned int ssid)
{
	ss_log("system sound ", ssid, NULL);
	(void)ss_buzz(ssid);
	AudioServicesPlaySystemSound(ssid);
}

static void
my_play_done(unsigned int ssid, id done)
{
	ss_log("system sound (completion) ", ssid, NULL);
	(void)ss_buzz(ssid);
	AudioServicesPlaySystemSoundWithCompletion(ssid, done);
}

static void
my_play_opts(unsigned int ssid, id options, id done)
{
	ss_log("system sound (options) ", ssid, options);
	if (!vibe_dict(options)) {
		(void)ss_buzz(ssid);
	}
	AudioServicesPlaySystemSoundWithOptions(ssid, options, done);
}

static void
my_play_vibe(unsigned int ssid, id unknown, id pattern)
{
	ss_log("system sound (vibration) ", ssid, pattern);
	if (!vibe_dict(pattern)) {
		(void)ss_buzz(ssid);
	}
	AudioServicesPlaySystemSoundWithVibration(ssid, unknown, pattern);
}

static void
my_stop(unsigned int ssid, long a1, long a2)
{
	vibe_once(0);
	AudioServicesStopSystemSound(ssid, a1, a2);
}

#define INTERPOSE(mine, theirs) \
	__attribute__((used)) static const struct { const void *replacement, *replacee; } \
	interpose_##theirs __attribute__((section("__DATA,__interpose"))) = { (const void *)mine, (const void *)theirs }
INTERPOSE(my_play, AudioServicesPlaySystemSound);
INTERPOSE(my_play_done, AudioServicesPlaySystemSoundWithCompletion);
INTERPOSE(my_play_opts, AudioServicesPlaySystemSoundWithOptions);
INTERPOSE(my_play_vibe, AudioServicesPlaySystemSoundWithVibration);
INTERPOSE(my_stop, AudioServicesStopSystemSound);

/*
 * Sounds for unattended tests (SpringBoard only): a system sound id written to /var/mobile/whyred_sound
 * is played by SpringBoard (AudioServicesPlaySystemSound on the main queue), then the file is removed.
 * A command-line process has no audio session, and its requests never reached mediaserverd's system
 * sound server; SpringBoard's do.
 */
static unsigned int sound_id;

static void
sound_now(void *unused)
{
	(void)unused;
	char b[24];
	say("test sound ", hex(b, sound_id));
	AudioServicesPlaySystemSound(sound_id);
}

static void *
sounder(void *unused)
{
	(void)unused;
	for (;;) {
		sleep(1);
		char buf[16];
		int fd = open("/var/mobile/whyred_sound", 0);
		if (fd < 0) {
			continue;
		}
		long n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		unlink("/var/mobile/whyred_sound");
		unsigned int v = 0;
		for (long i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++) {
			v = v * 10 + (unsigned int)(buf[i] - '0');
		}
		if (v != 0) {
			sound_id = v;
			dispatch_async_f(&_dispatch_main_q, NULL, sound_now);
		}
	}
	return NULL;
}

__attribute__((constructor)) static void
whyred_ui_init(void)
{
	if (same_text(getprogname(), "backboardd")) {
		long t;
		pthread_create(&t, NULL, bk_hook_thread, NULL);
		return;
	}
	if (same_text(getprogname(), "assetsd")) {
		pl_hooks();
		return;
	}
	fallback_class = objc_getClass("_UIStatusBarVisualProvider_Fallback");
	legacy_class = objc_getClass("_UIStatusBarVisualProvider_LegacyPhone");
	if (fallback_class == NULL || legacy_class == NULL) {
		return;                         /* not a UIKit process */
	}
	SEL sel = sel_registerName("visualProviderSubclassForScreen:visualProviderInfo:");
	for (int i = 0; i < HOOKS; i++) {
		Class cls = objc_getClass(hook_names[i]);
		Method m = cls ? class_getClassMethod(cls, sel) : NULL;
		if (m == NULL) {
			say("no selector on ", hook_names[i]);
			continue;
		}
		/* a class without its own method inherits one already wrapped: skip it */
		IMP imp = method_getImplementation(m);
		int dup = 0;
		for (int j = 0; j < HOOKS; j++) {
			if (imp == (IMP)wrappers[j]) {
				dup = 1;
			}
		}
		if (dup) {
			continue;
		}
		orig[i] = (provider_fn)imp;
		method_setImplementation(m, (IMP)wrappers[i]);
		say("hooked ", hook_names[i]);
	}
	kb_hooks();
	ci_hooks();
	if (same_text(getprogname(), "SpringBoard")) {
		telephony_hooks();
		long st;
		pthread_create(&st, NULL, sounder, NULL);
	}
	if (same_text(getprogname(), "SpringBoard") || same_text(getprogname(), "Preferences")) {
		long bt;
		pthread_create(&bt, NULL, bl_hook_thread, NULL);
	}
	if (same_text(getprogname(), "Preferences")) {
		long t;
		pthread_create(&t, NULL, typer, NULL);
	}
}
