#! /bin/bash
function help ()
{
cat <<EOF
-----------------------------------------------------------------
Version: 1.0
Autotest:Unit Test;
         WIFI/BT Open/Close Test;
         Meida autotest
         Security Test
Note: Media Test need to connect wifi and need to set BootTarget:
      Can use the following command
      "> setenv append_bootargs androidboot.selinux=permissive"
      "> saveenv"
Options:
 -h:      displays this help message
 -m:      Module name as follows:
          Media:     media
          WIFI/BT:   wifi
          Unit:      unit
          Security:  security
          Widevine:  widevine
 -t:      Case name of module as follows:
          Media:     quick
          WIFI/BT:   quick ; connect
          Unit:      quick
          Security:  quick
          Widevine:  quick
-------------------------------------------------------------------
EOF
}


adb_command=0
RED='\033[0;31m'
STD='\033[0;0m'
BLUE='\033[0;36m'
adb_device=""
test_module=0
test_case=0
casename=""
modulename=""
start_test=0
module_names="security media unit wifi graphic widevine"
case_names=""
file_list=(command_quick.txt test_list.txt FslOMXAutoTest.apk http_45_video_1.txt imx_unit_test security_unit_test WidevineTest)
function_list=(fun_run_unit fun_run_media fun_run_wifi fun_run_security fun_run_widevine)
module_name=(UNIT-MODULE MEDIA-MODULE WIFI-MODULE SECURITY-MODULE WIDEVINE-MODULE)

#--------------------------------------wget--file---------------------------------------
for file_item in "${file_list[@]}" ;do
    if [ -e $file_item ];then
        rm $file_item
    fi
    wget 10.193.108.179/share_write/gtest/${file_item} 2>/dev/null
done
#---------------------------------------wifi_test----------------------------------------
pre_status_wifi=0
pre_status_bt=0

