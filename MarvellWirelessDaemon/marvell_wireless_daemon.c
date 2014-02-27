/*
* All Rights Reserved
*
* MARVELL CONFIDENTIAL
* Copyright 2008 ~ 2010 Marvell International Ltd All Rights Reserved.
* The source code contained or described herein and all documents related to
* the source code ("Material") are owned by Marvell International Ltd or its
* suppliers or licensors. Title to the Material remains with Marvell International Ltd
* or its suppliers and licensors. The Material contains trade secrets and
* proprietary and confidential information of Marvell or its suppliers and
* licensors. The Material is protected by worldwide copyright and trade secret
* laws and treaty provisions. No part of the Material may be used, copied,
* reproduced, modified, published, uploaded, posted, transmitted, distributed,
* or disclosed in any way without Marvell's prior express written permission.
*
* No license under any patent, copyright, trade secret or other intellectual
* property right is granted to or conferred upon you by disclosure or delivery
* of the Materials, either expressly, by implication, inducement, estoppel or
* otherwise. Any license under such intellectual property rights must be
* express and approved by Marvell in writing.
* 
*/
#define LOG_TAG "marvellWirelessDaemon"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <utils/Log.h>

#include <cutils/log.h>
#include <sys/types.h>  
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <stddef.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cutils/properties.h>

#include <sys/prctl.h>
#include <linux/capability.h>
#include <private/android_filesystem_config.h>

#include "marvell_wireless_daemon.h"


#ifndef WIFI_SDIO_IF_DRIVER_MODULE_NAME
#define WIFI_SDIO_IF_DRIVER_MODULE_NAME "mlan"
#endif
#ifndef WIFI_SDIO_IF_DRIVER_MODULE_PATH
#define WIFI_SDIO_IF_DRIVER_MODULE_PATH "/system/lib/modules/mlan.ko"
#endif
#ifndef WIFI_SDIO_IF_DRIVER_MODULE_ARG
#define WIFI_SDIO_IF_DRIVER_MODULE_ARG ""
#endif


#ifndef WIFI_DRIVER_MODULE_NAME
#define WIFI_DRIVER_MODULE_NAME "sd8xxx"
#endif
#ifndef WIFI_DRIVER_MODULE_PATH
#define WIFI_DRIVER_MODULE_PATH "/system/lib/modules/sd8xxx.ko"
#endif
#ifndef WIFI_DRIVER_MODULE_ARG
#define WIFI_DRIVER_MODULE_ARG "drv_mode=5 cfg80211_wext=0xc sta_name=wlan uap_name=wlan wfd_name=p2p max_uap_bss=1 fw_name=mrvl/sd8787_uapsta.bin"
#endif


#ifndef BLUETOOTH_DRIVER_MODULE_NAME
#define BLUETOOTH_DRIVER_MODULE_NAME "mbt8xxx"
#endif
#ifndef BLUETOOTH_DRIVER_MODULE_PATH
#define BLUETOOTH_DRIVER_MODULE_PATH "/system/lib/modules/mbt8xxx.ko"
#endif
//add option to debug driver mbt_drvdbg=0x3000f mbt_drvdbg=0x1000003f
#ifndef BLUETOOTH_DRIVER_MODULE_ARG
#define BLUETOOTH_DRIVER_MODULE_ARG "fw_name=mrvl/sd8787_uapsta.bin"
#endif


#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef HCI_DEV_ID
#define HCI_DEV_ID 0
#endif

#define HCID_START_DELAY_SEC   1
#define HCID_STOP_DELAY_USEC 500000
#define HCIATTACH_STOP_DELAY_SEC 1
#define FM_ENABLE_DELAY_SEC  3


/** BIT value */
#define MBIT(x)    (1 << (x))
#define DRV_MODE_STA       MBIT(0)
#define DRV_MODE_UAP       MBIT(1)
#define DRV_MODE_WIFIDIRECT       MBIT(2)

#define STA_WEXT_MASK        MBIT(0)
#define UAP_WEXT_MASK        MBIT(1)
#define STA_CFG80211_MASK    MBIT(2)
#define UAP_CFG80211_MASK    MBIT(3)

