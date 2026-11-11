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

__attribute__((constructor)) static void
whyred_ui_init(void)
{
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
}