fun_wifi_check ()
{
    if [ $1 -eq 0 ];then
        wifi_result=$($adb_c shell dumpsys wifi | head -1 | grep Wi-Fi)
        wifi_result2="enabled"
        echo $(echo $wifi_result | grep "${wifi_result2}")
    else
        bt_result=$($adb_c shell dumpsys bluetooth_manager | grep state)
        bt_result2="ON"
        echo $(echo $bt_result | grep "${bt_result2}")
    fi
}
fun_wifi_open ()
{
    if [ $1 -eq 0 ];then
        $adb_c shell svc wifi enable
        sleep 1
        echo $($adb_c shell dumpsys wifi| head -1 | grep Wi-Fi)
        local ret=$(fun_wifi_check 0)
        if [ -z "$ret" ];then
            echo "WiFi OPEN FAILED"
        else
            if [ $pre_status_wifi -eq 0 ];then
                echo "WiFi OPEN SUCCESSED"
                pre_status_wifi=1
            else
                echo "WiFi OPEN FAILED"
            fi
        fi
    else
        $adb_c shell svc bluetooth enable
        sleep 2
        echo $($adb_c shell dumpsys bluetooth_manager | grep state)
        local ret2=$(fun_wifi_check 1)
        if [ -z "$ret2" ];then
            echo "Bluetooth OPEN FAILED"
        else
            if [ $pre_status_bt -eq 0 ];then
                echo "Bluetooth OPEN SUCCESSED"
                pre_status_bt=1
            else
                echo "Bluetooth OPEN FAILED"
            fi
        fi
    fi
}
fun_wifi_close ()
{
    if [ $1 -eq 0 ];then
        $adb_c shell svc wifi disable
        sleep 1
        echo $($adb_c shell dumpsys wifi | head -1 | grep Wi-Fi)
        local ret=$(fun_wifi_check 0)
        if [ -z "$ret" ];then
            if [ $pre_status_wifi -eq 1 ];then
                echo "WiFi CLOSED SUCCESSED"
                pre_status_wifi=0
            else
                echo "WiFi CLOSED FAILED"
            fi
        else
            echo "WiFi CLOSED FAILED"
        fi
    else
        $adb_c shell svc bluetooth disable
        sleep 2
        echo $($adb_c shell dumpsys bluetooth_manager | grep state)
        local ret2=$(fun_wifi_check 1)
        if [ -z "$ret2" ];then
            if [ $pre_status_bt -eq 1 ];then
                    echo "Bluetooth CLOSED SUCCESSED"
                    pre_status_bt=0
            else
                    echo "Bluetooth CLOSED FAILED"
            fi
        else
            echo "Bluetooth CLOSED FAILED"
        fi
    fi
}
fun_wifi_on_off ()
{
    result=$(fun_wifi_check 0)
    if [ -z "$result" ];then
        echo "WiFi CURRENT STATE IS CLOSED"
        pre_status_wifi=0
        fun_wifi_open 0
        sleep 1
        fun_wifi_close 0
        sleep 1
    else
        echo "WiFi CURRENT STATE IS OPENED"
        pre_status_wifi=1
        fun_wifi_close 0
        sleep 1
        fun_wifi_open 0
        sleep 1
    fi
    result2=$(fun_wifi_check 1)
    if [ -z "$result2" ];then
        echo "Bluetooth CURRENT STATE IS CLOSED"
        pre_status_bt=0
        fun_wifi_open 1
        sleep 2
        fun_wifi_close 1
        sleep 1
    else
        echo "Bluetooth CURRENT STATE IS OPENED"
        pre_status_bt=1
        fun_wifi_close 1
        sleep 2
        fun_wifi_open 1
        sleep 1
    fi
}
fun_run_wifi ()
{
    if [ $# -eq 0 ];then
        fun_wifi_on_off
    else
        case "$1" in
        "quick") fun_wifi_on_off
                ;;
        "connect")
                ;;
        *)    echo -e "${RED}===No found case===${STD}"
                exit 1;;
        esac
    fi
}
#-----------------------------------wifi_test_end--------------------------------------
#---------------------------------media_test_start-------------------------------------
fun_check_testbase ()
{
    $adb_c shell ls /data/autotest >/dev/null
}
fun_check_testresult ()
{
    $adb_c shell ls /data/autotest_result >/dev/null
}
fun_checkapk ()
{
    local result=$($adb_c shell pm list packages fslomxautotest)
    if [ -z $result ];then
        apk_is_install=1
    else
        apk_is_install=0
    fi
}
fun_media_test ()
{
    #create autotest dir
    fun_check_testbase
    if [ $? -eq 0 ];then
        echo "autotest directory exists"
    else
        echo "=========Create autotest directory========="
        $adb_c shell mkdir /data/autotest
    fi

    #create autotest_result dir
    fun_check_testresult
    if [ $? -eq 0 ];then
        echo "autotest_result directory exists"
    else
        echo "=========Create autotest_result directory========="
        $adb_c shell mkdir /data/autotest_result
    fi
    $adb_c shell chmod 777 /data/autotest_result
    $adb_c shell chmod 777 /data/autotest

    #push necessary file
    $adb_c push command_quick.txt /data/autotest/
    if [ $? -eq 0 ];then
        echo "==========push command_quick.txt sucessful========="
    else
        echo "==========push command_quick.txt failed========="
    fi
    $adb_c push test_list.txt /data/autotest/
    if [ $? -eq 0 ];then
        echo "==========push test_list.txt sucessful========="
    else
        echo "==========push test_list.txt failed========="
    fi
    $adb_c push http_45_video_1.txt /data/autotest/
    if [ $? -eq 0 ];then
        echo "==========push http_45_video_1.txt sucessful========="
    else
        echo "==========push http_45_video_1.txt failed========="
    fi
    $adb_c shell chmod 666 /data/autotest/*

    #install apk
    fun_checkapk
    if [ $apk_is_install -eq 1 ];then
        $adb_c install FslOMXAutoTest.apk
    fi
    #judge if install successfully
    fun_checkapk
    if [ $apk_is_install -eq 1 ];then
        return 1
    else
        $adb_c shell am start -n com.nxp.snc.fslomxautotest/.MainActivity
        return 0
    fi
}
fun_run_media ()
{
    if [ $# -eq 0 ];then
        fun_media_test
    else
        case "$1" in
          "quick")fun_media_test
                  ;;
           *) echo -e "${RED}===No found case===${STD}"
              exit 1;;
        esac
    fi
}
#----------------------------------------media_test_end---------------------------------------
#-------------------------------------Secutity_test_start-------------------------------------
fun_check_security_file()
{
    $adb_c shell ls /data/security_unit_test >/dev/null 2>&1
}
fun_run_security ()
{
    fun_check_security_file
    if [ $? -ne 0 ];then
        chmod 777 security_unit_test
        $adb_c push security_unit_test /data/
    fi
    if [ $# -eq 0 ];then
        $adb_c shell /data/security_unit_test
    else
        case "$1" in
          "quick")
                  $adb_c shell /data/security_unit_test
                  ;;
            *)  echo -e "${RED}===No found case===${STD}"
                exit 1;;
        esac
    fi
}
#-------------------------------------Secutity_test_end-------------------------------------
#--------------------------------------Unit_test_start--------------------------------------
fun_check_unit_file()
{
    $adb_c shell ls /data/imx_unit_test &>/dev/null
}
fun_run_unit ()
{
    fun_check_unit_file
    if [ $? -ne 0 ];then
        chmod 777 imx_unit_test
        $adb_c push imx_unit_test /data/
    fi
    if [ $# -eq 0 ];then
        $adb_c shell /data/imx_unit_test
    else
        case "$1" in
           "quick")
                   $adb_c shell /data/imx_unit_test
                   ;;
            *)  echo -e "${RED}===No found case===${STD}"
                exit 1;;
        esac
    fi
}
#-------------------------------------Unit_test_end------------------------------------
#-------------------------------------Widevine_test------------------------------------
fun_check_widevine_hal()
{
    adb shell lshal | grep "android.hardware.drm@1.0::IDrmFactory/widevine" >/dev/null 2>&1
}
fun_run_widevine()
{
    fun_check_widevine_hal
    if [ $? -ne 0 ];then
        echo -e ${RED}There is no widevine hal${STD}
        exit 1
    else
        echo -e ${BLUE}There is widevine hal${STD}
        chmod 777 WidevineTest
        $adb_c push WidevineTest /data/
    fi
    if [ $# -eq 0 ];then
        $adb_c shell /data/WidevineTest
    else
        case "$1" in
           "quick")
                   $adb_c shell /data/WidevineTest
                   ;;
            *)  echo -e "${RED}===No found case===${STD}"
                exit 1;;
        esac
    fi

}
#-----------------------------------Widevine_test_end----------------------------------
fun_find_module ()
{
    case "$1" in
        "media")     fun_run_media    $2;;
        "wifi")      fun_run_wifi     $2;;
        "security")  fun_run_security $2;;
        "unit")      fun_run_unit     $2;;
        "widevine")  fun_run_widevine $2;;
        *)          echo -e "${RED}No found module${STD}"
                    return 1;;
    esac
}
while [ -n "$1" ]
do
    case "$1" in
        -h) help; exit ;;
        -s) adb_command=1
            adb_device="$2"
            shift;;
        -m) test_module=1
            modulename="$2"
            shift;;
        -t) test_case=1
            casename="$2"
            shift;;
        *)  echo -e ${RED}Unknown Command $1${STD}
            exit 1;;
    esac
    shift
done

#if do not input -m but input -t , show error
if [ $test_module -eq 0 ] && [ $test_case -eq 1 ];then
    echo -e ${RED}No Module Input!!${STD}
    exit 1
fi

#set adb devices
if [ $adb_command -eq 1 ];then
    adb_c="adb -s $adb_device"
else
    adb_c="adb"
fi
$adb_c root >/dev/null

#Determine whether the module exists
if [ $test_module -eq 1 ];then
    #exists
    if [[ ${module_names/$modulename/} != ${module_names} ]];then
        if [ -n "$casename" ];then
            echo -e ${BLUE}\[$modulename --$casename\]\:START${STD}
            fun_find_module $modulename $casename
            if [ $? -eq 0 ];then
                echo -e ${BLUE}\[$modulename --$casename\]\:END${STD}
            else
                echo -e ${RED}\[$modulename --$casename\]\:FAILED${STD}
            fi
        else
            echo -e ${BLUE}\[$modulename\]\:START${STD}
            fun_find_module $modulename
            if [ $? -eq 0 ];then
               echo -e ${BLUE}\[$modulename\]\:END${STD}
            else
               echo -e ${RED}\[$modulename\]\:FAILED${STD}
            fi
        fi
    else
        echo -e ${RED}Module Not exists!!${STD}
        exit 1
    fi
else
    echo -e ${BLUE}\[ALL MODULE\]\:START${STD}
    i=0
    for function_item in "${function_list[@]}"; do
        echo -e ${BLUE}\[${module_name[$i]}\]\:START${STD}
        $function_item
        if [ $? -eq 0 ];then
            echo -e ${BLUE}\[${module_name[$i]}\]\:END${STD}
        else
            echo -e ${RED}\[${module_name[$i]}\]\:FAILED${STD}
        fi
        i=$[ $i + 1 ]
    done
    echo -e ${BLUE}\[ALL MODULE\]\:END${STD}
fi