#define info(fmt, ...)  ALOGI ("%s(L%d): " fmt,__FUNCTION__, __LINE__,  ## __VA_ARGS__)
#define debug(fmt, ...) ALOGD ("%s(L%d): " fmt,__FUNCTION__, __LINE__,  ## __VA_ARGS__)
#define warn(fmt, ...) ALOGW ("## WARNING : %s(L%d): " fmt "##",__FUNCTION__, __LINE__, ## __VA_ARGS__)
#define error(fmt, ...) ALOGE ("## ERROR : %s(L%d): " fmt "##",__FUNCTION__, __LINE__, ## __VA_ARGS__)
#define asrt(s) if(!(s)) ALOGE ("## %s assert %s failed at line:%d ##",__FUNCTION__, #s, __LINE__)

//SD8787 power state 
union POWER_SD8787
{
    unsigned int on; // FALSE, means off, others means ON
    struct 
    {
        unsigned int wifi_on:1;  //TRUE means on, FALSE means OFF
        unsigned int uap_on:1;
        unsigned int bt_on:1;
        unsigned int fm_on:1;
    }type;
} power_sd8787;

//Static paths and args
static const char* WIFI_DRIVER_MODULE_1_PATH = 	WIFI_SDIO_IF_DRIVER_MODULE_PATH;
static const char* WIFI_DRIVER_MODULE_1_NAME =  WIFI_SDIO_IF_DRIVER_MODULE_NAME;
static const char* WIFI_DRIVER_MODULE_1_ARG =   WIFI_SDIO_IF_DRIVER_MODULE_ARG;

static const char* WIFI_DRIVER_MODULE_2_PATH = WIFI_DRIVER_MODULE_PATH;
static const char* WIFI_DRIVER_MODULE_2_NAME = WIFI_DRIVER_MODULE_NAME;
//5: DRV_MODE_STA|DRV_MODE_WIFIDIRECT,  4:STA_CFG80211_MASK
static const char* WIFI_DRIVER_MODULE_2_ARG =  WIFI_DRIVER_MODULE_ARG;
//3:DRV_MODE_STA|DRV_MODE_UAP  0x0c:STA_CFG80211_MASK|UAP_CFG80211_MASK
static const char* WIFI_UAP_DRIVER_MODULE_2_ARG =        "drv_mode=3 cfg80211_wext=0xc";
static const char* WIFI_DRIVER_IFAC_NAME =         "/sys/class/net/wlan0";
static const char* WIFI_UAP_DRIVER_IFAC_NAME =         "/sys/class/net/uap0";

//WIFI init and cal config file
static const char* WIFI_DRIVER_MODULE_INIT_ARG = " init_cfg=wifi_init_cfg.conf";
static const char* WIFI_DRIVER_MODULE_INIT_CFG_PATH = "/etc/firmware/wifi_init_cfg.conf";

static const char* WIFI_DRIVER_MODULE_CAL_DATA_ARG = " cal_data_cfg=wifi_cal_data.conf";
static const char* WIFI_DRIVER_MODULE_CAL_DATA_CFG_PATH = "/etc/firmware/wifi_cal_data.conf";

static const char* BT_DRIVER_MODULE_PATH = BLUETOOTH_DRIVER_MODULE_PATH;
static const char* BT_DRIVER_MODULE_NAME = BLUETOOTH_DRIVER_MODULE_NAME;
static const char* BT_DRIVER_MODULE_ARG  = BLUETOOTH_DRIVER_MODULE_ARG;

static const char* WIFI_DRIVER_MODULE_COUNTRY_ARG = " reg_alpha2=";


//Bluetooth init and cal config file
static const char* BT_DRIVER_MODULE_INIT_ARG = " init_cfg=bt_init_cfg.conf";
static const char* BT_DRIVER_MODULE_INIT_CFG_PATH = "/etc/firmware/bt_init_cfg.conf";
static const char* BT_DRIVER_MODULE_CAL_DATA_ARG = " init_cfg=bt_cal_data.conf";
static const char* BT_DRIVER_MODULE_CAL_DATA_CFG_PATH = "/etc/firmware/bt_cal_data.conf";
static const char* BT_DRIVER_IFAC_NAME =     "/sys/class/bt_fm_nfc/mbtchar0";

