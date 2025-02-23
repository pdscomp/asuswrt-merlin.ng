#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <bcmnvram.h>
#include <shutils.h>
#include <shared.h>
#include <wlutils.h>
#include <syslog.h>
#include "iboxcom.h"

#include <endian.h>
#if __BYTE_ORDER == __BIG_ENDIAN
#include <linux/byteorder/big_endian.h>
#elif __BYTE_ORDER == __LITTLE_ENDIAN
#include <linux/byteorder/little_endian.h>
#else
#error Unknown endian
#endif
extern unsigned char label_mac[];
#if defined(RTCONFIG_AMAS)
extern unsigned char cfg_group_g[];
extern char productid_g[];
extern char mac[];
#endif

int getStorageStatus(STORAGE_INFO_T *st)
{
	memset(st, 0, sizeof(*st) );

	st->AppHttpPort = __cpu_to_le16(nvram_get_int("dm_http_port"));

	/*
	if(!is_router_mode()) {
		return 0;
	}
	*/

	st->MagicWord = __cpu_to_le16(EXTEND_MAGIC);
	st->AppAPILevel = EXTEND_API_LEVEL;
	st->ExtendCap = 0;

#ifdef RTCONFIG_WEBDAV
	st->ExtendCap |= __cpu_to_le16(EXTEND_CAP_WEBDAV);
#else
	st->ExtendCap = 0;
	if(check_if_file_exist("/opt/etc/init.d/S50aicloud")) 
		st->ExtendCap |= __cpu_to_le16(EXTEND_CAP_WEBDAV);
#endif


#ifdef RTCONFIG_TUNNEL
	st->ExtendCap |= __cpu_to_le16(EXTEND_CAP_AAE_BASIC);
#endif

	st->ExtendCap |= __cpu_to_le16(EXTEND_CAP_SWCTRL);

#ifdef RTCONFIG_AMAS
#ifdef RTCONFIG_SW_HW_AUTH
	if (getAmasSupportMode() != 0)
	{
#endif
		if (!repeater_mode()
#if defined(RTCONFIG_BCMWL6) && defined(RTCONFIG_PROXYSTA)
			&& !psr_mode()
#endif
#ifdef RTCONFIG_DPSTA
			&& !(dpsta_mode() && nvram_get_int("re_mode") == 0)
#endif
		)
			st->ExtendCap |= __cpu_to_le16(EXTEND_CAP_AMAS);
#ifdef RTCONFIG_SW_HW_AUTH
	}
#endif
	if (nvram_get_int("amas_bdl"))
		st->ExtendCap |= __cpu_to_le16(EXTEND_CAP_AMAS_BDL);
#endif
#if defined(RTCONFIG_CFGSYNC) && defined(RTCONFIG_MASTER_DET)
	if (nvram_get_int("cfg_master"))
		st->ExtendCap |= __cpu_to_le16(EXTEND_CAP_MASTER);
#endif
	if(nvram_get_int("enable_webdav")) 	
		st->u.wt.EnableWebDav = 1;
	else
		st->u.wt.EnableWebDav = 0;

	st->u.wt.HttpType = nvram_get_int("st_webdav_mode");
	st->u.wt.HttpPort = htons(nvram_get_int("webdav_http_port"));
	st->u.wt.HttpsPort = htons(nvram_get_int("webdav_https_port"));

	if(nvram_get_int("ddns_enable_x")) {
		st->u.wt.EnableDDNS = 1;
		snprintf(st->u.wt.HostName, sizeof(st->u.wt.HostName), nvram_safe_get("ddns_hostname_x"));
	}
	else {
		st->u.wt.EnableDDNS = 0;
	}

	st->u.dev.sw.AiHOMEAPILevel = EXTEND_AIHOME_API_LEVEL;

	// setup st->u.WANIPAddr
	st->u.wt.WANIPAddr = __cpu_to_le32(inet_network(get_wanip()));

	st->u.wt.isNotDefault = nvram_get_int("x_Setting");


#ifdef RTCONFIG_TUNNEL
	st->EnableAAE = (BYTE)nvram_get_int("aae_enable");
	printf("AAE EnableAAE =%d <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n", st->EnableAAE);
	const char* tnl_devid = nvram_get("aae_deviceid");
	if(tnl_devid) {
		strcpy(st->AAEDeviceID, tnl_devid);
		printf("AAE DeviceID =%s <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n", st->AAEDeviceID);
	}
#endif
	memcpy(st->Label_MacAddress, label_mac, 6);

	return 0;
}
#if defined(RTCONFIG_AMAS)
int storage_setbuf(int type, unsigned char *dst, unsigned char *data)
{
	int len = 0;
	unsigned char c;

	switch (type) {
		case INFO_TYPE_GROUPID: /* group id */
			len = 20;
			break;
		case INFO_TYPE_PRODUCT_NAME: /* product id */
			len = strlen(get_productid());
			break;
		case INFO_TYPE_MAC: /* mac */
			len = 6;
			break;
	}

	if (len) {
		c = type;
		memcpy(dst, &c, 1);
		c = len;
		memcpy(dst+1, &c, 1);
		memcpy(dst+2, data, len);
	}

	return len + 2;
}
int getStorageStatusFindCap(STORAGE_INFO_FINDCAP_T *st)
{
	unsigned char value[256];
	unsigned char *p = NULL;

	memset(st, 0, sizeof(*st));

	st->MagicWord = __cpu_to_le16(EXTEND_MAGIC);
	st->ExtendCap = 0;

#ifdef RTCONFIG_WEBDAV
	st->ExtendCap |= __cpu_to_le16(EXTEND_CAP_WEBDAV);
#else
	st->ExtendCap = 0;
	if(check_if_file_exist("/opt/etc/init.d/S50aicloud"))
		st->ExtendCap |= __cpu_to_le16(EXTEND_CAP_WEBDAV);
#endif


#ifdef RTCONFIG_TUNNEL
	st->ExtendCap |= __cpu_to_le16(EXTEND_CAP_AAE_BASIC);
#endif

	st->ExtendCap |= __cpu_to_le16(EXTEND_CAP_SWCTRL);

#ifdef RTCONFIG_AMAS
#ifdef RTCONFIG_SW_HW_AUTH
	if (getAmasSupportMode() != 0)
	{
#endif
		if (!repeater_mode()
#if defined(RTCONFIG_BCMWL6) && defined(RTCONFIG_PROXYSTA)
			&& !psr_mode()
#endif
#ifdef RTCONFIG_DPSTA
			&& !(dpsta_mode() && nvram_get_int("re_mode") == 0)
#endif
		)
			st->ExtendCap |= __cpu_to_le16(EXTEND_CAP_AMAS);
#ifdef RTCONFIG_SW_HW_AUTH
	}
#endif
	if (nvram_get_int("amas_bdl"))
		st->ExtendCap |= __cpu_to_le16(EXTEND_CAP_AMAS_BDL);
#endif
#if defined(RTCONFIG_CFGSYNC) && defined(RTCONFIG_MASTER_DET)
	if (nvram_get_int("cfg_master"))
	{
		st->ExtendCap |= __cpu_to_le16(EXTEND_CAP_MASTER);
	}
#endif

	char vsie_id_str[41] = {0};
	hex2str(cfg_group_g, &vsie_id_str[0], sizeof(vsie_id_str)/2);
	printf("CfgGroup IDstr : %s\n", vsie_id_str);

	memset(value, 0, sizeof(value));
	p = value;
	p += storage_setbuf(INFO_TYPE_GROUPID, p, (unsigned char*)cfg_group_g);
	p += storage_setbuf(INFO_TYPE_PRODUCT_NAME, p, (unsigned char*)productid_g);
	p += storage_setbuf(INFO_TYPE_MAC, p, (unsigned char*)mac);
	memcpy(st->Info, value, p-value);

	return 0;
}
#endif

