/*
 * whyred_ui.dylib: UIKit fixes for the 1080x2160 panel, loaded into UIKit processes by the
 * kernel's kern.exec_inject (boot-arg topaz_inject, patch 0919).
 *
 * Status bar: +[_UIStatusBarVisualProvider_iOS visualProviderSubclassForScreen:visualProviderInfo:]
 * picks a layout from the screen's exclusion area and size. 360x720 points matches no iPhone, so
 * UIKit falls back to _UIStatusBarVisualProvider_Fallback (the pink "rdar" bar). When that happens
 * this returns _UIStatusBarVisualProvider_LegacyPhone (the iPhone 8 bar) instead. Any screen UIKit
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
extern const char *getprogname(void);
extern int open(const char *path, int flags, ...);
extern long write(int fd, const void *buf, unsigned long n);
extern int close(int fd);

static Class (*orig_provider)(Class self, SEL cmd, id screen, id info);
static Class fallback_class, legacy_class;
static int swaps;

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

static Class
whyred_provider(Class self, SEL cmd, id screen, id info)
{
	Class c = orig_provider(self, cmd, screen, info);
	if (c == fallback_class && legacy_class != NULL) {
		if (swaps++ == 0) {
			say("status bar Fallback -> LegacyPhone", "");
		}
		return legacy_class;
	}
	return c;
}

__attribute__((constructor)) static void
whyred_ui_init(void)
{
	Class ios = objc_getClass("_UIStatusBarVisualProvider_iOS");
	fallback_class = objc_getClass("_UIStatusBarVisualProvider_Fallback");
	legacy_class = objc_getClass("_UIStatusBarVisualProvider_LegacyPhone");
	if (ios == NULL || fallback_class == NULL || legacy_class == NULL) {
		return;                         /* not a UIKit process */
	}
	Method m = class_getClassMethod(ios, sel_registerName("visualProviderSubclassForScreen:visualProviderInfo:"));
	if (m == NULL) {
		say("status bar selector not found", "");
		return;
	}
	orig_provider = (Class (*)(Class, SEL, id, id))method_getImplementation(m);
	method_setImplementation(m, (IMP)whyred_provider);
	say("status bar hook installed", "");
}