static const char* WIRELESS_UNIX_SOCKET_DIR = "/data/misc/wifi/sockets/socket_daemon";

static const char* MODULE_FILE = "/proc/modules";


static const char DRIVER_PROP_NAME[]    = "wlan.driver.status";
static const char WIFI_COUNTRY_CODE[] = "wifi.country_code";

static char *rfkill_state_path_wifi = NULL;
static char *rfkill_state_path_bluetooth = NULL;

static char wifi_drvdbg_arg[MAX_DRV_ARG_SIZE];
static char bt_drvdbg_arg[MAX_DRV_ARG_SIZE];

static int flag_exit = 0;
static int debug = 1;


int insmod(const char *filename, const char *args)
{
    void *module = NULL;
    unsigned int size = 0;
    int ret = 0;
    module = load_file(filename, &size);
    if (!module)
    {
        ALOGD("loadfile:%s, error: %s", filename, strerror(errno));
        ret = -1;
        goto out;
    }
    ret = init_module(module, size, args);
    free(module);
out:
    ALOGD("insmod:%s,args %s, size %d, ret: %d", filename, args, size, ret);
    return ret;
}

int rmmod(const char *modname)
{
    int ret = -1;
    int maxtry = 10;

    while (maxtry-- > 0) {
        ret = delete_module(modname, O_NONBLOCK | O_EXCL);
        if (ret < 0 && errno == EAGAIN)
            usleep(500000);
        else
            break;
    }

    if (ret != 0)
        ALOGD("Unable to unload driver module \"%s\": %s\n",
             modname, strerror(errno));
    return ret;
}

int check_driver_loaded(const char *modname) 
{
    FILE *proc = NULL;
    char line[64];
    int ret = FALSE;

    if ((proc = fopen(MODULE_FILE, "r")) == NULL) 
    {
        ALOGW("Could not open %s: %s", MODULE_FILE, strerror(errno));
        ret = FALSE;
        goto out;
    }
    while ((fgets(line, sizeof(line), proc)) != NULL) 
    {
        if (strncmp(line, modname, strlen(modname)) == 0) 
        {
            fclose(proc);
            ret = TRUE;    
            goto out;
        }
    }
    fclose(proc);
out:
    ALOGD("check_driver_loaded %s: ret:%d", modname, ret);
    return ret;
}


static void android_set_aid_and_cap(void) {
    int ret = -1;
    prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0);

    gid_t groups[] = {AID_BLUETOOTH, AID_WIFI, AID_NET_BT_ADMIN, AID_NET_BT, AID_INET, AID_NET_RAW, AID_NET_ADMIN};
    if ((ret = setgroups(sizeof(groups)/sizeof(groups[0]), groups)) != 0){
        ALOGE("setgroups failed, ret:%d, strerror:%s", ret, strerror(errno));
        return;
    }

    if(setgid(AID_SYSTEM) != 0){
        ALOGE("setgid failed, ret:%d, strerror:%s", ret, strerror(errno));
        return;
    }

    if ((ret = setuid(AID_SYSTEM)) != 0){
        ALOGE("setuid failed, ret:%d, strerror:%s", ret, strerror(errno));
        return;
    }

    struct __user_cap_header_struct header;
    struct __user_cap_data_struct cap;
    header.version = _LINUX_CAPABILITY_VERSION;
    header.pid = 0;

    cap.effective = cap.permitted = 1 << CAP_NET_RAW |
    1 << CAP_NET_ADMIN |
    1 << CAP_NET_BIND_SERVICE |
    1 << CAP_SYS_MODULE |
    1 << CAP_IPC_LOCK;

    cap.inheritable = 0;
    if ((ret = capset(&header, &cap)) != 0){
        ALOGE("capset failed, ret:%d, strerror:%s", ret, strerror(errno));
        return;
    }
    return;
}

