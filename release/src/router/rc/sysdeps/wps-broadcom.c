/*
 * Miscellaneous services
 *
 * Copyright (C) 2009, Broadcom Corporation. All Rights Reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $Id: services.c,v 1.100 2010/03/04 09:39:18 Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <bcmnvram.h>
#include <shutils.h>
#include <dirent.h>
#include <sys/mount.h>
#include <sys/vfs.h>
#include <rc.h>
#include <shared.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <security_ipc.h>
#include <bcmutils.h>
#include <wlutils.h>
#include <wlscan.h>
#ifdef RTCONFIG_WPS
#if defined(RTCONFIG_HND_ROUTER_AX_6756) || defined(RTCONFIG_HND_ROUTER_AX_675X)
#if defined(RTCONFIG_SDK502L07P1_121_37)
#include <wps_ui.h>
#else
#include <wpsdefs.h>
#endif
#else
#include <wps_ui.h>
#endif
#endif

#ifdef RTCONFIG_QTN
#include "web-qtn.h"
#endif

#if defined(RTCONFIG_AMAS) && defined(RTCONFIG_CFGSYNC)
#include <cfg_onboarding.h>
#endif

#ifdef RTCONFIG_BRCM_HOSTAPD
#if defined(RTCONFIG_HND_ROUTER_AX_6756) || defined(RTCONFIG_HND_ROUTER_AX_675X)
#if defined(RTCONFIG_SDK502L07P1_121_37)
#include <wps_ui.h>
#else
#include <wpsdefs.h>
#endif
#else
#include <wps_ui.h>
#endif
#include <wlif_utils.h>
#define HAPD_DIR	"/var/run/hostapd"
#define HAPD_CMD_BUF	256
#define WPA_CLI_APP	"wpa_cli-2.7"
static int wps_config_command;
static void hapd_wps_cleanup();
static inline bool
hapd_disabled()
{
	return nvram_match("hapd_enable", "0") ? TRUE : FALSE;
}
#define	HAPD_DISABLED() hapd_disabled()
#endif	/* RTCONFIG_BRCM_HOSTAPD */

