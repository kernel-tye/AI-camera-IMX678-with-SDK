# source 这个文件即可用 atc (海思 SVP NPU 的模型编译器, Ascend CANN 6.10)
# 装在用户目录, 不需要 root. 转出来的 .om 是 PICO 格式, 直接给板子用。
A=/home/ty/Ascend/ascend-toolkit/svp_latest
export ASCEND_HOME=$A
export PATH=$A/atc/bin:$A/toolkit/bin:$PATH
export LD_LIBRARY_PATH=$A/atc/lib64:$A/atc/third_party_lib:$A/acllib/lib64:$LD_LIBRARY_PATH
export PYTHONPATH=$A/atc/python/site-packages:$A/toolkit/python/site-packages:$PYTHONPATH
export ASCEND_OPP_PATH=$A/opp
export ASCEND_AICPU_PATH=$A
export TOOLCHAIN_HOME=$A/toolkit