static int ensure_config_file_exists(const char *config_file,const char* template_file)
{
    char buf[2048];
    int srcfd, destfd;
    struct stat sb;
    int nread;
    int ret;

    ret = access(config_file, R_OK|W_OK);
    if ((ret == 0) || (errno == EACCES)) {
        if ((ret != 0) &&
            (chmod(config_file, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) != 0)) {
            ALOGE("Cannot set RW to \"%s\": %s", config_file, strerror(errno));
            return -1;
        }
        /* return if filesize is at least 10 bytes */
        if (stat(config_file, &sb) == 0 ) {
			return 0;
        }
    } else if (errno != ENOENT) {
        ALOGE("Cannot access \"%s\": %s", config_file, strerror(errno));
        return -1;
    }

    srcfd = TEMP_FAILURE_RETRY(open(template_file, O_RDONLY));
    if (srcfd < 0) {
        ALOGE("Cannot open \"%s\": %s", template_file, strerror(errno));
        return -1;
    }

    destfd = TEMP_FAILURE_RETRY(open(config_file, O_CREAT|O_RDWR, 0660));
    if (destfd < 0) {
        close(srcfd);
        ALOGE("Cannot create \"%s\": %s", config_file, strerror(errno));
        return -1;
    }

    while ((nread = TEMP_FAILURE_RETRY(read(srcfd, buf, sizeof(buf)))) != 0) {
        if (nread < 0) {
            ALOGE("Error reading \"%s\": %s", template_file, strerror(errno));
            close(srcfd);
            close(destfd);
            unlink(config_file);
            return -1;
        }
        TEMP_FAILURE_RETRY(write(destfd, buf, nread));
    }

    close(destfd);
    close(srcfd);

    /* chmod is needed because open() didn't set permisions properly */
    if (chmod(config_file, 0660) < 0) {
        ALOGE("Error changing permissions of %s to 0660: %s",
             config_file, strerror(errno));
        unlink(config_file);
        return -1;
    }

    if (chown(config_file, AID_SYSTEM, AID_WIFI) < 0) {
        ALOGE("Error changing group ownership of %s to %d: %s",
             config_file, AID_WIFI, strerror(errno));
        unlink(config_file);
        return -1;
    }


	return 0;
	
}



int uap_enable(void)
{
    int ret = 0;
    power_sd8787.type.uap_on = TRUE;
    ret = wifi_uap_enable(WIFI_UAP_DRIVER_MODULE_2_ARG);
    if(ret < 0)goto out;
    ret = wait_interface_ready (WIFI_UAP_DRIVER_IFAC_NAME);
    if(ret < 0)goto out;
#ifdef SD8787_NEED_CALIBRATE
    ret = wifi_calibrate();        
#endif
out:
    return ret;
}

int wifi_set_drv_arg(char * wifi_drv_arg) {
    if (strlen(wifi_drv_arg) >= MAX_DRV_ARG_SIZE) {
        ALOGW("The wifi driver arg[%s] is too long( >= %d )!", wifi_drv_arg, MAX_DRV_ARG_SIZE);
        return -1;
    }
    memset(wifi_drvdbg_arg, 0, MAX_DRV_ARG_SIZE);
    strcpy(wifi_drvdbg_arg, wifi_drv_arg);

    return 0;
}

int bt_set_drv_arg(char * bt_drv_arg) {
    if (strlen(bt_drv_arg) >= MAX_DRV_ARG_SIZE) {
        ALOGW("The bt driver arg[%s] is too long( >= %d )!", bt_drv_arg, MAX_DRV_ARG_SIZE);
        return -1;
    }
    memset(bt_drvdbg_arg, 0, MAX_DRV_ARG_SIZE);
    strcpy(bt_drvdbg_arg, bt_drv_arg);

    return 0;
}





int wifi_enable(void)
{
    int ret = 0;
    power_sd8787.type.wifi_on = TRUE;
    ret = wifi_uap_enable(WIFI_DRIVER_MODULE_2_ARG);
    if(ret < 0)goto out;
    ret = wait_interface_ready(WIFI_DRIVER_IFAC_NAME);
    if(ret < 0)
    {
        property_set(DRIVER_PROP_NAME, "timeout");
        goto out;
    }
#ifdef SD8787_NEED_CALIBRATE
    ret = wifi_calibrate();
#endif
out:
    if(ret == 0)
    {
        property_set(DRIVER_PROP_NAME, "ok");
    }
    else
    {
        property_set(DRIVER_PROP_NAME, "failed");
    }
    return ret;
}


