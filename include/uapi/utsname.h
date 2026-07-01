#ifndef _CUTEOS_UAPI_UTSNAME_H
#define _CUTEOS_UAPI_UTSNAME_H

#define UTS_FIELD_LEN 65

struct utsname {
	char sysname[UTS_FIELD_LEN];
	char nodename[UTS_FIELD_LEN];
	char release[UTS_FIELD_LEN];
	char version[UTS_FIELD_LEN];
	char machine[UTS_FIELD_LEN];
	char domainname[UTS_FIELD_LEN];
};

#endif