static int
set_wps_env(char *uibuf)
{
	int wps_fd = -1;
	struct sockaddr_in to;
	int sentBytes = 0;
	uint32 uilen = strlen(uibuf);

	//if (is_wps_enabled() == 0)
	//	return -1;

	if ((wps_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		goto exit;
	}

	/* send to WPS */
	to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	to.sin_family = AF_INET;
	to.sin_port = htons(WPS_UI_PORT);

	sentBytes = sendto(wps_fd, uibuf, uilen, 0, (struct sockaddr *) &to,
		sizeof(struct sockaddr_in));

	if (sentBytes != uilen) {
		goto exit;
	}

	/* Sleep 100 ms to make sure
	   WPS have received socket */
	usleep(100*1000);
	close(wps_fd);
	return 0;

exit:
	if (wps_fd >= 0)
		close(wps_fd);

	/* Show error message ?  */
	return -1;
}

#ifdef RTCONFIG_BRCM_HOSTAPD
#define HAPD_ON_PRIMARY_IFACE           0x0     /* run hostapd on primary interface */
#define HAPD_ON_VIRTUAL_IFACE           0x1     /* run hostapd on virtual interface */
extern int hapd_get_config_filename(char *nv_ifname, char *o_fname, int sz, uint32 *o_flgs, int mode);
extern int hapd_create_config_file(char *nv_ifname, char *filename, uint32 flags);
extern void stop_hostapd_per_radio(int radio_idx);
extern int start_hostapd_per_radio(int radio_idx);
#endif

int
start_wps_method_ob(void)
{
	int wps_band;
	char prefix[] = "wlXXXXXX_";
#ifdef RTCONFIG_BRCM_HOSTAPD
	FILE *fp = NULL;
	char tmp[100];
	char filename[128];
	char cmd[64], buf[256];
	int mode;
	int wait_hapd = 20;
	int hapd_is_ready = 0;
	uint32 flags = 0;
#endif

#if defined(RTCONFIG_AMAS) && defined(RTCONFIG_CFGSYNC)
	if (nvram_get_int("cfg_obstatus") == OB_TYPE_LOCKED) {
		wps_band = 0;
	}
	else
#endif
	wps_band = nvram_get_int("wps_band_x");
	snprintf(prefix, sizeof(prefix), "wl%d", wps_band);

#ifdef RTCONFIG_BRCM_HOSTAPD
	if(!HAPD_DISABLED() && !nvram_get_int("wps_enable_x")) {
		_dprintf("Prepare to trigger WPS upon radio [%d]...\n", wps_band);
		nvram_set(strcat_r(prefix, "_wps_mode", tmp), "enabled");
#if defined(RTCONFIG_BCMWL6) && defined(RTCONFIG_PROXYSTA)
		if (is_dpsr(wps_band)
#ifdef RTCONFIG_DPSTA
			|| is_dpsta(wps_band)
#endif
		)
			mode = HAPD_ON_VIRTUAL_IFACE;
		else
			mode = HAPD_ON_PRIMARY_IFACE;
#endif
		if (hapd_get_config_filename(prefix, filename, sizeof(filename), &flags, mode) < 0) {
			_dprintf("Error to get hostapd config filename\n");
			return -1;
		}

		if (hapd_create_config_file(prefix, filename, flags) < 0) {
			_dprintf("Error to get hostapd config filename\n");
			return -1;
		}
		else {
			_dprintf("restart hostapd upon radio [%d]\n", wps_band);
			stop_hostapd_per_radio(wps_band);
			start_hostapd_per_radio(wps_band);

			snprintf(cmd, sizeof(cmd), "hostapd_cli -i %s ping", nvram_safe_get(strcat_r(prefix, "_ifname", tmp)));

			while(wait_hapd) {
				if (wait_hapd != 20)
				    sleep(1);

				if ((--wait_hapd) == 0) {
					_dprintf("restart hostapd upon radio [%d] - FAILED\n", wps_band);
					break;
				}

				fp = popen(cmd, "r");
				if(fp) {
					while (fgets(buf, sizeof(buf), fp) != NULL) {
						if(strstr(buf, "PONG") != NULL)
						{
							hapd_is_ready = 1;
							_dprintf("restart hostapd upon radio [%d] - DONE\n");
							start_wps_pbcd();
							sleep(1);
							break;
						}
					}
					pclose(fp);
				}

				if(!hapd_is_ready)
					continue;

				break;
			}
		}
	}
#endif
	start_wps_method();
}

int
stop_wps_method_ob(void)
{
#ifdef RTCONFIG_BRCM_HOSTAPD
	FILE *fp = NULL;
	char prefix[] = "wlXXXXXX_";
	char word[256], *next;
	char tmp[100], cmd[64], buf[256];
	char filename[128];
	int unit;
	int mode;
	int wait_hapd = 20;
	int hapd_is_ready = 0;
	uint32 flags = 0;
#endif

	stop_wps_method();
#ifdef RTCONFIG_BRCM_HOSTAPD
	if(!HAPD_DISABLED() && !nvram_get_int("wps_enable_x")) {
		stop_wps_pbcd();
		foreach (word, nvram_safe_get("wl_ifnames"), next) {
			if (wl_ioctl(word, WLC_GET_INSTANCE, &unit, sizeof(unit)))
				continue;

			snprintf(prefix, sizeof(prefix), "wl%d", unit);
			if(nvram_match(strcat_r(prefix, "_wps_mode", tmp), "enabled")) {
				nvram_set(strcat_r(prefix, "_wps_mode", tmp), "disabled");
#if defined(RTCONFIG_BCMWL6) && defined(RTCONFIG_PROXYSTA)
				if (is_dpsr(unit)
#ifdef RTCONFIG_DPSTA
					|| is_dpsta(unit)
#endif
				)
					mode = HAPD_ON_VIRTUAL_IFACE;
				else
					mode = HAPD_ON_PRIMARY_IFACE;
#endif
				if (hapd_get_config_filename(prefix, filename, sizeof(filename), &flags, mode) < 0) {
					_dprintf("Error to get hostapd config filename\n");
					return -1;
				}

				if (hapd_create_config_file(prefix, filename, flags) < 0) {
					_dprintf("Error to get hostapd config filename\n");
					return -1;
				}
				else {
					_dprintf("restart hostapd upon radio [%d]\n", unit);
					stop_hostapd_per_radio(unit);
					start_hostapd_per_radio(unit);

					snprintf(cmd, sizeof(cmd), "hostapd_cli -i %s ping", nvram_safe_get(strcat_r(prefix, "_ifname", tmp)));
					while(wait_hapd) {
						if (wait_hapd != 20)
							sleep(1);

						if ((--wait_hapd) == 0) {
							_dprintf("restart hostapd upon radio [%d] - FAILED\n", unit);
							break;
						}

						fp = popen(cmd, "r");
						if(fp) {
							while (fgets(buf, sizeof(buf), fp) != NULL) {
								if(strstr(buf, "PONG") != NULL)
								{
									hapd_is_ready = 1;
									_dprintf("restart hostapd upon radio [%d] - DONE\n", unit);
									break;
								}
							}
							pclose(fp);
						}

						if(!hapd_is_ready)
							continue;

						break;
					}
				}
			}
		}
	}
#endif
}
/*
 * input variables:
 * 	nvram: wps_band_x:
 * 	nvram: wps_action: WPS_UI_ACT_ADDENROLLEE/WPS_UI_ACT_NONE
 *     (nvram: wps_method: according to wps_sta_pin)
 *	nvram: wps_sta_pin:
 *	nvram: wps_version2:
 *	nvram: wps_autho_sta_mac:
 *
 * output variables:
 * 	wps_proc_status
 */

int
start_wps_method(void)
{
	int wps_band;
	char prefix[]="wlXXXXXX_", tmp[100];
	char *wps_sta_pin;
	char buf[256] = "SET ";
	int len = 4;
	char ifname[NVRAM_MAX_PARAM_LEN];
#ifdef RTCONFIG_BRCM_HOSTAPD
	char word[256], *next;
#endif

	if (getpid()!=1) {
		notify_rc("start_wps_method");
		return 0;
	}

	wps_band = nvram_get_int("wps_band_x");
#if defined(RTCONFIG_AMAS) && defined(RTCONFIG_CFGSYNC)
	if (nvram_get_int("cfg_obstatus") == OB_TYPE_LOCKED) {
		wps_band = 0;
		dbg("wps_band(%d) for wps registrar\n", wps_band);
	}
#endif
#if defined(RTCONFIG_BCMWL6) && defined(RTCONFIG_PROXYSTA)
	if (is_dpsr(wps_band)
#ifdef RTCONFIG_DPSTA
		|| is_dpsta(wps_band)
#endif
	)
		snprintf(prefix, sizeof(prefix), "wl%d.1_", wps_band);
	else
#endif
		snprintf(prefix, sizeof(prefix), "wl%d_", wps_band);
#ifdef RTCONFIG_VIF_ONBOARDING
	if (nvram_get_int("wps_via_vif")) {
		snprintf(prefix, sizeof(prefix), "wl%d.%d_", wps_band,
			(!nvram_get_int("re_mode")) ? nvram_get_int("obvif_cap_subunit"): nvram_get_int("obvif_re_subunit"));
		nvram_unset("wps_via_vif");
	}
#endif
	strlcpy(ifname, nvram_safe_get(strcat_r(prefix, "ifname", tmp)), sizeof(ifname));
	wps_sta_pin = nvram_safe_get("wps_sta_pin");

#ifdef RTCONFIG_QTN
	int retval;

#ifdef RTCONFIG_WPS_DUALBAND
	if (1)
#else
	if (wps_band)
#endif
	{
		if (strlen(wps_sta_pin) && strcmp(wps_sta_pin, "00000000") && (wl_wpsPincheck(wps_sta_pin) == 0))
		{
			retval = rpc_qcsapi_wps_registrar_report_pin(WIFINAME, wps_sta_pin);
			if (retval < 0)
				dbG("rpc_qcsapi_wps_registrar_report_pin %s error, return: %d\n", WIFINAME, retval);
		}
		else
		{
			retval = rpc_qcsapi_wps_registrar_report_button_press(WIFINAME);
			if (retval < 0)
				dbG("rpc_qcsapi_wps_registrar_report_button_press %s error, return: %d\n", WIFINAME, retval);
		}

#ifdef RTCONFIG_WPS_DUALBAND
		// return 0;
#else
		return 0;
#endif
	}
#endif

#ifdef RTCONFIG_BRCM_HOSTAPD
	if (!HAPD_DISABLED()) {
		char cmd[HAPD_CMD_BUF] = {0};
		char sta_mac[32] = {0};

		if (is_router_mode() && !nvram_get_int("w_Setting") && !strcmp(wps_sta_pin, "00000000")) {
			// Register for wps success notification so that once wps succeeds
			// in any single interface it can be stopped on other interfaces
			kill_pidfile_s("/var/run/wps_pbcd.pid", SIGUSR1);

			foreach (word, nvram_safe_get("wl_ifnames"), next) {
				snprintf(cmd, sizeof(cmd), "hostapd_cli -p"
					" %s -i %s wps_pbc", HAPD_DIR, word);

				if (cmd[0] != '\0') {
					if (system(cmd) == 0) {
						wps_config_command = WPS_UI_CMD_START;
						wl_wlif_update_wps_ui(WLIF_WPS_UI_FINDING_PBC_STA);
					} else {
						dbg("Err : %s failed to execute %s \n",
							__FUNCTION__, cmd);
					}
				}
			}
		} else {
			if (strlen(wps_sta_pin) && strcmp(wps_sta_pin, "00000000") && (wl_wpsPincheck(wps_sta_pin) == 0)) {
				char *ptr = sta_mac[0] != '\0' ? sta_mac : "";
				snprintf(cmd, sizeof(cmd),
					"hostapd_cli -p %s -i %s wps_pin any %s %s",
					HAPD_DIR, ifname, wps_sta_pin, ptr);
			} else {
				snprintf(cmd, sizeof(cmd), "hostapd_cli -p"
					" %s -i %s wps_pbc", HAPD_DIR, ifname);
			}
			if (cmd[0] != '\0') {
				if (system(cmd) == 0) {
					wps_config_command = WPS_UI_CMD_START;
					wl_wlif_update_wps_ui(WLIF_WPS_UI_FINDING_PBC_STA);
				} else {
					dbg("Err : %s failed to execute %s \n",
						__FUNCTION__, cmd);
				}
			}
		}
	} else
#endif	/* RTCONFIG_BRCM_HOSTAPD */
	{
		if (strlen(wps_sta_pin) && strcmp(wps_sta_pin, "00000000") && (wl_wpsPincheck(wps_sta_pin) == 0))
			len += sprintf(buf + len, "wps_method=\"%d\" ", WPS_UI_METHOD_PIN);
		else
			len += sprintf(buf + len, "wps_method=\"%d\" ", WPS_UI_METHOD_PBC);

		if (nvram_match("wps_version2", "enabled") && strlen(nvram_safe_get("wps_autho_sta_mac")))
			len += sprintf(buf + len, "wps_autho_sta_mac=\"%s\" ", nvram_safe_get("wps_autho_sta_mac"));

		if (strlen(wps_sta_pin))
			len += sprintf(buf + len, "wps_sta_pin=\"%s\" ", wps_sta_pin);
		else
			len += sprintf(buf + len, "wps_sta_pin=\"00000000\" ");

		len += sprintf(buf + len, "wps_action=\"%d\" ", WPS_UI_ACT_ADDENROLLEE);

		len += sprintf(buf + len, "wps_config_command=\"%d\" ", WPS_UI_CMD_START);

		nvram_set("wps_proc_status", "0");
		nvram_set("wps_proc_status_x", "0");

		len += sprintf(buf + len, "wps_pbc_method=\"%d\" ", WPS_UI_PBC_SW);
		len += sprintf(buf + len, "wps_ifname=\"%s\" ", ifname);

		dbG("wps env buffer: %s\n", buf);

#if 0
		nvram_unset("wps_sta_devname");
		nvram_unset("wps_sta_mac");
		nvram_unset("wps_pinfail");
		nvram_unset("wps_pinfail_mac");
		nvram_unset("wps_pinfail_name");
		nvram_unset("wps_pinfail_state");
#endif
		nvram_unset("wps_band");

		nvram_set("wps_env_buf", buf);
		nvram_set_int("wps_restart_war", 1);
		set_wps_env(buf);
	}

	sprintf(tmp, "%lu", uptime());
	nvram_set("wps_uptime", tmp);

	return 0;
}

int
stop_wps_method(void)
{
	char buf[256] = "SET ";
	int len = 4;
#ifdef RTCONFIG_BRCM_HOSTAPD
	char word[256], *next;
#endif

	if (getpid()!=1) {
		notify_rc("stop_wps_method");
		return 0;
	}

#ifdef RTCONFIG_QTN
	int retval = rpc_qcsapi_wps_cancel(WIFINAME);
	if (retval < 0)
		dbG("rpc_qcsapi_wps_cancel %s error, return: %d\n", WIFINAME, retval);
#endif

#ifdef RTCONFIG_BRCM_HOSTAPD
	if (!HAPD_DISABLED()) {
		char prefix[]="wlXXXXXX_", tmp[128];
		char *mode;
		char cmd[HAPD_CMD_BUF] = {0};
		char ifname[NVRAM_MAX_PARAM_LEN];
		int unit;

		if (is_router_mode()) {
			foreach (word, nvram_safe_get("wl_ifnames"), next) {
				if (wl_ioctl(word, WLC_GET_INSTANCE, &unit, sizeof(unit)))
					continue;

				snprintf(prefix, sizeof(prefix), "wl%d", unit);
	                        mode = nvram_safe_get(strcat_r(prefix, "_mode", tmp));

				if (!strcmp(mode, "ap")) {
					/* Execute wps_cancel cli cmd and reset the wps state variables to 0 */
					snprintf(cmd, sizeof(cmd), "hostapd_cli -p"
						" %s -i %s wps_cancel", HAPD_DIR, word);
				} else {
					snprintf(cmd, sizeof(cmd), "%s -p "
						"/var/run/%s_wpa_supplicant -i %s wps_cancel",
						WPA_CLI_APP, prefix, word);
				}

				if (system(cmd) == 0) {
					wps_config_command = WPS_UI_CMD_NONE;
					wl_wlif_update_wps_ui(WLIF_WPS_UI_INIT);
				}
			}
		} else {
			snprintf(prefix, sizeof(prefix), "wl%d", nvram_get_int("wps_band_x"));
			mode = nvram_safe_get(strcat_r(prefix, "_mode", tmp));
			wl_ifname(nvram_get_int("wps_band_x"), 0, ifname);

			if (!strcmp(mode, "ap")) {
				/* Execute wps_cancel cli cmd and reset the wps state variables to 0 */
				snprintf(cmd, sizeof(cmd), "hostapd_cli -p"
					" %s -i %s wps_cancel", HAPD_DIR, ifname);
			} else {
				snprintf(cmd, sizeof(cmd), "%s -p "
					"/var/run/%s_wpa_supplicant -i %s wps_cancel",
					WPA_CLI_APP, prefix, ifname);
			}

			if (system(cmd) == 0) {
				wps_config_command = WPS_UI_CMD_NONE;
				wl_wlif_update_wps_ui(WLIF_WPS_UI_INIT);
			}
		}
	} else
#endif
	{
		len += sprintf(buf + len, "wps_config_command=%d ", WPS_UI_CMD_STOP);
		len += sprintf(buf + len, "wps_action=%d ", WPS_UI_ACT_NONE);

		set_wps_env(buf);

		nvram_set_int("wps_uptime", 0);
	}

	return 0;
}

/*
 * input variables:
 *	nvram: wps_band:
 *	nvram: wps_action: WPS_UI_ACT_ENROLL/WPS_UI_ACT_STA_CONFIGAP/WPS_UI_ACT_STA_GETAPCONFIG
 *	nvram: wps_method: WPS_UI_METHOD_PIN/WPS_UI_METHOD_PBC
 *	nvram: wps_ap_pin:
 *
 * output variables:
 *	wps_action
 *	wps_proc_status
 *
 * Currently only supports action WPS_UI_ACT_ENROLL and method WPS_UI_METHOD_PBC
 */

#ifdef RTCONFIG_AMAS
int start_wps_enr(void)
{
	char prefix[]="wlXXXXXX_", tmp[100], buf[256] = "SET ";
	int wps_action = WPS_UI_ACT_ENROLL;
	int wps_method = WPS_UI_METHOD_PBC;
	int wps_config_command = WPS_UI_CMD_START;
	int len = 4;
	char ifname[NVRAM_MAX_PARAM_LEN];

	snprintf(prefix, sizeof(prefix), "wl%d", nvram_get_int("wps_band_x"));
	strlcpy(ifname, nvram_safe_get(strcat_r(prefix, "_ifname", tmp)), sizeof(ifname));

#ifdef RTCONFIG_BRCM_HOSTAPD
	if (!HAPD_DISABLED()) {
		int wps_method = WPS_UI_METHOD_PBC;
		char cmd[HAPD_CMD_BUF] = {0};

		switch (wps_method){
			case WPS_UI_METHOD_PBC:
			snprintf(cmd, sizeof(cmd), "%s -p "
				"/var/run/%s_wpa_supplicant -i %s wps_pbc",
				WPA_CLI_APP, prefix, ifname);
			break;

			case WPS_UI_METHOD_PIN:
			snprintf(cmd, sizeof(cmd), "%s -p "
				"/var/run/%s_wpa_supplicant -i %s wps_pin any %s",
				WPA_CLI_APP, prefix, ifname, nvram_safe_get("wps_device_pin"));
			break;
		}

		if (system(cmd) == 0) {
			wps_config_command = WPS_UI_CMD_START;
			wl_wlif_update_wps_ui(WLIF_WPS_UI_FIND_PBC_AP);
		} else {
			dbg("Err : %s failed to execute %s \n",
				__FUNCTION__, cmd);
		}
	} else
#endif	/* RTCONFIG_BRCM_HOSTAPD */
	{
		sprintf(tmp, "%d", wps_action);
		nvram_set("wps_action", tmp);

		len += sprintf(buf + len, "wps_action=\"%d\" ", wps_action);
		len += sprintf(buf + len, "wps_pbc_method=\"%d\" ", WPS_UI_PBC_SW);
		len += sprintf(buf + len, "wps_method=\"%d\" ", wps_method);

		nvram_set("wps_proc_status", "0");

		len += sprintf(buf + len, "wps_config_command=\"%d\" ", wps_config_command);
		len += sprintf(buf + len, "wps_ifname=\"%s\" ", ifname);

		nvram_set("wps_env_buf", buf);
		set_wps_env(buf);
	}

	sprintf(tmp, "%lu", uptime());
	nvram_set("wps_uptime", tmp);

	return 0;
}
#endif

int is_wps_stopped(void)
{
	int ret = 1;
#if defined(RTCONFIG_QTN) && defined(RTCONFIG_WPS_DUALBAND)
	int ret_qtn = 1;
#endif
	int status = nvram_get_int("wps_proc_status");
	time_t now = uptime();
	time_t wps_uptime = strtoul(nvram_safe_get("wps_uptime"), NULL, 10);

#ifdef RTCONFIG_AMAS
	if ((is_router_mode()
#if defined(RTCONFIG_DPSTA) && defined(RTAC68U)
		|| (is_dpsta_repeater() && dpsta_mode() && nvram_get_int("re_mode") == 0)
#elif defined(RPAX56) || defined(RPAX58)
		|| ((dpsta_mode()||rp_mode()||dpsr_mode) && nvram_get_int("re_mode") == 0)
#endif
		) && !nvram_get_int("obd_Setting") && nvram_match("amesh_led", "1"))
		return 0;
#endif

	nvram_set_int("wps_proc_status_x", status);

	if (!status && (!wps_uptime || ((now - wps_uptime) < 2))) {
		dbg("Wait WPS to start...\n");
		return 0;
	}

#ifdef RTCONFIG_QTN
	char wps_state[32], state_str[32];
	int retval, state = -1 ;

#ifdef RTCONFIG_WPS_DUALBAND
	if (nvram_get_int("wps_enable"))
#else
	if (nvram_get_int("wps_enable") && nvram_get_int("wps_band_x"))
#endif
	{
		retval = rpc_qcsapi_wps_get_state(WIFINAME, wps_state, sizeof(wps_state));
		if (retval < 0)
			dbG("rpc_qcsapi_wps_get_state %s error, return: %d\n", WIFINAME, retval);
		else
		{
			if (sscanf(wps_state, "%d %s", &state, state_str) != 2)
				dbG("prase wps state error!\n");

			switch (state) {
				case 0: /* WPS_INITIAL */
					dbg("QTN: WPS Init\n");
					break;
				case 1: /* WPS_START */
					dbg("QTN: Processing WPS start...\n");
					ret = 0;
#ifdef RTCONFIG_WPS_DUALBAND
					ret_qtn = 0;
#endif
					break;
				case 2: /* WPS_SUCCESS */
					dbg("QTN: WPS Success\n");
					break;
				case 3: /* WPS_ERROR */
					dbg("QTN: WPS Fail due to message exchange error!\n");
					break;
				case 4: /* WPS_TIMEOUT */
					dbg("QTN: WPS Fail due to time out!\n");
					break;
				case 5: /* WPS_OVERLAP */
					dbg("QTN: WPS Fail due to PBC session overlap!\n");
					break;
				default:
					ret = 0;
#ifdef RTCONFIG_WPS_DUALBAND
					ret_qtn = 0;
#endif
					break;
			}
		}

#ifndef RTCONFIG_WPS_DUALBAND
		return ret;
#endif
	}
#endif

	switch (status) {
		case 0: /* Init */
			dbg("Idle\n");
#if 0
			if (nvram_get_int("wps_restart_war") && (now - wps_uptime) < 3)
			{
				char tmp[100];
				dbg("Re-send WPS env!!!\n");
				set_wps_env(nvram_safe_get("wps_env_buf"));
				nvram_unset("wps_env_buf");
				nvram_set_int("wps_restart_war", 0);
				sprintf(tmp, "%lu", uptime());
				nvram_set("wps_uptime", tmp);
				return 0;
			}
#endif
			break;
		case 1: /* WPS_ASSOCIATED */
			dbg("Processing WPS start...\n");
			ret = 0;
			break;
		case 2: /* WPS_OK */
		case 7: /* WPS_MSGDONE */
			dbg("WPS Success\n");
			break;
		case 3: /* WPS_MSG_ERR */
			dbg("WPS Fail due to message exchange error!\n");
			break;
		case 4: /* WPS_TIMEOUT */
			dbg("WPS Fail due to time out!\n");
			break;
		case 5: /* WPS_UI_SENDM2 */
			ret = 0;
			dbg("Send M2\n");
			break;
		case 6: /* WPS_UI_SENDM7 */
			ret = 0;
			dbg("Send M7\n");
			break;
		case 8: /* WPS_OVERLAP */
			dbg("WPS Fail due to PBC session overlap!\n");
			break;
		case 9: /* WPS_UI_FIND_PBC_AP */
			ret = 0;
			dbg("Finding a PBC access point...\n");
			break;
		case 10: /* WPS_UI_ASSOCIATING */
			ret = 0;
			dbg("Associating with access point...\n");
			break;
		default:
			ret = 0;
			break;
	}

#if defined(RTCONFIG_QTN) && defined(RTCONFIG_WPS_DUALBAND)
	if (ret == 1 || ret_qtn == 1) {
		nvram_set("wps_proc_status", "0");
		return 1;
	} else {
		return 0;
	}
#else
	return ret;
#endif
	// TODO: handle enrollee
}

int is_wps_success(void)
{
	int wps_proc_status = nvram_get_int("wps_proc_status_x");
	return (wps_proc_status == 2 || wps_proc_status == 7);
}