int wifi_disable(void)
{
    int ret = 0;
    power_sd8787.type.wifi_on = FALSE;
    power_sd8787.type.uap_on = FALSE;
    ret = wifi_uap_disable();
    if(ret == 0)
    {
        property_set(DRIVER_PROP_NAME, "unloaded");
    }
    else
    {
        property_set(DRIVER_PROP_NAME, "failed");
    }
    return ret;
}




#ifdef SD8787_NEED_CALIBRATE
int wifi_calibrate(void)
{
    int ret = 0;
    int ret2 = system("mlanconfig wlan0 regrdwr 5 0x26 0x30");
    if(ret2 != 0)
    {
        ALOGD("calibrate:mlanconfig wlan0 regrdwr 5 0x26 0x30 , ret: 0x%x, strerror: %s", ret2, strerror(errno));
    }
    if(ret2 < 0)
    {
        ret = ret2;
    }
    return ret;
}
#endif

int uap_disable(void)
{
    int ret = 0;
    power_sd8787.type.uap_on = FALSE;
    ret = wifi_uap_disable();
    return ret;
}

int bt_enable(void) 
{
    int ret = 0;
    power_sd8787.type.bt_on = TRUE;

    ret = bt_fm_enable();
	
out:
    return ret;
}


int bt_disable() 
{
    int ret = 0;

    power_sd8787.type.bt_on = FALSE;
    if(power_sd8787.type.fm_on == FALSE)
    {
        ret = bt_fm_disable();
    }
out:
    return ret;
}

int fm_enable(void) 
{
    int ret = 0;
    power_sd8787.type.fm_on = TRUE;

    ret = bt_fm_enable();
    if(ret < 0)goto out;
    sleep(FM_ENABLE_DELAY_SEC);
out:
    return ret;
}

int fm_disable() 
{
    int ret = 0;
    power_sd8787.type.fm_on = FALSE;
    if(power_sd8787.type.bt_on == FALSE)
    {
        ret = bt_fm_disable();
    }
    return ret;
}

static int config_file_exist(const char* config_file)
{
    if (access(config_file, F_OK) == 0)
    {
        return 1;
    }

    return 0;
}

static void change_wlan_config_attr(void){
	chmod("/proc/mwlan/config", 0766);
}

int wifi_uap_enable(const char* driver_module_arg)
{
    int ret = 0;
    char arg_buf[MAX_BUFFER_SIZE];
    char country_code[MAX_BUFFER_SIZE];

    ALOGD("wifi_uap_enable");
    memset(arg_buf, 0, MAX_BUFFER_SIZE);
    strcpy(arg_buf, driver_module_arg);
    strcat(arg_buf, wifi_drvdbg_arg);

    country_code[0] = '\0';
    property_get(WIFI_COUNTRY_CODE, country_code, "");
    if(strcmp("", country_code) != 0)
    {
        strcat(arg_buf, WIFI_DRIVER_MODULE_COUNTRY_ARG);
        strcat(arg_buf, country_code);
    }
    if (config_file_exist(WIFI_DRIVER_MODULE_INIT_CFG_PATH)){    
        ALOGD("The wifi config file exists");
        strcat(arg_buf, WIFI_DRIVER_MODULE_INIT_ARG);
    }
    if (config_file_exist(WIFI_DRIVER_MODULE_CAL_DATA_CFG_PATH)){    
		ALOGD("The wifi cal file exists");
        strcat(arg_buf, WIFI_DRIVER_MODULE_CAL_DATA_ARG);
    }

    ret = set_power(TRUE);
    if (ret < 0)
    {
        ALOGD("wifi_uap_enable, set_power fail");
        goto out;
    }
    if(check_driver_loaded(WIFI_DRIVER_MODULE_1_NAME) == FALSE)
    {
        ret = insmod(WIFI_DRIVER_MODULE_1_PATH, WIFI_DRIVER_MODULE_1_ARG);
        if (ret < 0)
        {
            ALOGD("wifi_uap_enable, insmod: %s %s fail", WIFI_DRIVER_MODULE_1_PATH, WIFI_DRIVER_MODULE_1_ARG);
            goto out;
        }
    }

    if(check_driver_loaded(WIFI_DRIVER_MODULE_2_NAME) == FALSE)
    {
    
		ALOGD("insmod wifi arguments %s\n",arg_buf);
        ret = insmod(WIFI_DRIVER_MODULE_2_PATH, arg_buf);
        if (ret < 0)
        {
            ALOGD("wifi_uap_enable, insmod: %s %s fail", WIFI_DRIVER_MODULE_2_PATH, arg_buf);
            goto out;
        }
    }

	change_wlan_config_attr();
	
out:
    return ret;
}


