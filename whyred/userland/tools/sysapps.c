/*
 * sysapps: run only the SystemAppMigrator data migrator, which installs the removable system apps
 * held in /private/var/staged_system_apps (Voice Memos, Calculator, Notes ...). Data migration is
 * off on this phone (LastSystemVersion set in com.apple.migration.plist: the full run took an hour),
 * so these were never installed.
 * Usage: sysapps [disposition]. Signed with the entitlements of com.apple.migrationpluginwrapper.
 */
#include <stddef.h>

typedef struct objc_class *Class;
typedef struct objc_object *id;
typedef struct objc_selector *SEL;
extern Class objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern void objc_msgSend(void);
extern void *dlopen(const char *path, int mode);
extern int printf(const char *fmt, ...);
extern int atoi(const char *s);
extern const void *CFStringCreateWithCString(const void *alloc, const char *s, unsigned int encoding);
extern const char *CFStringGetCStringPtr(const void *s, unsigned int encoding);

#define MSG(ret, ...) ((ret (*)(__VA_ARGS__))objc_msgSend)

static id
str(const char *c)
{
	return (id)CFStringCreateWithCString(NULL, c, 0x08000100);
}

int
main(int argc, char **argv)
{
	unsigned int disp = argc > 1 ? (unsigned int)atoi(argv[1]) : 0;
	if (dlopen("/System/Library/PrivateFrameworks/DataMigration.framework/DataMigration", 1) == NULL) {
		printf("DataMigration did not load\n");
		return 1;
	}
	Class dcm = objc_getClass("DataClassMigrator");
	id m = MSG(id, Class, SEL, id)(dcm, sel_registerName("dataClassMigratorForBundleAtPath:"),
	    str("/System/Library/DataClassMigrators/SystemAppMigrator.migrator"));
	if (m == NULL) {
		printf("no migrator from the bundle\n");
		return 1;
	}
	MSG(void, id, SEL, unsigned int)(m, sel_registerName("setUserDataDisposition:"), disp);
	MSG(void, id, SEL, id)(m, sel_registerName("setContext:"),
	    MSG(id, Class, SEL)(objc_getClass("NSDictionary"), sel_registerName("dictionary")));
	printf("running SystemAppMigrator, disposition %u\n", disp);
	signed char ok = MSG(signed char, id, SEL)(m, sel_registerName("performMigration"));
	printf("performMigration: %s\n", ok ? "YES" : "NO");
	return ok ? 0 : 2;
}
