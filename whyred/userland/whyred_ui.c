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
extern const char *class_getName(Class cls);
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
}
