#!/bin/bash
set -e

######################## 1 ################################
# check path
curr_path_abs=$(cd `dirname $0`; pwd)
curr_folder_name="${curr_path_abs##*/}"
echo $curr_path_abs
if [[ "${curr_folder_name}" != "gprs_a9" ]]; then
    echo "Plese exec build.sh in gprs_a9 folder"
    exit 1
fi

######################### 2 ###############################
# check parameters
CFG_RELEASE=debug
if [[ "$1xx" == "releasexx" ]]; then
    CFG_RELEASE=release
fi

###########################################################

function patch_elf()
{
    echo ">> Patching elf"
    rsync --read-batch=libcsdk-patches/SW_V2129_csdk.elf.patch ../../lib/GPRS_C_SDK/platform/csdk/debug/SW_V2129_csdk.elf
    md5sum -c --quiet libcsdk-patches/md5
}

function generate_CSDK_lib()
{
    echo ">> Generate CSDK lib now"
    cd ../../  #root path of micropython project
    cd lib/GPRS_C_SDK/platform/tools/genlib
    chmod +x genlib.sh
    PATH=$PATH:$curr_path_abs/../../lib/csdtk42-linux/bin
    export LD_LIBRARY_PATH=$curr_path_abs/../../lib/csdtk42-linux/lib
    ./genlib.sh ${CFG_RELEASE}
    cd ../../../../../
    if [[ -f "lib/GPRS_C_SDK/hex/libcsdk/libcsdk_${CFG_RELEASE}.a" ]]; then
        echo ">> Geneate CSDK lib compelte"
    else
        echo ">> Generate CSDK lib fail, please check error"
        exit 1
    fi
    cd ${curr_path_abs}
}

patch_elf
generate_CSDK_lib

