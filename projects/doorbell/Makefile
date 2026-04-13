#SDK_DIR ?= $(abspath ../..)
SDK_DIR =/home/liyy/work/bk_avdk_smp_v3.1.1.8/bk_avdk_smp

PROJECT_MAKE_FILE := $(SDK_DIR)/tools/build_tools/build_files/project_main.mk
ifeq ($(wildcard $(PROJECT_MAKE_FILE)),)
    $(error "$(PROJECT_MAKE_FILE) not exist, please check sdk directory.")
endif
include $(PROJECT_MAKE_FILE)