int wifi_uap_disable()
{
    int ret = 0;
    if(check_driver_loaded(WIFI_DRIVER_MODULE_2_NAME) == TRUE)
    {
        ret = rmmod (WIFI_DRIVER_MODULE_2_NAME);
        if (ret < 0) goto out;
    }
    if(check_driver_loaded(WIFI_DRIVER_MODULE_1_NAME) == TRUE)
    {
        ret = rmmod (WIFI_DRIVER_MODULE_1_NAME);
        if (ret < 0) goto out;
    }
    ret = set_power(FALSE);
out:
    return ret;
}


int bt_fm_enable(void)
{
    int ret = 0;
    char arg_buf[MAX_BUFFER_SIZE];


    ALOGD("bt_fm_enable");
    memset(arg_buf, 0, MAX_BUFFER_SIZE);
    strcpy(arg_buf, BT_DRIVER_MODULE_ARG);

    if (config_file_exist(BT_DRIVER_MODULE_INIT_CFG_PATH)){
        ALOGD("The bluetooth config file exists");
        strcat(arg_buf, BT_DRIVER_MODULE_INIT_ARG);
    }
	
    if (config_file_exist(BT_DRIVER_MODULE_CAL_DATA_CFG_PATH)){
        ALOGD("The bluetooth config file exists");
        strcat(arg_buf, BT_DRIVER_MODULE_CAL_DATA_ARG);
    }

    strcat(arg_buf, bt_drvdbg_arg);

    if(check_driver_loaded(BT_DRIVER_MODULE_NAME) == FALSE)
    {
    	ALOGD("insmod bt arguments %s\n",arg_buf);
        ret = insmod(BT_DRIVER_MODULE_PATH, arg_buf);
        if (ret < 0) {
            error("insmod %s failed\n", BT_DRIVER_MODULE_PATH);
            goto out;
        }
    }
    ret = set_power(TRUE);
    if (ret < 0)
    {
        ALOGD("bt_fm_enable, set_power fail: errno:%d, %s", errno, strerror(errno));
        goto out;
    }

out:
    return ret;
}

int bt_fm_disable(void)
{
    int ret = 0;
    if(check_driver_loaded(BT_DRIVER_MODULE_NAME) == TRUE)
    {
        ret = rmmod(BT_DRIVER_MODULE_NAME);
        if (ret < 0) goto out;
    }

    ret = set_power(FALSE);
out:
    return ret;
}


static int is_rfkill_disabled(void)
{
    char value[PROPERTY_VALUE_MAX];

    property_get("ro.rfkilldisabled", value, "0");
    if (strcmp(value, "1") == 0) {
        return 1;
    }

    return 0;
}

static int init_rfkill_subsystem(void)
{
    char path[64];
    char buf[16];
    int fd, sz, id;
	int rfkill_wifi_id=-1;
	int rfkill_bt_id=-1;

    if (is_rfkill_disabled())
        return -1;

    for (id = 0; ; id++)
    {
        snprintf(path, sizeof(path), "/sys/class/rfkill/rfkill%d/type", id);
        fd = open(path, O_RDONLY);
        if (fd < 0) break;

        sz = read(fd, &buf, sizeof(buf));
        close(fd);

        if (sz >= 4 && memcmp(buf, "wifi", 4) == 0){
            rfkill_wifi_id = id;
            continue;
        }
        if (sz >= 9 && memcmp(buf, "bluetooth", 9) == 0){
            rfkill_bt_id = id;
            continue;
        }
    }

	if(rfkill_bt_id>=0){
    	asprintf(&rfkill_state_path_bluetooth, "/sys/class/rfkill/rfkill%d/state", rfkill_bt_id);
	}
	if(rfkill_wifi_id>=0){
    	asprintf(&rfkill_state_path_wifi, "/sys/class/rfkill/rfkill%d/state", rfkill_wifi_id);
	}
	
    return 0;
}

static int set_wireless_power(char* path,int on)
{
    int sz;
    int fd = -1;
    int ret = -1;
    char buffer = on?'1':'0';

    /* check if we have rfkill interface */
    if (is_rfkill_disabled())
        return 0;

	ALOGE("set_wireless_power %s",
		on?"on":"off");

    fd = open(path, O_WRONLY);

    if (fd < 0)
    {
        ALOGE("set_wireless_power : open(%s) for write failed: %s (%d)",
            path, strerror(errno), errno);
        return ret;
    }

    sz = write(fd, &buffer, 1);

    if (sz < 0) {
        ALOGE("set_wireless_power : write(%s) failed: %s (%d)",
            path, strerror(errno),errno);
    }
    else
        ret = 0;

    if (fd >= 0)
        close(fd);
		

    return ret;
}


int set_power(int on)
{
    if((!on) && (!(power_sd8787.on)))
    {
    	if(rfkill_state_path_wifi)
			set_wireless_power(rfkill_state_path_wifi,on);
		else if(rfkill_state_path_bluetooth)
			set_wireless_power(rfkill_state_path_bluetooth,on);
    }
    else if(on)
    {
    	if(rfkill_state_path_wifi)
			set_wireless_power(rfkill_state_path_wifi,on);
		else if(rfkill_state_path_bluetooth)
			set_wireless_power(rfkill_state_path_bluetooth,on);
    }
    return 0;
}



//to do: don’t use polling mode, use uevent, listen interface added uevent from driver
int wait_interface_ready (const char* interface_path)
{
#define TIMEOUT_VALUE 5000    //at most 5seconds
    int fd, count = TIMEOUT_VALUE;

    while(count--) {
        fd = open(interface_path, O_RDONLY);
        if (fd >= 0)
        {
            close(fd);
            return 0;
        }
        usleep(1000);
    }
    ALOGW("timeout(%dms) to wait %s", TIMEOUT_VALUE, interface_path);
    return -1;
}

static void kill_handler(int sig)
{
    ALOGI("Received signal %d.", sig);
    power_sd8787.on = FALSE;

    if (set_power(FALSE) < 0) 
    {
        ALOGW("set_fm_power failed.");
    }
    flag_exit = 1;
}

int serv_listen (const char* name)
{
    int fd,len;
    struct sockaddr_un unix_addr;

    /* Create a Unix domain stream socket */
    if ( (fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0)
        return (-1);
    unlink (name);
    /* Fill in socket address structure */
    memset (&unix_addr, 0, sizeof (unix_addr));
    unix_addr.sun_family = AF_UNIX;
    strcpy (unix_addr.sun_path, name);
    snprintf(unix_addr.sun_path, sizeof(unix_addr.sun_path), "%s", name);
    len = sizeof (unix_addr.sun_family) + strlen (unix_addr.sun_path);

    /* Bind the name to the descriptor */
    if (bind (fd, (struct sockaddr*)&unix_addr, len) < 0)
    {
        ALOGW("bind fd:%d and address:%s error: %s", fd, unix_addr.sun_path, strerror(errno));
        goto error;
    }
    if (chmod (name, 0666) < 0)
    {
        ALOGW("change %s mode error: %s", name, strerror(errno));
        goto error;
    }
    if (listen (fd, 5) < 0)
    {
        ALOGW("listen fd %d error: %s", fd, strerror(errno));
        goto error;
    }
    return (fd);

error:
    close (fd);
    return (-1); 
}

#define    STALE    30    /* client's name can't be older than this (sec) */


/* returns new fd if all OK, < 0 on error */
int serv_accept (int listenfd)
{
    int                clifd, len;
    time_t             staletime;
    struct sockaddr_un unix_addr;
    struct stat        statbuf;
    const char*        pid_str;

    len = sizeof (unix_addr);
    if ( (clifd = accept (listenfd, (struct sockaddr *) &unix_addr, &len)) < 0)
    {
        ALOGW("listenfd %d, accept error: %s", listenfd, strerror(errno));
        return (-1);        /* often errno=EINTR, if signal caught */
    }
    return (clifd);
}

//Command Handler
int cmd_handler(char* buffer)
{ 
    int ret = 0;
    ALOGD("marvell wireless daemon received cmd: %s\n", buffer);
    if(!strncmp(buffer, "WIFI_DISABLE", strlen("WIFI_DISABLE")))
        ret = wifi_disable();
    else if (!strncmp(buffer, "WIFI_ENABLE", strlen("WIFI_ENABLE")))
        ret = wifi_enable();
    if(!strncmp(buffer, "UAP_DISABLE", strlen("UAP_DISABLE")))
        ret = uap_disable();
    else if (!strncmp(buffer, "UAP_ENABLE", strlen("UAP_ENABLE")))
        ret = uap_enable();
    else if (!strncmp(buffer, "BT_DISABLE", strlen("BT_DISABLE")))
        ret = bt_disable();
    else if (!strncmp(buffer, "BT_ENABLE", strlen("BT_ENABLE")))
        ret = bt_enable();
    else if (!strncmp(buffer, "FM_DISABLE", strlen("FM_DISABLE")))
        ret = fm_disable();
    else if (!strncmp(buffer, "FM_ENABLE", strlen("FM_ENABLE")))
        ret = fm_enable();
    else if (!strncmp(buffer, "BT_OFF", strlen("BT_OFF"))) {
        power_sd8787.type.bt_on = 0;
        ret = set_power(0);
    } else if (!strncmp(buffer, "BT_ON", strlen("BT_ON"))) {
        power_sd8787.type.bt_on = 1;
        ret = set_power(1);
    } else if (!strncmp(buffer, "WIFI_DRV_ARG ", strlen("WIFI_DRV_ARG "))) {
        /* Note: The ' ' before the arg is needed */
        ret = wifi_set_drv_arg(buffer + strlen("WIFI_DRV_ARG"));
    } else if (!strncmp(buffer, "BT_DRV_ARG ", strlen("BT_DRV_ARG"))) {
        /* Note: The ' ' before the arg is needed */
        ret = bt_set_drv_arg(buffer + strlen("BT_DRV_ARG"));
    }
    return ret;
} 

void handle_thread(int clifd)
{
    int nread;
    char buffer[MAX_BUFFER_SIZE];
    int len = 0;
    int ret = 0;
    nread = read(clifd, buffer, sizeof (buffer));
    if (nread == SOCKERR_IO){
        ALOGE("read error on fd %d\n", clifd);
    }
    else if (nread == SOCKERR_CLOSED){
        ALOGE("fd %d has been closed.\n", clifd);
    }
    else {
        ALOGI("Got that! the data is %s\n", buffer);
        ret = cmd_handler(buffer);
    }
    if(ret == 0)
        strncpy(buffer, "0,OK", sizeof(buffer));
    else
        strncpy(buffer, "1,FAIL", sizeof(buffer));

    nread = write(clifd, buffer, strlen(buffer));

    if (nread == SOCKERR_IO) {
        ALOGE("write error on fd %d\n", clifd);
    }
    else if (nread == SOCKERR_CLOSED) {
        ALOGE("fd %d has been closed.\n", clifd);
    }
    else
        ALOGI("Wrote %s to client. \n", buffer);
}

//Daemon entry 
int main(int argc,char** argv)
{
    int listenfd = -1;
    int clifd = -1;

    power_sd8787.on = FALSE;
    //register SIGINT and SIGTERM, set handler as kill_handler
    struct sigaction sa;
    sa.sa_flags = SA_NOCLDSTOP;
    sa.sa_handler = kill_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

	//set cap in init.rc
    android_set_aid_and_cap();
	init_rfkill_subsystem();

    listenfd = serv_listen (WIRELESS_UNIX_SOCKET_DIR);
    if (listenfd < 0) 
    {
        ALOGE("serv_listen error.\n");
        return -1;
    }
    ALOGI("succeed to create socket and listen.\n");
    while (1)
    { 
        clifd = serv_accept (listenfd);
        if (clifd < 0) 
        {
            ALOGE("serv_accept error. \n");
            continue;
        }
        handle_thread(clifd);
        close (clifd);
    }
    close(listenfd);
    return 0; 
}



